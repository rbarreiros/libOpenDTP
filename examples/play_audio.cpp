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
#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>

void PrintUsage(const char* progName) {
    std::cout << "Usage:\n"
              << "  " << progName << " <client-number> <client-password> <server-address> <server-port> <source-id> <group-id>\n"
              << "\n"
              << "Example:\n"
              << "  cat audio.amb | " << progName << " 268999 MyPassword master.brandmeister.es 54006 268999 91\n"
              << "Note: Streams triple-AMBE frames (27 bytes each chunk) from standard input.\n";
}

int main(int argc, char* argv[]) {
    std::cout << "\nlibOpenDTP play_audio demo (Modern C++ conversion of DigestPlay)\n";
    std::cout << "===============================================================\n\n";

    if (argc < 7) {
        PrintUsage(argv[0]);
        return 1;
    }

    uint32_t clientNumber = std::stoul(argv[1]);
    std::string password = argv[2];
    std::string serverAddress = argv[3];
    uint16_t serverPort = static_cast<uint16_t>(std::stoul(argv[4]));
    uint32_t sourceId = std::stoul(argv[5]);
    uint32_t groupId = std::stoul(argv[6]);

    // Create client
    opendtp::Client client(clientNumber, "libOpenDTP play_audio v1.0");

    // Set callback notifications
    client.onConnected = []() {
        std::cout << "[Client] Connected and authenticated successfully!\n";
    };

    client.onDisconnected = [](opendtp::ClientError reason) {
        std::cout << "[Client] Disconnected. Reason: " << static_cast<int>(reason) << "\n";
    };

    client.onError = [](opendtp::ClientError error, const std::string& message) {
        std::cerr << "[Client Error] Code: " << static_cast<int>(error) << ", Message: " << message << "\n";
    };

    client.onDebugReport = [](const std::string& report) {
        std::cout << "[Server Log] " << report << "\n";
    };

    client.onTextMessage = [](const opendtp::TextMessage& msg) {
        std::cout << "[SMS] Received from " << msg.sourceId << " to " 
                  << msg.destinationId << (msg.isGroup ? " (Group)" : " (Private)")
                  << ": " << msg.text << "\n";
    };

    std::cout << "[Client] Connecting to " << serverAddress << ":" << serverPort << "...\n";
    auto result = client.ConnectSync(serverAddress, serverPort, password, 5000);
    if (result != opendtp::ClientError::SUCCESS) {
        std::cerr << "[Client] Failed to connect: " << static_cast<int>(result) << "\n";
        return 1;
    }

    // Subscribe to the destination group
    std::cout << "[Client] Subscribing to group " << groupId << "...\n";
    client.Subscribe(groupId);

    // Wait a brief moment for subscription propagation
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    std::cout << "[Client] Starting voice transmission...\n";
    client.StartVoiceCall(sourceId, groupId, true);

    // Read triple AMBE frames (27 bytes) from stdin and stream them every 60ms
    std::vector<uint8_t> frameBuffer(27);
    size_t count = 0;
    
    // 60 milliseconds frame duration
    auto interval = std::chrono::milliseconds(60);
    auto nextFrameTime = std::chrono::steady_clock::now();

    while (std::cin.read(reinterpret_cast<char*>(frameBuffer.data()), 27)) {
        // Sleep until the exact time of the next frame to prevent buffer issues or jitter
        std::this_thread::sleep_until(nextFrameTime);
        nextFrameTime += interval;

        // Transmit audio frame to server
        client.SendAudioFrame(frameBuffer);
        
        if (count % 20 == 0) {
            std::cout << "[Client] Transmitted " << count << " frames...\r" << std::flush;
        }
        count++;
    }

    std::cout << "\n[Client] Input data stream ended. Terminating voice transmission...\n";
    client.EndVoiceCall();

    std::cout << "[Client] Disconnecting...\n";
    client.Disconnect();
    
    std::cout << "Done.\n";
    return 0;
}
