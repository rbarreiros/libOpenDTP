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

#pragma once

#include <string>
#include <memory>
#include <functional>
#include <span>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>
#include "Common.hpp"
#include "Types.hpp"

#include <asio/ip/udp.hpp>
#include <asio/steady_timer.hpp>

/**
 * @file Client.hpp
 * @brief Thread-safe client engine for the Open DMR Terminal Protocol.
 */

namespace opendtp {

/**
 * @class Client
 * @brief Thread-safe network client implementing asynchronous connection, keep-alive, SMS, location, and audio streaming over the Open DMR Terminal Protocol.
 *
 * This client runs network processing in a background worker thread using Standalone ASIO.
 * It manages UDP sockets, challenge-response authentication, dynamic talkgroup subscriptions,
 * and exposes callback hooks to handle incoming events.
 * 
 * ### Quick Start Example
 * @code
 * #include <opendtp/Client.hpp>
 * #include <iostream>
 * 
 * int main() {
 *     // Create client with DMR ID 268999
 *     opendtp::Client client(268999);
 * 
 *     // Setup success callback
 *     client.onConnected = []() {
 *         std::cout << "Connected successfully!\n";
 *     };
 * 
 *     // Setup error handler
 *     client.onError = [](opendtp::ClientError err, const std::string& msg) {
 *         std::cerr << "Error: " << msg << "\n";
 *     };
 * 
 *     // Connect asynchronously
 *     client.Connect("master.brandmeister.es", 54006, "MyPassword");
 * 
 *     // Wait or execute program loop
 *     // ...
 *     client.Disconnect();
 * }
 * @endcode
 */
class Client {
public:
    /**
     * @brief Constructs a new Client instance.
     * @param dmrId The user's registered DMR subscriber ID.
     * @param description Software application description name/version to transmit during Keep-Alive.
     */
    Client(uint32_t dmrId, std::string description = "libOpenDTP Client 1.0.0");
    
    /**
     * @brief Destructor. Disconnects cleanly, joins threads, and releases network sockets.
     */
    ~Client();

    // Prevent copy or assignment to avoid duplicate socket ownership
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    Client(Client&&) = delete;
    Client& operator=(Client&&) = delete;

    // --- Connection API ---
    
    /**
     * @brief Initiates an asynchronous connection and challenge-response login sequence.
     * @details Resolves the host, opens a UDP socket, starts background network thread, and sends
     *          a keep-alive identity trigger packet. Returns immediately. Outcome is reported
     *          via onConnected, onDisconnected, or onError.
     * @param host Target server address (domain or IP).
     * @param port Target server UDP port.
     * @param password Connection password/key.
     * @param timeoutMs Timeout in milliseconds for resolution and authentication.
     * @return ClientError::SUCCESS if the resolution was dispatched successfully, or an error code on startup failure.
     */
    ClientError Connect(const std::string& host, uint16_t port, const std::string& password, uint32_t timeoutMs = 5000);

    /**
     * @brief Synchronously connects to the server.
     * @details Blocks the calling thread until the connection succeeds or fails.
     * @param host Target server address (domain or IP).
     * @param port Target server UDP port.
     * @param password Connection password/key.
     * @param timeoutMs Connection timeout in milliseconds.
     * @return ClientError::SUCCESS on connection success, or error code on failure.
     */
    ClientError ConnectSync(const std::string& host, uint16_t port, const std::string& password, uint32_t timeoutMs = 5000);
    
    /**
     * @brief Gracefully disconnects from the server, sends CLOSE notice, and stops background threads.
     */
    void Disconnect();
    
    /**
     * @brief Checks if the client is currently connected and authenticated.
     * @return True if connected and authenticated, false otherwise.
     */
    bool IsConnected() const noexcept { return state_ == State::CONNECTED; }

    // --- Subscription Management ---
    
    /**
     * @brief Subscribes to receive voice traffic for a talkgroup.
     * @param talkgroup The talkgroup target ID.
     */
    void Subscribe(uint32_t talkgroup);
    
    /**
     * @brief Unsubscribes from receiving voice traffic for a talkgroup.
     * @param talkgroup The talkgroup target ID.
     */
    void Unsubscribe(uint32_t talkgroup);

    // --- Call Transmission ---
    
    /**
     * @brief Starts an outbound voice call transmission.
     * @details Sends the DMR Voice Header LC PDU 3 times to ensure delivery.
     * @param sourceId The DMR ID of the transmitting subscriber.
     * @param destinationId The target receiving DMR ID (private or group).
     * @param isGroup True if transmitting to a group/talkgroup, false for private call.
     */
    void StartVoiceCall(uint32_t sourceId, uint32_t destinationId, bool isGroup);
    
    /**
     * @brief Transmits a raw audio payload.
     * @details Encapsulates the AMBE frame data using the real-time sequence counter and real-time flags.
     * @param ambeData The raw voice payload (typically 27 bytes triple AMBE).
     */
    void SendAudioFrame(std::span<const uint8_t> ambeData);
    
