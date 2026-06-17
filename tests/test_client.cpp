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

#include <gtest/gtest.h>
#include "opendtp/Client.hpp"
#include "opendtp/Protocol.hpp"
#include "Sha256.hpp"

#include <asio.hpp>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>

namespace {

struct MockServer {
    asio::io_context io;
    asio::ip::udp::socket socket;
    asio::ip::udp::endpoint clientEp;
    std::thread runThread;
    std::string expectedPassword = "supersecretpassword";
    std::string salt = "saltsaltsalt";
    uint32_t receivedVersionDmrId = 0;
    std::vector<std::vector<uint8_t>> receivedPackets;
    std::mutex mtx;
    uint8_t buf[1024];

    MockServer() : socket(io, asio::ip::udp::endpoint(asio::ip::address_v4::loopback(), 0)) {
        StartReceive();
        runThread = std::thread([this]() {
            try {
                io.run();
            } catch (...) {}
        });
    }

    ~MockServer() {
        io.stop();
        std::error_code ec;
        socket.close(ec);
        if (runThread.joinable()) {
            runThread.join();
        }
    }

    void StartReceive() {
        socket.async_receive_from(
            asio::buffer(buf),
            clientEp,
            [this](const std::error_code& ec, size_t bytes) {
                if (!ec && bytes > 0) {
                    std::cout << "[MockServer] Received " << bytes << " bytes from client\n";
                    std::vector<uint8_t> pkt(buf, buf + bytes);
                    {
                        std::lock_guard<std::mutex> lock(mtx);
                        receivedPackets.push_back(pkt);
                    }
                    ProcessIncoming(pkt);
                    StartReceive();
                }
            }
        );
    }

    uint16_t GetPort() const {
        return socket.local_endpoint().port();
    }

    void ProcessIncoming(const std::vector<uint8_t>& pkt) {
        auto headerOpt = opendtp::protocol::DeserializeHeader(pkt);
        if (!headerOpt) {
            std::cout << "[MockServer] Failed to deserialize header!\n";
            return;
        }
        std::cout << "[MockServer] Processing packet, type: " << static_cast<int>(headerOpt->type) 
                  << ", length: " << headerOpt->length << "\n";
        std::span<const uint8_t> payload(pkt.data() + 18, headerOpt->length);

        if (headerOpt->type == opendtp::MessageType::KEEP_ALIVE && headerOpt->length > 0) {
            auto vOpt = opendtp::protocol::DeserializeVersionData(payload);
            if (vOpt) {
                receivedVersionDmrId = vOpt->dmrId;
                std::cout << "[MockServer] Received VersionData, dmrId: " << vOpt->dmrId 
                          << ", description: " << vOpt->description << ". Sending Challenge.\n";
                // Send challenge salt
                std::vector<uint8_t> challenge(salt.begin(), salt.end());
                SendPacket(opendtp::MessageType::CHALLENGE, 0, challenge);
            } else {
                std::cout << "[MockServer] Failed to deserialize VersionData!\n";
            }
        }
        else if (headerOpt->type == opendtp::MessageType::AUTHENTICATION) {
            std::cout << "[MockServer] Received Authentication.\n";
            // Check hash
            std::vector<uint8_t> expectedBuf(salt.begin(), salt.end());
            expectedBuf.insert(expectedBuf.end(), expectedPassword.begin(), expectedPassword.end());
            auto expectedDigest = opendtp::crypto::Sha256(expectedBuf);

            bool matches = (payload.size() == 32);
            if (matches) {
                for (size_t i = 0; i < 32; ++i) {
                    if (payload[i] != expectedDigest[i]) {
                        matches = false;
                        break;
                    }
                }
            }

            if (matches) {
                std::cout << "[MockServer] Authentication successful! Sending empty KeepAlive.\n";
                // Success - Empty Keep Alive
                SendPacket(opendtp::MessageType::KEEP_ALIVE, 0, {});
            } else {
                std::cout << "[MockServer] Authentication failed! Matches: " << matches << "\n";
            }
        }
    }

