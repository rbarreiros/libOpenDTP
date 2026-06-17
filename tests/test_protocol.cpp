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
#include <cstring>
#include "opendtp/Protocol.hpp"
#include "Sha256.hpp"

// Test SHA256 Cryptography with standard RFC test vectors
TEST(CryptoTest, Sha256TestVectors) {
    // Test Vector 1: Empty string
    // Digest: e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
    std::string empty = "";
    std::span<const uint8_t> emptySpan(reinterpret_cast<const uint8_t*>(empty.data()), empty.size());
    auto hash1 = opendtp::crypto::Sha256(emptySpan);
    
    uint8_t expected1[32] = {
        0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14, 0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24,
        0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c, 0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55
    };
    for (size_t i = 0; i < 32; ++i) {
        EXPECT_EQ(hash1[i], expected1[i]);
    }

    // Test Vector 2: "abc"
    // Digest: ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
    std::string abc = "abc";
    std::span<const uint8_t> abcSpan(reinterpret_cast<const uint8_t*>(abc.data()), abc.size());
    auto hash2 = opendtp::crypto::Sha256(abcSpan);
    
    uint8_t expected2[32] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad
    };
    for (size_t i = 0; i < 32; ++i) {
        EXPECT_EQ(hash2[i], expected2[i]);
    }
}

// Test Header serialization & deserialization round-trip
TEST(ProtocolTest, HeaderRoundTrip) {
    opendtp::protocol::Header h;
    h.type = opendtp::MessageType::KEEP_ALIVE;
    h.flags = opendtp::MessageFlag::REAL_TIME_1;
    h.number = 123456;
    h.length = 789;

    auto bytes = opendtp::protocol::SerializeHeader(h);
    EXPECT_EQ(bytes.size(), 18);

    auto result = opendtp::protocol::DeserializeHeader(bytes);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->type, h.type);
    EXPECT_EQ(result->flags, h.flags);
    EXPECT_EQ(result->number, h.number);
    EXPECT_EQ(result->length, h.length);
}

// Test Header deserialization failures (short buffer, bad signature)
TEST(ProtocolTest, HeaderFailures) {
    std::vector<uint8_t> shortBytes = { 1, 2, 3 };
    auto result = opendtp::protocol::DeserializeHeader(shortBytes);
    EXPECT_FALSE(result.has_value());

    std::vector<uint8_t> badSignBytes(18, 0);
    std::memcpy(badSignBytes.data(), "BADSIGN1", 8);
    result = opendtp::protocol::DeserializeHeader(badSignBytes);
    EXPECT_FALSE(result.has_value());
}

// Test VersionData serialization & deserialization round-trip
TEST(ProtocolTest, VersionDataRoundTrip) {
    opendtp::protocol::VersionData v;
    v.dmrId = 268999;
    v.service = opendtp::ServiceType::OPEN_TERMINAL;
    v.description = "TestTerminal v1.2.3";

    auto bytes = opendtp::protocol::SerializeVersionData(v);
    EXPECT_EQ(bytes.size(), 5 + v.description.size());

    auto result = opendtp::protocol::DeserializeVersionData(bytes);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->dmrId, v.dmrId);
    EXPECT_EQ(result->service, v.service);
    EXPECT_EQ(result->description, v.description);
}

// Test SubscriptionData serialization & deserialization
TEST(ProtocolTest, SubscriptionDataRoundTrip) {
    opendtp::protocol::SubscriptionData sub;
    sub.type = 7;
    sub.number = 91;

    auto bytes = opendtp::protocol::SerializeSubscription(sub);
    EXPECT_EQ(bytes.size(), 8);

    auto result = opendtp::protocol::DeserializeSubscription(bytes);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->type, sub.type);
    EXPECT_EQ(result->number, sub.number);
}

// Test TextMessage serialization & deserialization with non-ASCII and UTF-8 characters
TEST(ProtocolTest, TextMessageRoundTrip) {
    opendtp::protocol::TextMessageData textMsg;
    textMsg.sourceId = 12345;
    textMsg.destinationId = 91;
    textMsg.isGroup = true;
    textMsg.text = "Hello Brandmeister! 📻 Testing 123";

    auto bytes = opendtp::protocol::SerializeTextMessage(textMsg);
    
    auto result = opendtp::protocol::DeserializeTextMessage(bytes);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->sourceId, textMsg.sourceId);
    EXPECT_EQ(result->destinationId, textMsg.destinationId);
    EXPECT_EQ(result->isGroup, textMsg.isGroup);
    EXPECT_EQ(result->text, textMsg.text);
}

// Test LocationReport serialization & deserialization
TEST(ProtocolTest, LocationReportRoundTrip) {
    opendtp::protocol::LocationReport loc;
    loc.format = 0; // NMEA
    loc.nmea = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A";

    auto bytes = opendtp::protocol::SerializeLocationReport(loc);
    
    auto result = opendtp::protocol::DeserializeLocationReport(bytes);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->format, loc.format);
    EXPECT_EQ(result->nmea, loc.nmea);
}
