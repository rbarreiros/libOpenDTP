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

#include "opendtp/Protocol.hpp"
#include <cstring>

namespace opendtp::protocol {

namespace {

// Safe buffer write helpers to prevent alignment issues
void WriteBytes(std::vector<uint8_t>& buf, const void* data, size_t size) {
    const uint8_t* bytePtr = static_cast<const uint8_t*>(data);
    buf.insert(buf.end(), bytePtr, bytePtr + size);
}

template <typename T>
void WriteLE(std::vector<uint8_t>& buf, T val) {
    T leVal = HostToLittle(val);
    WriteBytes(buf, &leVal, sizeof(T));
}

// Safe buffer read helpers
template <typename T>
bool ReadLE(std::span<const uint8_t> bytes, size_t& offset, T& val) {
    if (offset + sizeof(T) > bytes.size()) {
        return false;
    }
    T rawVal;
    std::memcpy(&rawVal, bytes.data() + offset, sizeof(T));
    val = LittleToHost(rawVal);
    offset += sizeof(T);
    return true;
}

bool ReadBytes(std::span<const uint8_t> bytes, size_t& offset, void* dest, size_t size) {
    if (offset + size > bytes.size()) {
        return false;
    }
    std::memcpy(dest, bytes.data() + offset, size);
    offset += size;
    return true;
}

// Custom UTF-8 to UTF-16 conversion helpers
std::vector<uint16_t> Utf8ToUtf16(std::string_view utf8) {
    std::vector<uint16_t> utf16;
    utf16.reserve(utf8.size());
    for (size_t i = 0; i < utf8.size(); ) {
        uint32_t cp = 0;
        uint8_t c = static_cast<uint8_t>(utf8[i]);
        size_t len = 0;
        if (c < 0x80) { cp = c; len = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
        else { i++; continue; } // Invalid UTF-8 starting byte
        
        if (i + len > utf8.size()) break;
        for (size_t j = 1; j < len; ++j) {
            cp = (cp << 6) | (static_cast<uint8_t>(utf8[i + j]) & 0x3F);
        }
        i += len;
        
        if (cp < 0x10000) {
            utf16.push_back(static_cast<uint16_t>(cp));
        } else if (cp <= 0x10FFFF) {
            cp -= 0x10000;
            utf16.push_back(static_cast<uint16_t>((cp >> 10) + 0xD800));
            utf16.push_back(static_cast<uint16_t>((cp & 0x3FF) + 0xDC00));
        }
    }
    return utf16;
}

std::string Utf16ToUtf8(std::span<const uint16_t> utf16) {
    std::string utf8;
    utf8.reserve(utf16.size());
    for (size_t i = 0; i < utf16.size(); ) {
        uint32_t cp = utf16[i++];
        if (cp >= 0xD800 && cp <= 0xDBFF && i < utf16.size()) {
            uint32_t trail = utf16[i];
            if (trail >= 0xDC00 && trail <= 0xDFFF) {
                cp = ((cp - 0xD800) << 10) + (trail - 0xDC00) + 0x10000;
                i++;
            }
        }
        if (cp < 0x80) {
            utf8.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            utf8.push_back(static_cast<char>((cp >> 6) | 0xC0));
            utf8.push_back(static_cast<char>((cp & 0x3F) | 0x80));
        } else if (cp < 0x10000) {
            utf8.push_back(static_cast<char>((cp >> 12) | 0xE0));
            utf8.push_back(static_cast<char>(((cp >> 6) & 0x3F) | 0x80));
            utf8.push_back(static_cast<char>((cp & 0x3F) | 0x80));
        } else {
            utf8.push_back(static_cast<char>((cp >> 18) | 0xF0));
            utf8.push_back(static_cast<char>(((cp >> 12) & 0x3F) | 0x80));
            utf8.push_back(static_cast<char>(((cp >> 6) & 0x3F) | 0x80));
            utf8.push_back(static_cast<char>((cp & 0x3F) | 0x80));
        }
    }
    return utf8;
}

} // namespace

std::vector<uint8_t> SerializeHeader(const Header& header) {
    std::vector<uint8_t> buf;
    buf.reserve(18);
    WriteBytes(buf, PROTOCOL_SIGN.data(), SIGN_LENGTH);
    WriteLE(buf, header.type);
    WriteLE(buf, header.flags);
    WriteLE(buf, header.number);
    WriteLE(buf, header.length);
    return buf;
}

std::vector<uint8_t> SerializeVersionData(const VersionData& data) {
    std::vector<uint8_t> buf;
    buf.reserve(5 + data.description.size());
    WriteLE(buf, data.dmrId);
    WriteLE(buf, static_cast<uint8_t>(data.service));
    WriteBytes(buf, data.description.data(), data.description.size());
    return buf;
}

std::vector<uint8_t> SerializeSubscription(const SubscriptionData& data) {
    std::vector<uint8_t> buf;
    buf.reserve(8);
    WriteLE(buf, data.type);
    WriteLE(buf, data.number);
    return buf;
}

std::vector<uint8_t> SerializeSessionPoll(const SessionPollData& data) {
    std::vector<uint8_t> buf;
    buf.reserve(16);
    WriteLE(buf, data.type);
    WriteLE(buf, data.flag);
    WriteLE(buf, data.number);
    WriteLE(buf, data.state);
    return buf;
}

std::vector<uint8_t> SerializeTextMessage(const TextMessageData& data) {
    std::vector<uint8_t> buf;
    std::vector<uint16_t> utf16Text = Utf8ToUtf16(data.text);
    
    buf.reserve(16 + utf16Text.size() * 2);
    WriteLE(buf, static_cast<uint32_t>(0)); // reserved
    WriteLE(buf, data.sourceId);
    WriteLE(buf, data.destinationId);
    
    uint16_t option = data.isGroup ? 128 : 0;
    WriteLE(buf, option);
    
    uint16_t byteLength = static_cast<uint16_t>(utf16Text.size() * 2);
    WriteLE(buf, byteLength);
    
    for (uint16_t val : utf16Text) {
        WriteLE(buf, val);
    }
    
    return buf;
}

std::vector<uint8_t> SerializeLocationReport(const LocationReport& data) {
    std::vector<uint8_t> buf;
    buf.reserve(10 + data.nmea.size());
    WriteLE(buf, static_cast<uint32_t>(0)); // reserved
    WriteLE(buf, data.format);
    
    // NMEA string should include the null terminator in bytes
    uint16_t byteLength = static_cast<uint16_t>(data.nmea.size() + 1);
    WriteLE(buf, byteLength);
    WriteBytes(buf, data.nmea.data(), data.nmea.size());
    buf.push_back(0); // Null terminator
    return buf;
}

std::optional<Header> DeserializeHeader(std::span<const uint8_t> bytes) {
    if (bytes.size() < 18) {
        return std::nullopt;
    }
    
    if (std::memcmp(bytes.data(), PROTOCOL_SIGN.data(), SIGN_LENGTH) != 0) {
        return std::nullopt;
    }
    
    size_t offset = SIGN_LENGTH;
    Header h;
    
    uint16_t typeRaw;
    if (!ReadLE(bytes, offset, typeRaw)) return std::nullopt;
    h.type = static_cast<MessageType>(typeRaw);
    
    if (!ReadLE(bytes, offset, h.flags)) return std::nullopt;
    if (!ReadLE(bytes, offset, h.number)) return std::nullopt;
    if (!ReadLE(bytes, offset, h.length)) return std::nullopt;
    
    return h;
}

std::optional<VersionData> DeserializeVersionData(std::span<const uint8_t> bytes) {
    if (bytes.size() < 5) {
        return std::nullopt;
    }
    
    size_t offset = 0;
    VersionData data;
    if (!ReadLE(bytes, offset, data.dmrId)) return std::nullopt;
    
    uint8_t serviceRaw;
    if (!ReadLE(bytes, offset, serviceRaw)) return std::nullopt;
    data.service = static_cast<ServiceType>(serviceRaw);
    
    if (bytes.size() > offset) {
        data.description.assign(reinterpret_cast<const char*>(bytes.data() + offset), bytes.size() - offset);
        // Clean trailing null-characters if present
        while (!data.description.empty() && data.description.back() == '\0') {
            data.description.pop_back();
        }
    }
    return data;
}

std::optional<SubscriptionData> DeserializeSubscription(std::span<const uint8_t> bytes) {
    if (bytes.size() < 8) {
        return std::nullopt;
    }
    size_t offset = 0;
    SubscriptionData data;
    if (!ReadLE(bytes, offset, data.type)) return std::nullopt;
    if (!ReadLE(bytes, offset, data.number)) return std::nullopt;
    return data;
}

std::optional<SessionPollData> DeserializeSessionPoll(std::span<const uint8_t> bytes) {
    if (bytes.size() < 16) {
        return std::nullopt;
    }
    size_t offset = 0;
    SessionPollData data;
    if (!ReadLE(bytes, offset, data.type)) return std::nullopt;
    if (!ReadLE(bytes, offset, data.flag)) return std::nullopt;
    if (!ReadLE(bytes, offset, data.number)) return std::nullopt;
    if (!ReadLE(bytes, offset, data.state)) return std::nullopt;
    return data;
}

std::optional<SuperHeader> DeserializeSuperHeader(std::span<const uint8_t> bytes) {
    if (bytes.size() < 32) { // 4*3 + 10*2 = 32
        return std::nullopt;
    }
    size_t offset = 0;
    SuperHeader data;
    if (!ReadLE(bytes, offset, data.type)) return std::nullopt;
    if (!ReadLE(bytes, offset, data.sourceId)) return std::nullopt;
    if (!ReadLE(bytes, offset, data.destinationId)) return std::nullopt;
    
    char srcCall[10];
    if (!ReadBytes(bytes, offset, srcCall, 10)) return std::nullopt;
    data.sourceCall.assign(srcCall, strnlen(srcCall, 10));
    
    char destCall[10];
    if (!ReadBytes(bytes, offset, destCall, 10)) return std::nullopt;
    data.destinationCall.assign(destCall, strnlen(destCall, 10));
    
    return data;
}

std::optional<TextMessageData> DeserializeTextMessage(std::span<const uint8_t> bytes) {
    if (bytes.size() < 16) {
        return std::nullopt;
    }
    size_t offset = 0;
    uint32_t reserved;
    if (!ReadLE(bytes, offset, reserved)) return std::nullopt;
    
    TextMessageData data;
    if (!ReadLE(bytes, offset, data.sourceId)) return std::nullopt;
    if (!ReadLE(bytes, offset, data.destinationId)) return std::nullopt;
    
    uint16_t option;
    if (!ReadLE(bytes, offset, option)) return std::nullopt;
    data.isGroup = (option == 128);
    
    uint16_t byteLength;
    if (!ReadLE(bytes, offset, byteLength)) return std::nullopt;
    
    if (offset + byteLength > bytes.size()) {
        return std::nullopt;
    }
    
    std::vector<uint16_t> utf16Text(byteLength / 2);
    for (size_t i = 0; i < utf16Text.size(); ++i) {
        if (!ReadLE(bytes, offset, utf16Text[i])) return std::nullopt;
    }
    
    data.text = Utf16ToUtf8(utf16Text);
    return data;
}

std::optional<TextMessageStatus> DeserializeTextMessageStatus(std::span<const uint8_t> bytes) {
    if (bytes.size() < 13) {
        return std::nullopt;
    }
    size_t offset = 0;
    uint32_t reserved;
    if (!ReadLE(bytes, offset, reserved)) return std::nullopt;
    
    TextMessageStatus data;
    if (!ReadLE(bytes, offset, data.sourceId)) return std::nullopt;
    if (!ReadLE(bytes, offset, data.destinationId)) return std::nullopt;
    if (!ReadLE(bytes, offset, data.status)) return std::nullopt;
    
    return data;
}

std::optional<LocationRequest> DeserializeLocationRequest(std::span<const uint8_t> bytes) {
    if (bytes.size() < 12) {
        return std::nullopt;
    }
    size_t offset = 0;
    uint32_t reserved;
    if (!ReadLE(bytes, offset, reserved)) return std::nullopt;
    
    LocationRequest data;
    if (!ReadLE(bytes, offset, data.type)) return std::nullopt;
    if (!ReadLE(bytes, offset, data.interval)) return std::nullopt;
    
    return data;
}

std::optional<LocationReport> DeserializeLocationReport(std::span<const uint8_t> bytes) {
    if (bytes.size() < 10) {
        return std::nullopt;
    }
    size_t offset = 0;
    uint32_t reserved;
    if (!ReadLE(bytes, offset, reserved)) return std::nullopt;
    
    LocationReport data;
    if (!ReadLE(bytes, offset, data.format)) return std::nullopt;
    
    uint16_t byteLength;
    if (!ReadLE(bytes, offset, byteLength)) return std::nullopt;
    
    if (offset + byteLength > bytes.size()) {
        return std::nullopt;
    }
    
    if (byteLength > 0) {
        const char* charData = reinterpret_cast<const char*>(bytes.data() + offset);
        // Exclude trailing null characters if they were included
        size_t len = strnlen(charData, byteLength);
        data.nmea.assign(charData, len);
    }
    
    return data;
}

} // namespace opendtp::protocol
