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
#include <fstream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <csignal>
#include <atomic>

std::atomic<bool> running{true};

void SignalHandler(int signalNum) {
    if (signalNum == SIGINT) {
        std::cout << "\n[Signal] Interrupt signal (SIGINT) received. Shutting down...\n";
        running = false;
    }
}

void PrintUsage(const char* progName) {
    std::cout << "Usage:\n"
              << "  " << progName << " <client-number> <client-password> <server-address> <server-port> <group-id> <output-file>\n"
              << "\n"
              << "Example:\n"
              << "  " << progName << " 268999 MyPassword master.brandmeister.es 54006 91 output.amb\n"
              << "Note: Records raw triple-AMBE frames (27 bytes each chunk) into the output file.\n";
}

int main(int argc, char* argv[]) {
    std::cout << "\nlibOpenDTP record_audio utility (Record AMBE frames to file)\n";
    std::cout << "===========================================================\n\n";

    if (argc < 7) {
        PrintUsage(argv[0]);
        return 1;
    }

    uint32_t clientNumber = std::stoul(argv[1]);
    std::string password = argv[2];
    std::string serverAddress = argv[3];
    uint16_t serverPort = static_cast<uint16_t>(std::stoul(argv[4]));
    uint32_t groupId = std::stoul(argv[5]);
    std::string outputFile = argv[6];

    // Open binary output file
    std::ofstream out(outputFile, std::ios::binary);
    if (!out.is_open()) {
        std::cerr << "Error: Failed to open output file '" << outputFile << "' for writing.\n";
        return 1;
    }

    // Register signal handler for clean shutdown
    std::signal(SIGINT, SignalHandler);

    // Create client
    opendtp::Client client(clientNumber, "libOpenDTP record_audio v1.0");

    // Register client callbacks
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

    client.onVoiceHeader = [](uint32_t srcId, uint32_t destId, bool isGroup) {
        std::cout << "\n[Call Start] Incoming voice call from " << srcId << " to " 
                  << destId << (isGroup ? " (Group)" : " (Private)") << "\nRecording: " << std::flush;
    };

    client.onAudioFrame = [&](const opendtp::AudioFrame& frame) {
        if (out.is_open() && !frame.ambeData.empty()) {
            out.write(reinterpret_cast<const char*>(frame.ambeData.data()), frame.ambeData.size());
            out.flush();
            std::cout << "." << std::flush;
        }
    };

    client.onVoiceTerminator = []() {
        std::cout << "\n[Call End] Call terminated.\n";
    };

    std::cout << "[Client] Connecting to " << serverAddress << ":" << serverPort << "...\n";
    auto result = client.ConnectSync(serverAddress, serverPort, password, 5000);
    if (result != opendtp::ClientError::SUCCESS) {
        std::cerr << "[Client] Failed to connect: " << static_cast<int>(result) << "\n";
        return 1;
    }

    // Subscribe to the destination group ID
    std::cout << "[Client] Subscribing to group " << groupId << "...\n";
    client.Subscribe(groupId);

    std::cout << "[Record] Monitoring group " << groupId << ". Press Ctrl+C to stop recording.\n\n";

    // Monitor loop
    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "[Record] Shutting down connection and flushing files...\n";
    
    // Clean up
    client.Unsubscribe(groupId);
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // allow unsubscribe packet to leave
    client.Disconnect();
    out.close();

    std::cout << "Done. Audio saved to '" << outputFile << "'.\n";
    return 0;
}
