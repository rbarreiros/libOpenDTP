# libOpenDTP

[![CI](https://github.com/rbarreiros/libOpenDTP/actions/workflows/ci.yml/badge.svg)](https://github.com/rbarreiros/libOpenDTP/actions/workflows/ci.yml)
[![codecov](https://codecov.io/gh/rbarreiros/libOpenDTP/graph/badge.svg)](https://codecov.io/gh/rbarreiros/libOpenDTP)

`libOpenDTP` is a modern C++20, cross-platform, library implementing the **Open DMR Terminal Protocol** (ODMRTP) for the BrandMeister DMR Network.

The library provides clean, thread-safe asynchronous UDP networking and simple, event-driven callbacks based on ASIO.

---

## Building and Testing

### Prerequisites
- A C++20 compliant compiler (GCC 13+, Clang 16+, or MSVC 2022+).
- CMake 3.15 or newer.
- An internet connection (on first build) for CMake to fetch standalone `asio` and `googletest`.

### Compiling
Configure and compile the library, tests, and example applications:

```bash
# Create and enter the build directory
mkdir -p build && cd build

# Configure CMake
cmake -DBUILD_TESTING=ON -DBUILD_EXAMPLES=ON ..

# Build everything
make -j$(nproc)
```

### Running the Test Suite
`libOpenDTP` comes with a comprehensive GoogleTest suite that spins up a local UDP mock server to test network handshakes, credentials validation, keep-alive timers, SMS message relays, and voice transmissions:

```bash
# Execute the test binary directly from the build folder
./tests/opendtp_tests
```

### Running the Examples

**1. Record Audio (`record_audio`):**
Monitors a specified talkgroup and dumps incoming AMBE audio frames directly to a file:
```bash
./examples/record_audio <client-number> <client-password> <server-address> <server-port> <group-id> <output-file.amb>
```

**2. Play Audio (`play_audio`):**
Streams a pre-recorded AMBE audio file back to the BrandMeister server:
```bash
cat <output-file.amb> | ./examples/play_audio <client-number> <client-password> <server-address> <server-port> <source-id> <group-id>
```

---

## Installation and Packaging

### Standard Installation
Install the compiled library, public headers, and executable targets to the system path:

```bash
sudo make install
```

### Packaging (CPack)
You can compile native install packages for your current operating system automatically using CMake's CPack helper:

```bash
# Generate DEB (Debian/Ubuntu), ZIP, and TGZ installers
make package
```
This generates the following files in the `build/` directory:
- Linux / Debian: `libopendtp-<version>-Linux.deb`
- Windows / generic: `libopendtp-<version>-Linux.zip`
- macOS / Apple: `.pkg` / `.dmg` installer files.

---

## Basic Usage

The following example shows how to instantiate the client, register callbacks, authenticate with a BrandMeister server, and subscribe to a talkgroup to receive voice calls:

```cpp
#include <opendtp/Client.hpp>
#include <iostream>
#include <thread>

int main() {
    // 1. Create the client (dmrId, client_name_version)
    opendtp::Client client(268999, "MyDMRTerminal v1.0");

    // 2. Register Callback Hooks
    client.onConnected = []() {
        std::cout << "Successfully authenticated with BrandMeister server!\n";
    };

    client.onDisconnected = [](opendtp::ClientError reason) {
        std::cout << "Disconnected! Error Code: " << static_cast<int>(reason) << "\n";
    };

    client.onTextMessage = [](const opendtp::TextMessage& msg) {
        std::cout << "Incoming SMS from " << msg.sourceId << ": " << msg.text << "\n";
    };

    client.onVoiceHeader = [](uint32_t srcId, uint32_t destId, bool isGroup) {
        std::cout << "Voice Call started from " << srcId << " to " 
                  << destId << (isGroup ? " (Group)" : " (Private)") << "\n";
    };

    client.onAudioFrame = [](const opendtp::AudioFrame& frame) {
        // Handle incoming triple-AMBE frame bytes (frame.ambeData is std::vector<uint8_t> of size 27)
        std::cout << "Received audio frame #" << frame.sequenceNumber << "\n";
    };

    client.onVoiceTerminator = []() {
        std::cout << "Voice Call ended.\n";
    };

    // 3. Connect (Synchronous challenge-response, blocks until connected or fails)
    std::cout << "Connecting to server...\n";
    auto err = client.Connect("master.brandmeister.network", 54006, "my_bm_password", 5000);
    if (err != opendtp::ClientError::SUCCESS) {
        std::cerr << "Connect failed: " << static_cast<int>(err) << "\n";
        return 1;
    }

    // 4. Manage Talkgroup Subscriptions
    client.Subscribe(91); // Subscribe to TG 91 (Worldwide)

    // Keep client running in the foreground (callbacks trigger on the Asio worker thread)
    std::this_thread::sleep_for(std::chrono::seconds(60));

    // 5. Send a Text Message
    client.SendTextMessage(91, true, "Greetings from libOpenDTP!");

    // 6. Disconnect (gracefully notifies server and stops worker threads)
    client.Disconnect();
    return 0;
}
```

## License

This project is licensed under the GNU Lesser General Public License version 3 (LGPL-3.0) - see the [COPYING](COPYING) and [COPYING.LESSER](COPYING.LESSER) files for details.