    /**
     * @brief Terminates the current outbound voice call transmission, sending the DMR Voice Terminator.
     */
    void EndVoiceCall();

    // --- Messaging and Location Services ---
    
    /**
     * @brief Sends a text SMS message.
     * @param destinationId The recipient DMR ID.
     * @param isGroup True for a group message, false for private message.
     * @param text The UTF-8 encoded text message.
     */
    void SendTextMessage(uint32_t destinationId, bool isGroup, const std::string& text);
    
    /**
     * @brief Sends a GPS location report.
     * @param nmeaSentence The raw NMEA ASCII sentence (e.g. "$GPRMC...").
     */
    void SendLocationReport(const std::string& nmeaSentence);

    // --- Callback Hooks ---
    
    /**
     * @brief Hook invoked when successfully connected and authenticated.
     */
    std::function<void()> onConnected;
    
    /**
     * @brief Hook invoked when disconnected (manually or due to server-initiated close).
     * @param reason The disconnection reason error code.
     */
    std::function<void(ClientError reason)> onDisconnected;

    /**
     * @brief Hook invoked when a network, DNS, timeout, or socket I/O error occurs.
     * @param error The error classification code.
     * @param message A descriptive error message string.
     */
    std::function<void(ClientError error, const std::string& message)> onError;
    
    /**
     * @brief Hook invoked when a debug/report log message is received from the server.
     * @param report The ASCII debug log string.
     */
    std::function<void(const std::string& report)> onDebugReport;
    
    /**
     * @brief Hook invoked when an incoming text message is received.
     * @param message The received TextMessage details.
     */
    std::function<void(const TextMessage& message)> onTextMessage;
    
    /**
     * @brief Hook invoked when a text message delivery status report is received.
     * @param srcId The source DMR ID.
     * @param destId The destination DMR ID.
     * @param status The delivery status byte code.
     */
    std::function<void(uint32_t srcId, uint32_t destId, uint8_t status)> onTextMessageStatus;
    
    /**
     * @brief Hook invoked when a location request (periodical or one-shot) is received from the server.
     * @param requestType The trigger type ID.
     * @param interval The update interval in seconds.
     */
    std::function<void(uint32_t requestType, uint32_t interval)> onLocationRequest;
    
    /**
     * @brief Hook invoked when an incoming location report is received.
     * @param report The LocationReport details.
     */
    std::function<void(const LocationReport& report)> onLocationReport;
    
    /**
     * @brief Hook invoked when an incoming voice call header (LC PDU) is received.
     * @param sourceId The DMR ID of the caller.
     * @param destinationId The target destination ID.
     * @param isGroup True if the call is group, false if private.
     */
    std::function<void(uint32_t sourceId, uint32_t destinationId, bool isGroup)> onVoiceHeader;
    
    /**
     * @brief Hook invoked when an incoming audio frame is received.
     * @param frame The AudioFrame sequence and AMBE payload.
     */
    std::function<void(const AudioFrame& frame)> onAudioFrame;
    
    /**
     * @brief Hook invoked when the incoming voice call terminator is received.
     */
    std::function<void()> onVoiceTerminator;

private:
    enum class State {
        DISCONNECTED,
        CONNECTING,
        AUTHENTICATING,
        CONNECTED
    };

    // --- Private Helper Methods ---
    void StartReceiveLoop();
    void HandleReceive(const std::error_code& ec, size_t bytesTransferred);
    void TransmitRaw(uint16_t type, uint16_t flag, std::span<const uint8_t> payload);
    void StartKeepAliveTimer();
    void StopKeepAliveTimer();
    void SendKeepAlive();
    void ProcessPacket(std::span<const uint8_t> packet);
    
    // State indicators
    uint32_t dmrId_;
    std::string description_;
    std::string password_;
    std::atomic<State> state_{State::DISCONNECTED};
    
    // Thread safety
    mutable std::mutex connectionMutex_;
    mutable std::mutex writeMutex_;
    std::condition_variable connectionCv_;
    ClientError lastError_{ClientError::SUCCESS};

    // Separate sequence counters for routine vs real-time traffic
    uint32_t routineSeq_{0};
    uint32_t realTimeSeq_{0};

    // Outbound call state
    std::atomic<bool> inCall_{false};

    // ASIO networking resources
    asio::io_context ioContext_;
    asio::ip::udp::socket socket_;
    asio::ip::udp::endpoint serverEndpoint_;
    asio::steady_timer keepAliveTimer_;
    asio::steady_timer connectTimeoutTimer_;
    asio::ip::udp::resolver resolver_;
    
    // Worker threads
    std::thread ioThread_;
    
    // Inline buffer for incoming network data
    alignas(8) uint8_t receiveBuffer_[1024];
    asio::ip::udp::endpoint senderEndpoint_;
};

} // namespace opendtp
