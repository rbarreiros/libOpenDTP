/*
 * Copyright (C) 2026 Rui Barreiros <rbarreiros@gmail.com>
 *
 * This file is part of libOpenDTP.
 *
 * libOpenDTP is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * libOpenDTP is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libOpenDTP.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "opendtp/Client.hpp"
#include "opendtp/Protocol.hpp"
#include "Sha256.hpp"

#include <asio.hpp>
#include <iostream>
#include <chrono>

namespace opendtp {

Client::Client(uint32_t dmrId, std::string description)
    : dmrId_(dmrId),
      description_(std::move(description)),
      ioContext_(),
      socket_(ioContext_),
      keepAliveTimer_(ioContext_),
      connectTimeoutTimer_(ioContext_),
      resolver_(ioContext_) {}

Client::~Client() {
    try {
        Disconnect();
    } catch (...) {
        // Prevent destructor exceptions
    }
}

ClientError Client::Connect(const std::string& host, uint16_t port, const std::string& password, uint32_t timeoutMs) {
    std::lock_guard<std::mutex> lock(connectionMutex_);

    if (state_ != State::DISCONNECTED) {
        return ClientError::ALREADY_CONNECTED;
    }

    password_ = password;
    routineSeq_ = 0;
    realTimeSeq_ = 0;
    inCall_ = false;
    lastError_ = ClientError::SUCCESS;

    try {
        // Start background worker thread
        ioContext_.restart();
        
        // Work guard to keep io_context running
        auto workGuard = asio::make_work_guard(ioContext_);
        
        ioThread_ = std::thread([this]() {
            try {
                ioContext_.run();
            } catch (const std::exception& e) {
                std::cerr << "OpenDTP client background thread exception: " << e.what() << std::endl;
            }
        });

        // Set state to CONNECTING
        state_ = State::CONNECTING;

        // Start connection timeout timer
        connectTimeoutTimer_.expires_after(std::chrono::milliseconds(timeoutMs));
        connectTimeoutTimer_.async_wait([this](const std::error_code& ec) {
            if (ec == asio::error::operation_aborted) {
                return;
            }
            std::lock_guard<std::mutex> lock(connectionMutex_);
            if (state_ == State::CONNECTING || state_ == State::AUTHENTICATING) {
                lastError_ = ClientError::RESPONSE_TIMEOUT;
                if (onError) {
                    onError(ClientError::RESPONSE_TIMEOUT, "Connection timed out during handshake");
                }
                Disconnect();
                connectionCv_.notify_all();
            }
        });

        // Perform async DNS resolution
        resolver_.async_resolve(host, std::to_string(port),
            [this](const std::error_code& ec, asio::ip::udp::resolver::results_type endpoints) {
                std::lock_guard<std::mutex> lock(connectionMutex_);
                if (state_ != State::CONNECTING) {
                    return; // Connection aborted or already failed
                }

                if (ec || endpoints.empty()) {
                    lastError_ = ClientError::DNS_RESOLVE;
                    std::string errMsg = ec ? ec.message() : "No endpoints resolved";
                    if (onError) {
                        onError(ClientError::DNS_RESOLVE, "DNS resolution failed: " + errMsg);
                    }
                    Disconnect();
                    connectionCv_.notify_all();
                    return;
                }

                serverEndpoint_ = *endpoints.begin();

                std::error_code openEc;
                socket_.open(serverEndpoint_.protocol(), openEc);
                if (openEc) {
                    lastError_ = ClientError::SOCKET_IO;
                    if (onError) {
                        onError(ClientError::SOCKET_IO, "Failed to open socket: " + openEc.message());
                    }
                    Disconnect();
                    connectionCv_.notify_all();
                    return;
                }

                StartReceiveLoop();
                SendKeepAlive();
            });

    } catch (const std::exception& e) {
        state_ = State::DISCONNECTED;
        if (onError) {
            onError(ClientError::SOCKET_IO, std::string("Socket error during connection startup: ") + e.what());
        }
        return ClientError::SOCKET_IO;
    }

    return ClientError::SUCCESS;
}

ClientError Client::ConnectSync(const std::string& host, uint16_t port, const std::string& password, uint32_t timeoutMs) {
    ClientError startErr = Connect(host, port, password, timeoutMs);
    if (startErr != ClientError::SUCCESS) {
        return startErr;
    }

    std::unique_lock<std::mutex> lock(connectionMutex_);
    connectionCv_.wait(lock, [this]() {
        return state_ == State::CONNECTED || state_ == State::DISCONNECTED;
    });

    if (state_ != State::CONNECTED) {
        return (lastError_ != ClientError::SUCCESS) ? lastError_ : ClientError::RESPONSE_TIMEOUT;
    }

    return ClientError::SUCCESS;
}

void Client::Disconnect() {
    State expected = state_.load();
    if (expected != State::DISCONNECTED) {
        state_ = State::DISCONNECTED;

        if (socket_.is_open()) {
            // Send CLOSE packet to BM server
            try {
                TransmitRaw(static_cast<uint16_t>(MessageType::CLOSE), MessageFlag::NONE, {});
            } catch (...) {}
        }

        StopKeepAliveTimer();

        std::error_code ec;
        connectTimeoutTimer_.cancel(ec);
        resolver_.cancel();
        socket_.close(ec);

        ioContext_.stop();

        if (onDisconnected) {
            onDisconnected(lastError_);
        }
    }

    // Always join the thread if we are not on it, and it's joinable
    if (ioThread_.joinable() && std::this_thread::get_id() != ioThread_.get_id()) {
        ioThread_.join();
    }
}

void Client::Subscribe(uint32_t talkgroup) {
    if (state_ != State::CONNECTED) return;
    
    protocol::SubscriptionData data{SESSION_TYPE_GROUP_VOICE, talkgroup};
    auto payload = protocol::SerializeSubscription(data);
    TransmitRaw(static_cast<uint16_t>(MessageType::SUBSCRIPTION), MessageFlag::NONE, payload);
}

void Client::Unsubscribe(uint32_t talkgroup) {
    if (state_ != State::CONNECTED) return;

    protocol::SubscriptionData data{SESSION_TYPE_GROUP_VOICE, talkgroup};
    auto payload = protocol::SerializeSubscription(data);
    TransmitRaw(static_cast<uint16_t>(MessageType::CANCELLING), MessageFlag::NONE, payload);
}

void Client::StartVoiceCall(uint32_t sourceId, uint32_t destinationId, bool isGroup) {
    if (state_ != State::CONNECTED) return;

    inCall_ = true;
    realTimeSeq_ = 0;

    // Build standard 12-byte DMR Voice Header LC PDU
    uint8_t header[12];
    std::memset(header, 0, 12);

    // Byte 0: protect = 0, reserved = 0, FLCO = 0 for group, 3 for private
    header[0] = isGroup ? 0x00 : 0x03;
    // Byte 1: FID = 0
    header[1] = 0x00;
    // Byte 2: service options = 0
    header[2] = 0x00;
    // Bytes 3-5: Destination ID (Big Endian 3 bytes)
    header[3] = static_cast<uint8_t>((destinationId >> 16) & 0xFF);
    header[4] = static_cast<uint8_t>((destinationId >> 8) & 0xFF);
    header[5] = static_cast<uint8_t>(destinationId & 0xFF);
    // Bytes 6-8: Source ID (Big Endian 3 bytes)
    header[6] = static_cast<uint8_t>((sourceId >> 16) & 0xFF);
    header[7] = static_cast<uint8_t>((sourceId >> 8) & 0xFF);
    header[8] = static_cast<uint8_t>(sourceId & 0xFF);
    // Bytes 9-11: CRC (zero, skipped calculation per wiki spec)
    header[9] = 0x00;
    header[10] = 0x00;
    header[11] = 0x00;

    // Send the Voice Header 3 times to ensure delivery over UDP
    TransmitRaw(static_cast<uint16_t>(MessageType::DMR_DATA_BASE) + 1, MessageFlag::REAL_TIME_1, header);
    TransmitRaw(static_cast<uint16_t>(MessageType::DMR_DATA_BASE) + 1, MessageFlag::REAL_TIME_1, header);
    TransmitRaw(static_cast<uint16_t>(MessageType::DMR_DATA_BASE) + 1, MessageFlag::REAL_TIME_1, header);
}

void Client::SendAudioFrame(std::span<const uint8_t> ambeData) {
    if (state_ != State::CONNECTED || !inCall_) return;

    TransmitRaw(static_cast<uint16_t>(MessageType::DMR_AUDIO_FRAME), MessageFlag::REAL_TIME_1, ambeData);
}

void Client::EndVoiceCall() {
    if (state_ != State::CONNECTED || !inCall_) return;

    // Send Voice Terminator (DMR_DATA_BASE + 2)
    TransmitRaw(static_cast<uint16_t>(MessageType::DMR_DATA_BASE) + 2, MessageFlag::REAL_TIME_1, {});
    inCall_ = false;
}

void Client::SendTextMessage(uint32_t destinationId, bool isGroup, const std::string& text) {
    if (state_ != State::CONNECTED) return;

    protocol::TextMessageData data{dmrId_, destinationId, isGroup, text};
    auto payload = protocol::SerializeTextMessage(data);
    TransmitRaw(static_cast<uint16_t>(MessageType::MESSAGE_TEXT), MessageFlag::NONE, payload);
}

void Client::SendLocationReport(const std::string& nmeaSentence) {
    if (state_ != State::CONNECTED) return;

    protocol::LocationReport data{0, nmeaSentence};
    auto payload = protocol::SerializeLocationReport(data);
    TransmitRaw(static_cast<uint16_t>(MessageType::LOCATION_REPORT), MessageFlag::NONE, payload);
}

// --- Private Helpers ---

void Client::StartReceiveLoop() {
    if (state_ == State::DISCONNECTED || !socket_.is_open()) {
        return;
    }

    socket_.async_receive_from(
        asio::buffer(receiveBuffer_),
        senderEndpoint_,
        [this](const std::error_code& ec, size_t bytesTransferred) {
            HandleReceive(ec, bytesTransferred);
        }
    );
}

void Client::HandleReceive(const std::error_code& ec, size_t bytesTransferred) {
    if (state_ == State::DISCONNECTED) {
        return;
    }

    if (ec) {
        if (ec != asio::error::operation_aborted) {
            std::cerr << "Socket read error: " << ec.message() << std::endl;
            lastError_ = ClientError::SOCKET_IO;
            if (onError) {
                onError(ClientError::SOCKET_IO, "Socket read error: " + ec.message());
            }
            Disconnect();
        }
        return;
    }

    if (bytesTransferred > 0) {
        // Validate packet came from server
        if (senderEndpoint_ == serverEndpoint_) {
            ProcessPacket(std::span<const uint8_t>(receiveBuffer_, bytesTransferred));
        } else {
            std::cout << "[Client] Ignored packet from " << senderEndpoint_ 
                      << " (expected: " << serverEndpoint_ << ")\n";
        }
    }

    StartReceiveLoop();
}

void Client::TransmitRaw(uint16_t type, uint16_t flag, std::span<const uint8_t> payload) {
    std::lock_guard<std::mutex> lock(writeMutex_);
    if (!socket_.is_open()) {
        return;
    }

    // Determine correct counter (index 1 for REAL_TIME_1 flag, index 0 for routine messages)
    bool isRealTime = (flag & MessageFlag::REAL_TIME_1) != 0;
    uint32_t seqNum = isRealTime ? realTimeSeq_++ : routineSeq_++;

    protocol::Header h{static_cast<MessageType>(type), flag, seqNum, static_cast<uint16_t>(payload.size())};
    auto headerBytes = protocol::SerializeHeader(h);

    std::vector<uint8_t> packet;
    packet.reserve(headerBytes.size() + payload.size());
    packet.insert(packet.end(), headerBytes.begin(), headerBytes.end());
    packet.insert(packet.end(), payload.begin(), payload.end());

    std::error_code ec;
    std::cout << "[Client] Transmitting packet type: " << type << ", length: " << payload.size() 
              << " to server " << serverEndpoint_ << "\n";
    socket_.send_to(asio::buffer(packet), serverEndpoint_, 0, ec);
    if (ec) {
        std::cerr << "Socket write error: " << ec.message() << std::endl;
        lastError_ = ClientError::SOCKET_IO;
        if (onError) {
            onError(ClientError::SOCKET_IO, "Socket write error: " + ec.message());
        }
    }
}

void Client::StartKeepAliveTimer() {
    if (state_ != State::CONNECTED) {
        return;
    }

    keepAliveTimer_.expires_after(std::chrono::seconds(KEEP_ALIVE_INTERVAL_SEC));
    keepAliveTimer_.async_wait([this](const std::error_code& ec) {
        if (ec == asio::error::operation_aborted || state_ != State::CONNECTED) {
            return;
        }
        SendKeepAlive();
        StartKeepAliveTimer();
    });
}

void Client::StopKeepAliveTimer() {
    std::error_code ec;
    keepAliveTimer_.cancel(ec);
}

void Client::SendKeepAlive() {
    protocol::VersionData v{dmrId_, ServiceType::OPEN_TERMINAL, description_};
    auto payload = protocol::SerializeVersionData(v);
    TransmitRaw(static_cast<uint16_t>(MessageType::KEEP_ALIVE), MessageFlag::NONE, payload);
}

void Client::ProcessPacket(std::span<const uint8_t> packet) {
    auto headerOpt = protocol::DeserializeHeader(packet);
    if (!headerOpt.has_value()) {
        return;
    }

    if (packet.size() < 18u + headerOpt->length) {
        return; // Malformed packet data
    }

    std::span<const uint8_t> payload = packet.subspan(18, headerOpt->length);

    switch (headerOpt->type) {
        case MessageType::CHALLENGE: {
            if (state_ == State::CONNECTING) {
                state_ = State::AUTHENTICATING;
                
                // Build salt + password buffer
                std::vector<uint8_t> hashBuf(payload.begin(), payload.end());
                hashBuf.insert(hashBuf.end(), password_.begin(), password_.end());
                
                // Compute SHA256 digest
                auto digest = crypto::Sha256(hashBuf);
                
                // Respond with authentication
                TransmitRaw(static_cast<uint16_t>(MessageType::AUTHENTICATION), MessageFlag::NONE, digest);
            }
            break;
        }

        case MessageType::KEEP_ALIVE: {
            // Keep alive message with zero length payload is connection success
            if (headerOpt->length == 0) {
                if (state_ == State::AUTHENTICATING) {
                    state_ = State::CONNECTED;
                    
                    std::error_code ec;
                    connectTimeoutTimer_.cancel(ec);
                    
                    StartKeepAliveTimer();
                    
                    // Unblock Connect() call
                    {
                        std::lock_guard<std::mutex> lock(connectionMutex_);
                        connectionCv_.notify_all();
                    }

                    if (onConnected) {
                        onConnected();
                    }
                }
            }
            break;
        }

        case MessageType::REPORT: {
            if (onDebugReport) {
                onDebugReport(std::string(payload.begin(), payload.end()));
            }
            break;
        }

        case MessageType::MESSAGE_TEXT: {
            auto textOpt = protocol::DeserializeTextMessage(payload);
            if (textOpt.has_value() && onTextMessage) {
                onTextMessage({textOpt->sourceId, textOpt->destinationId, textOpt->isGroup, textOpt->text});
            }
            break;
        }

        case MessageType::MESSAGE_STATUS: {
            auto statusOpt = protocol::DeserializeTextMessageStatus(payload);
            if (statusOpt.has_value() && onTextMessageStatus) {
                onTextMessageStatus(statusOpt->sourceId, statusOpt->destinationId, statusOpt->status);
            }
            break;
        }

        case MessageType::LOCATION_REQUEST: {
            auto reqOpt = protocol::DeserializeLocationRequest(payload);
            if (reqOpt.has_value() && onLocationRequest) {
                onLocationRequest(reqOpt->type, reqOpt->interval);
            }
            break;
        }

        case MessageType::LOCATION_REPORT: {
            auto repOpt = protocol::DeserializeLocationReport(payload);
            if (repOpt.has_value() && onLocationReport) {
                onLocationReport({repOpt->format, repOpt->nmea});
            }
            break;
        }

        default: {
            uint16_t typeRaw = static_cast<uint16_t>(headerOpt->type);
            
            // Check for Voice Header (DMR_DATA_BASE + 1)
            if (typeRaw == static_cast<uint16_t>(MessageType::DMR_DATA_BASE) + 1) {
                if (payload.size() >= 12 && onVoiceHeader) {
                    // Extract IDs from 12-byte LC structure
                    uint32_t destId = (payload[3] << 16) | (payload[4] << 8) | payload[5];
                    uint32_t srcId = (payload[6] << 16) | (payload[7] << 8) | payload[8];
                    bool isGroup = ((payload[0] & 0x3F) != 0x03);
                    onVoiceHeader(srcId, destId, isGroup);
                }
            }
            // Check for Voice Terminator (DMR_DATA_BASE + 2)
            else if (typeRaw == static_cast<uint16_t>(MessageType::DMR_DATA_BASE) + 2) {
                if (onVoiceTerminator) {
                    onVoiceTerminator();
                }
            }
            // Check for Audio Frame
            else if (headerOpt->type == MessageType::DMR_AUDIO_FRAME) {
                if (onAudioFrame) {
                    AudioFrame frame;
                    frame.sequenceNumber = headerOpt->number;
                    frame.ambeData.assign(payload.begin(), payload.end());
                    onAudioFrame(frame);
                }
            }
            break;
        }
    }
}

} // namespace opendtp
