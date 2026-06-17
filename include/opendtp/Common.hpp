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

#include <cstdint>
#include <string_view>

/**
 * @file Common.hpp
 * @brief Common protocol constants, message classes, message types, and error codes.
 */

namespace opendtp {

/**
 * @name Protocol Constants
 * @{
 */
constexpr uint32_t KEEP_ALIVE_INTERVAL_SEC = 5; /**< Interval in seconds to transmit Keep-Alive status packets. */
constexpr uint32_t SIGN_LENGTH = 8;             /**< Length in bytes of the protocol magic signature. */
constexpr std::string_view PROTOCOL_SIGN = "REWIND01"; /**< Protocol magic signature string. */
/** @} */

/**
 * @brief Identifies the functional domain/class of a protocol message.
 */
enum class MessageClass : uint16_t {
    REWIND_CONTROL = 0x0000, /**< Connection setup, Keep-Alive, and close. */
    SYSTEM_CONSOLE = 0x0100, /**< Remote console logs and debugging. */
    SERVER_NOTICE  = 0x0200, /**< Notices about system state or errors. */
    DEVICE_DATA    = 0x0800, /**< Interface data of physical repeaters. */
    APPLICATION    = 0x0900, /**< Voice traffic, configurations, and subscriptions. */
    TERMINAL       = 0x0A00, /**< Text messaging and location services. */
    KAIROS_DATA    = DEVICE_DATA + 0x00, /**< Kairos repeater interface data. */
    HYTERA_DATA    = DEVICE_DATA + 0x10, /**< Hytera repeater interface data. */
};

/**
 * @brief Specific command or message type under a MessageClass.
 */
enum class MessageType : uint16_t {
    // Control
    KEEP_ALIVE      = static_cast<uint16_t>(MessageClass::REWIND_CONTROL) + 0, /**< Status/Keep-Alive packet. */
    CLOSE           = static_cast<uint16_t>(MessageClass::REWIND_CONTROL) + 1, /**< Close session request. */
    CHALLENGE       = static_cast<uint16_t>(MessageClass::REWIND_CONTROL) + 2, /**< Authentication challenge from server. */
    AUTHENTICATION  = static_cast<uint16_t>(MessageClass::REWIND_CONTROL) + 3, /**< Authentication response hash from client. */
    REDIRECTION     = static_cast<uint16_t>(MessageClass::REWIND_CONTROL) + 8, /**< Server redirection instruction. */

    // Console
    REPORT          = static_cast<uint16_t>(MessageClass::SYSTEM_CONSOLE) + 0, /**< Debug reports from server. */

    // Notices
    BUSY_NOTICE     = static_cast<uint16_t>(MessageClass::SERVER_NOTICE) + 0,  /**< Channel busy warning. */
    ADDRESS_NOTICE  = static_cast<uint16_t>(MessageClass::SERVER_NOTICE) + 1,  /**< IP address change notification. */
    BINDING_NOTICE  = static_cast<uint16_t>(MessageClass::SERVER_NOTICE) + 2,  /**< Registration conflict/binding notice. */

    // App Control
    CONFIGURATION   = static_cast<uint16_t>(MessageClass::APPLICATION) + 0x00, /**< Stream properties configuration. */
    SUBSCRIPTION    = static_cast<uint16_t>(MessageClass::APPLICATION) + 0x01, /**< Talkgroup voice traffic subscription. */
    CANCELLING      = static_cast<uint16_t>(MessageClass::APPLICATION) + 0x02, /**< Talkgroup subscription cancellation. */
    SESSION_POLL    = static_cast<uint16_t>(MessageClass::APPLICATION) + 0x03, /**< Active voice session query. */

    // DMR Data
    DMR_DATA_BASE   = static_cast<uint16_t>(MessageClass::APPLICATION) + 0x10, /**< Base type for DMR payload. */
    DMR_AUDIO_FRAME = static_cast<uint16_t>(MessageClass::APPLICATION) + 0x20, /**< Raw DMR audio payload (triple AMBE). */
    DMR_EMBEDDED_DATA = static_cast<uint16_t>(MessageClass::APPLICATION) + 0x27, /**< Embedded signaling data. */
    SUPER_HEADER    = static_cast<uint16_t>(MessageClass::APPLICATION) + 0x28, /**< Call participants metadata header. */
    FAILURE_CODE    = static_cast<uint16_t>(MessageClass::APPLICATION) + 0x29, /**< Stream setup failure indication. */

