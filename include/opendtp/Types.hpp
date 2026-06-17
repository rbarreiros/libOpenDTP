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
#include <vector>
#include <cstdint>

/**
 * @file Types.hpp
 * @brief Public data structures for text messages, location reports, and audio frames.
 */

namespace opendtp {

/**
 * @brief Represents a text message sent or received via the DMR terminal protocol.
 */
struct TextMessage {
    uint32_t sourceId;      /**< The DMR ID of the message source. */
    uint32_t destinationId; /**< The DMR ID of the message destination (private or group). */
    bool isGroup;           /**< True if the message is destined for a group/talkgroup, false for a private call. */
    std::string text;       /**< The UTF-8 encoded text content of the message. */
};

/**
 * @brief Represents a GPS location report.
 */
struct LocationReport {
    uint32_t format = 0;   /**< The location report format (0 = NMEA format). */
    std::string nmea;      /**< The raw NMEA ASCII string (e.g. "$GPRMC..."). */
};

/**
 * @brief Represents a received or sent DMR audio frame.
 */
struct AudioFrame {
    uint32_t sequenceNumber;       /**< The real-time sequence number of this frame. */
    std::vector<uint8_t> ambeData; /**< Typically 27 bytes containing triple AMBE frames (9 bytes each). */
};

} // namespace opendtp
