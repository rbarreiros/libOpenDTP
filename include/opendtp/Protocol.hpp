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

#include <vector>
#include <string>
#include <string_view>
#include <span>
#include <optional>
#include <bit>
#include "Common.hpp"

/**
 * @file Protocol.hpp
 * @brief Low-level binary protocol structures, serialization, and deserialization functions.
 */

namespace opendtp::protocol {

/**
 * @brief Low-level C++ representation of the 18-byte Rewind protocol packet header.
 */
struct Header {
    MessageType type; /**< The message command or type. */
    uint16_t flags;   /**< Packet flags (e.g. MessageFlag::REAL_TIME_1). */
    uint32_t number;  /**< Packet sequence number. */
    uint16_t length;  /**< Size of the payload immediately following the header. */
};

/**
 * @brief Keep-Alive registration and identity payload structure.
 */
struct VersionData {
    uint32_t dmrId;           /**< Client's registered DMR ID. */
    ServiceType service;      /**< Service classification (typically ServiceType::OPEN_TERMINAL). */
    std::string description; /**< Descriptive software identification string. */
};

/**
 * @brief Subscription request payload (used for joining or leaving a talkgroup).
 */
struct SubscriptionData {
    uint32_t type;   /**< Session call type (typically SESSION_TYPE_GROUP_VOICE). */
    uint32_t number; /**< Target talkgroup DMR ID. */
};

/**
 * @brief Active session poll metadata query.
 */
struct SessionPollData {
    uint32_t type;   /**< Session call type. */
    uint32_t flag;   /**< Group or private sorting flags. */
    uint32_t number; /**< Target talkgroup or subscriber ID. */
    uint32_t state;  /**< Current session state representation. */
};

/**
 * @brief SuperHeader metadata structure sent at the start of a voice call.
 */
struct SuperHeader {
    uint32_t type;                /**< Call type ID. */
    uint32_t sourceId;            /**< Caller's source DMR ID. */
    uint32_t destinationId;       /**< Target receiver talkgroup/private DMR ID. */
    std::string sourceCall;       /**< Caller's FCC callsign (up to 10 chars). */
    std::string destinationCall;  /**< Target receiver callsign (up to 10 chars). */
};

/**
 * @brief Raw structure of a text SMS message payload (internally UTF-16LE).
 */
struct TextMessageData {
    uint32_t sourceId;      /**< Source DMR ID. */
    uint32_t destinationId; /**< Destination private or group DMR ID. */
    bool isGroup;           /**< True if group SMS, false if private SMS. */
    std::string text;       /**< Text message content (converted to UTF-8 in API). */
};

/**
 * @brief Text message receipt delivery status structure.
 */
struct TextMessageStatus {
    uint32_t sourceId;      /**< Original source DMR ID. */
    uint32_t destinationId; /**< Original destination DMR ID. */
    uint8_t status;         /**< SMS delivery status code. */
};

/**
 * @brief Location trigger query from the server.
 */
struct LocationRequest {
    uint32_t type;     /**< Trigger type (0 = Shot, 1 = Timed Start, 2 = Timed Stop). */
    uint32_t interval; /**< Update interval in seconds. */
};

/**
 * @brief Positional NMEA report payload wrapper.
 */
struct LocationReport {
    uint32_t format;  /**< Position format (0 = NMEA). */
    std::string nmea; /**< NMEA ASCII report string. */
};

/**
 * @brief Performs a compile-time byte swap operation to convert endianness.
 * @tparam T The integer or enum type.
 * @param val The value to swap.
 * @return The byte-swapped representation of the value.
 */
template <typename T>
constexpr T ByteSwap(T val) noexcept {
    if constexpr (sizeof(T) == 1) {
        return val;
    } else if constexpr (sizeof(T) == 2) {
        auto u = static_cast<uint16_t>(val);
        return static_cast<T>((u >> 8) | (u << 8));
    } else if constexpr (sizeof(T) == 4) {
        auto u = static_cast<uint32_t>(val);
        return static_cast<T>(
            ((u >> 24) & 0x000000FF) |
            ((u >> 8)  & 0x0000FF00) |
            ((u << 8)  & 0x00FF0000) |
            ((u << 24) & 0xFF000000)
        );
    } else if constexpr (sizeof(T) == 8) {
        auto u = static_cast<uint64_t>(val);
        u = ((u & 0x00000000FFFFFFFFULL) << 32) | ((u & 0xFFFFFFFF00000000ULL) >> 32);
        u = ((u & 0x0000FFFF0000FFFFULL) << 16) | ((u & 0xFFFF0000FFFF0000ULL) >> 16);
        u = ((u & 0x00FF00FF00FF00FFULL) << 8)  | ((u & 0xFF00FF00FF00FF00ULL) >> 8);
        return static_cast<T>(u);
    }
    return val;
}

/**
 * @brief Converts host-endian values to little-endian representation.
 * @tparam T The integral or enum type.
 * @param value The value to convert.
 * @return Little-endian representation.
 */
template <typename T>
requires std::is_integral_v<T> || std::is_enum_v<T>
constexpr T HostToLittle(T value) noexcept {
    if constexpr (std::endian::native == std::endian::big) {
        if constexpr (std::is_enum_v<T>) {
            using UnderlyingType = std::underlying_type_t<T>;
            return static_cast<T>(ByteSwap(static_cast<UnderlyingType>(value)));
        } else {
            return ByteSwap(value);
        }
    }
    return value;
}

/**
 * @brief Converts little-endian values to host-endian representation.
 * @tparam T The integral or enum type.
 * @param value The value to convert.
 * @return Host-endian representation.
 */
template <typename T>
requires std::is_integral_v<T> || std::is_enum_v<T>
constexpr T LittleToHost(T value) noexcept {
    return HostToLittle(value);
}

/**
 * @name Serialization APIs
 * @{
 */

/**
 * @brief Serializes a packet header into a binary buffer.
 * @param header The high-level header struct.
 * @return A vector of 18 bytes.
 */
std::vector<uint8_t> SerializeHeader(const Header& header);

/**
 * @brief Serializes Keep-Alive client identity into a binary payload.
 * @param data The VersionData identity.
 * @return Serialized bytes.
 */
std::vector<uint8_t> SerializeVersionData(const VersionData& data);

/**
 * @brief Serializes a Subscription request into binary.
 * @param data The SubscriptionData parameters.
 * @return Serialized bytes.
 */
std::vector<uint8_t> SerializeSubscription(const SubscriptionData& data);

/**
 * @brief Serializes a session query poll request into binary.
 * @param data The SessionPollData parameters.
 * @return Serialized bytes.
 */
std::vector<uint8_t> SerializeSessionPoll(const SessionPollData& data);

/**
 * @brief Serializes a text message (converting UTF-8 string to UTF-16LE payload).
 * @param data The TextMessageData.
 * @return Serialized bytes.
 */
std::vector<uint8_t> SerializeTextMessage(const TextMessageData& data);

/**
 * @brief Serializes a GPS location report payload.
 * @param data The NMEA LocationReport.
 * @return Serialized bytes.
 */
std::vector<uint8_t> SerializeLocationReport(const LocationReport& data);
/** @} */

/**
 * @name Deserialization APIs
 * @{
 */

/**
 * @brief Deserializes a packet header from bytes.
 * @param bytes The raw binary span.
 * @return The header struct on success, nullopt on format mismatch.
 */
std::optional<Header> DeserializeHeader(std::span<const uint8_t> bytes);

/**
 * @brief Deserializes Keep-Alive server payload parameters.
 * @param bytes The raw binary span.
 * @return The VersionData struct on success.
 */
std::optional<VersionData> DeserializeVersionData(std::span<const uint8_t> bytes);

/**
 * @brief Deserializes a Subscription request payload.
 * @param bytes The raw binary span.
 * @return The SubscriptionData.
 */
std::optional<SubscriptionData> DeserializeSubscription(std::span<const uint8_t> bytes);

/**
 * @brief Deserializes a session query poll response.
 * @param bytes The raw binary span.
 * @return The SessionPollData.
 */
std::optional<SessionPollData> DeserializeSessionPoll(std::span<const uint8_t> bytes);

/**
 * @brief Deserializes call SuperHeader participant metadata.
 * @param bytes The raw binary span.
 * @return The SuperHeader.
 */
std::optional<SuperHeader> DeserializeSuperHeader(std::span<const uint8_t> bytes);

/**
 * @brief Deserializes an incoming text SMS message (translating UTF-16LE to UTF-8).
 * @param bytes The raw binary span.
 * @return The TextMessageData.
 */
std::optional<TextMessageData> DeserializeTextMessage(std::span<const uint8_t> bytes);

/**
 * @brief Deserializes an incoming SMS status delivery report.
 * @param bytes The raw binary span.
 * @return The TextMessageStatus.
 */
std::optional<TextMessageStatus> DeserializeTextMessageStatus(std::span<const uint8_t> bytes);

/**
 * @brief Deserializes a location trigger request.
 * @param bytes The raw binary span.
 * @return The LocationRequest.
 */
std::optional<LocationRequest> DeserializeLocationRequest(std::span<const uint8_t> bytes);

/**
 * @brief Deserializes an incoming GPS location report.
 * @param bytes The raw binary span.
 * @return The LocationReport.
 */
std::optional<LocationReport> DeserializeLocationReport(std::span<const uint8_t> bytes);
/** @} */

} // namespace opendtp::protocol