    // Terminal
    TERMINAL_IDLE   = static_cast<uint16_t>(MessageClass::TERMINAL) + 0x00,    /**< Terminal is in idle state. */
    TERMINAL_ATTACH = static_cast<uint16_t>(MessageClass::TERMINAL) + 0x02,    /**< Terminal attachment request. */
    TERMINAL_DETACH = static_cast<uint16_t>(MessageClass::TERMINAL) + 0x03,    /**< Terminal detachment request. */
    TERMINAL_WAKEUP = static_cast<uint16_t>(MessageClass::TERMINAL) + 0x04,    /**< Wakeup ping for remote terminal. */
    MESSAGE_TEXT    = static_cast<uint16_t>(MessageClass::TERMINAL) + 0x10,    /**< Text SMS message payload. */
    MESSAGE_STATUS  = static_cast<uint16_t>(MessageClass::TERMINAL) + 0x11,    /**< SMS delivery status report. */
    LOCATION_REPORT = static_cast<uint16_t>(MessageClass::TERMINAL) + 0x20,    /**< GPS location report. */
    LOCATION_REQUEST = static_cast<uint16_t>(MessageClass::TERMINAL) + 0x21,   /**< Periodic or one-shot location request. */
};

/**
 * @brief Transmission flags indicating stream packaging format and buffer rules.
 */
enum MessageFlag : uint16_t {
    NONE         = 0,         /**< Routine non-realtime message. */
    REAL_TIME_1  = (1 << 0),  /**< Real-time transmission type 1 (e.g. voice/audio frames). */
    REAL_TIME_2  = (1 << 1),  /**< Real-time transmission type 2. */
    BUFFERING    = (1 << 2),  /**< Packet was buffered on the server (buffering playback). */
};

/**
 * @brief Service classification categories.
 */
enum class ServiceRole : uint8_t {
    REPEATER_AGENT = 0x10,    /**< Software interfacing physical repeaters. */
    APPLICATION    = 0x20,    /**< Clients executing messaging/voice software. */
};

/**
 * @brief Concrete service type identified during Keep-Alive.
 */
enum class ServiceType : uint8_t {
    CRONOS_AGENT        = static_cast<uint8_t>(ServiceRole::REPEATER_AGENT) + 0, /**< Cronos gateway agent. */
    TELLUS_AGENT        = static_cast<uint8_t>(ServiceRole::REPEATER_AGENT) + 1, /**< Tellus gateway agent. */
    SIMPLE_APPLICATION  = static_cast<uint8_t>(ServiceRole::APPLICATION) + 0,    /**< Routine system client. */
    OPEN_TERMINAL       = static_cast<uint8_t>(ServiceRole::APPLICATION) + 1,    /**< Custom Open DMR Terminal client. */
};

/**
 * @brief Application configuration options flag.
 */
enum class ConfigurationOption : uint32_t {
    SUPER_HEADER = (1 << 0), /**< SuperHeader metadata frame transmission is enabled. */
    LINEAR_FRAME = (1 << 1), /**< Linear (raw PCM) voice frame format support is enabled. */
};

/**
 * @name Session Call Types
 * @{
 */
constexpr uint32_t SESSION_TYPE_PRIVATE_VOICE = 5; /**< ID indicating a private voice session. */
constexpr uint32_t SESSION_TYPE_GROUP_VOICE   = 7; /**< ID indicating a group voice session. */

constexpr uint32_t SESSION_TYPE_FLAG_GROUP = (1 << 1); /**< Bitmask flag representing group calls. */
/** @} */

/**
 * @name Dynamic Directory Session Tree
 * @{
 */
constexpr uint32_t TREE_SESSION_BY_SOURCE = 8; /**< Directory session sorted by caller source DMR ID. */
constexpr uint32_t TREE_SESSION_BY_TARGET = 9; /**< Directory session sorted by receiver group ID. */
/** @} */

/**
 * @brief Error codes returned by client APIs or reported via callbacks.
 */
enum class ClientError : int32_t {
    SUCCESS            = 0,   /**< Operation completed successfully. */
    SOCKET_IO          = -1,  /**< Network I/O read/write error occurred on the socket. */
    WRONG_ADDRESS      = -2,  /**< Target IP address or port format is invalid. */
    WRONG_DATA         = -3,  /**< Received payload contains invalid formats or signature. */
    DNS_RESOLVE        = -4,  /**< Hostname resolution failed. */
    WRONG_PASSWORD     = -5,  /**< Challenge-response authentication rejected. */
    RESPONSE_TIMEOUT   = -6,  /**< Handshake or keep-alive response from server timed out. */
    ALREADY_CONNECTED  = -7,  /**< Client is already connected or currently connecting. */
    NOT_CONNECTED      = -8,  /**< Client is not currently connected to the server. */
};

} // namespace opendtp