    void SendPacket(opendtp::MessageType type, uint16_t flags, const std::vector<uint8_t>& payload) {
        std::cout << "[MockServer] Sending packet, type: " << static_cast<int>(type) 
                  << ", payload size: " << payload.size() << " to client " << clientEp << "\n";
        opendtp::protocol::Header h{type, flags, 0, static_cast<uint16_t>(payload.size())};
        auto hBytes = opendtp::protocol::SerializeHeader(h);
        std::vector<uint8_t> pkt = hBytes;
        pkt.insert(pkt.end(), payload.begin(), payload.end());
        std::error_code ec;
        socket.send_to(asio::buffer(pkt), clientEp, 0, ec);
        if (ec) {
            std::cout << "[MockServer] send_to error: " << ec.message() << "\n";
        }
    }
};

} // namespace

// Test connection handshake success
TEST(ClientNetworkTest, HandshakeSuccess) {
    MockServer server;
    opendtp::Client client(268999, "UnitTestTerminal v1");

    EXPECT_FALSE(client.IsConnected());

    auto result = client.ConnectSync("127.0.0.1", server.GetPort(), "supersecretpassword", 1000);
    EXPECT_EQ(result, opendtp::ClientError::SUCCESS);
    EXPECT_TRUE(client.IsConnected());

    client.Disconnect();
    EXPECT_FALSE(client.IsConnected());
}

// Test connection handshake failure (invalid password)
TEST(ClientNetworkTest, HandshakeInvalidPassword) {
    MockServer server;
    opendtp::Client client(268999, "UnitTestTerminal v1");

    auto result = client.ConnectSync("127.0.0.1", server.GetPort(), "wrongpassword", 1000);
    EXPECT_NE(result, opendtp::ClientError::SUCCESS);
    EXPECT_FALSE(client.IsConnected());
}

// Test message sending, voice streaming, and receiving notifications
TEST(ClientNetworkTest, MessageAndVoiceTraffic) {
    MockServer server;
    opendtp::Client client(268999, "UnitTestTerminal v1");

    // ConnectSync
    auto result = client.ConnectSync("127.0.0.1", server.GetPort(), "supersecretpassword", 1000);
    ASSERT_EQ(result, opendtp::ClientError::SUCCESS);

    // Subscribe to talkgroup 91
    client.Subscribe(91);
    
    // Start Call
    client.StartVoiceCall(268999, 91, true);
    
    // Send 3 audio frames (triple AMBE, 27 bytes each)
    std::vector<uint8_t> ambe(27, 0xAA);
    client.SendAudioFrame(ambe);
    client.SendAudioFrame(ambe);
    client.SendAudioFrame(ambe);
    
    // End voice call
    client.EndVoiceCall();

    // Send a Text Message
    client.SendTextMessage(91, true, "Hello group!");

    // Allow network buffer processing to settle
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Verify packets received by Mock Server
    std::lock_guard<std::mutex> lock(server.mtx);
    
    bool foundSubscription = false;
    bool foundVoiceHeader = false;
    bool foundAudioFrame = false;
    bool foundVoiceTerminator = false;
    bool foundTextMessage = false;

    for (const auto& pkt : server.receivedPackets) {
        auto headerOpt = opendtp::protocol::DeserializeHeader(pkt);
        if (!headerOpt) continue;

        if (headerOpt->type == opendtp::MessageType::SUBSCRIPTION) {
            foundSubscription = true;
        }
        else if (static_cast<uint16_t>(headerOpt->type) == static_cast<uint16_t>(opendtp::MessageType::DMR_DATA_BASE) + 1) {
            foundVoiceHeader = true;
        }
        else if (headerOpt->type == opendtp::MessageType::DMR_AUDIO_FRAME) {
            foundAudioFrame = true;
        }
        else if (static_cast<uint16_t>(headerOpt->type) == static_cast<uint16_t>(opendtp::MessageType::DMR_DATA_BASE) + 2) {
            foundVoiceTerminator = true;
        }
        else if (headerOpt->type == opendtp::MessageType::MESSAGE_TEXT) {
            foundTextMessage = true;
        }
    }

    EXPECT_TRUE(foundSubscription);
    EXPECT_TRUE(foundVoiceHeader);
    EXPECT_TRUE(foundAudioFrame);
    EXPECT_TRUE(foundVoiceTerminator);
    EXPECT_TRUE(foundTextMessage);

    client.Disconnect();
}

// Test receiving message from server to client callback
TEST(ClientNetworkTest, ServerToClientTextMessage) {
    MockServer server;
    opendtp::Client client(268999, "UnitTestTerminal v1");

    std::atomic<bool> textReceived{false};
    std::string receivedText = "";
    uint32_t receivedSourceId = 0;

    client.onTextMessage = [&](const opendtp::TextMessage& msg) {
        receivedSourceId = msg.sourceId;
        receivedText = msg.text;
        textReceived = true;
    };

    auto result = client.ConnectSync("127.0.0.1", server.GetPort(), "supersecretpassword", 1000);
    ASSERT_EQ(result, opendtp::ClientError::SUCCESS);

    // Mock server sends an incoming text message back to client
    opendtp::protocol::TextMessageData textData{12345, 268999, false, "Hello from mock server!"};
    auto serializedText = opendtp::protocol::SerializeTextMessage(textData);
    server.SendPacket(opendtp::MessageType::MESSAGE_TEXT, 0, serializedText);

    // Wait for client to receive and process
    for (int i = 0; i < 50; ++i) {
        if (textReceived) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_TRUE(textReceived);
    EXPECT_EQ(receivedSourceId, 12345);
    EXPECT_EQ(receivedText, "Hello from mock server!");

    client.Disconnect();
}

// Test asynchronous connection success with callbacks
TEST(ClientNetworkTest, HandshakeSuccessAsync) {
    MockServer server;
    opendtp::Client client(268999, "UnitTestTerminal v1");

    std::atomic<bool> connected{false};
    client.onConnected = [&]() {
        connected = true;
    };

    auto result = client.Connect("127.0.0.1", server.GetPort(), "supersecretpassword", 1000);
    EXPECT_EQ(result, opendtp::ClientError::SUCCESS);
    EXPECT_FALSE(client.IsConnected()); // Connect is async, should not be connected immediately

    // Wait for callback to be triggered
    for (int i = 0; i < 100; ++i) {
        if (connected) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_TRUE(connected);
    EXPECT_TRUE(client.IsConnected());

    client.Disconnect();
}

// Test asynchronous connection timeout
TEST(ClientNetworkTest, HandshakeTimeoutAsync) {
    opendtp::Client client(268999, "UnitTestTerminal v1");

    std::atomic<bool> errorTriggered{false};
    opendtp::ClientError errorReason = opendtp::ClientError::SUCCESS;
    
    client.onError = [&](opendtp::ClientError err, const std::string& /*msg*/) {
        errorReason = err;
        errorTriggered = true;
    };

    // We connect to a port where no mock server responds, with a short timeout
    auto result = client.Connect("127.0.0.1", 54321, "password", 100);
    EXPECT_EQ(result, opendtp::ClientError::SUCCESS);

    // Wait for timeout to fire (100ms timeout)
    for (int i = 0; i < 50; ++i) {
        if (errorTriggered) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_TRUE(errorTriggered);
    EXPECT_EQ(errorReason, opendtp::ClientError::RESPONSE_TIMEOUT);
    EXPECT_FALSE(client.IsConnected());
}

// Test DNS resolution failure callback
TEST(ClientNetworkTest, DnsResolutionFailureAsync) {
    opendtp::Client client(268999, "UnitTestTerminal v1");

    std::atomic<bool> errorTriggered{false};
    opendtp::ClientError errorReason = opendtp::ClientError::SUCCESS;
    
    client.onError = [&](opendtp::ClientError err, const std::string& /*msg*/) {
        errorReason = err;
        errorTriggered = true;
    };

    // Connect to an invalid address
    auto result = client.Connect("invalid.host.name.that.does.not.exist", 54006, "password", 1000);
    EXPECT_EQ(result, opendtp::ClientError::SUCCESS);

    // Wait for resolution to fail
    for (int i = 0; i < 200; ++i) {
        if (errorTriggered) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_TRUE(errorTriggered);
    EXPECT_EQ(errorReason, opendtp::ClientError::DNS_RESOLVE);
    EXPECT_FALSE(client.IsConnected());
}
