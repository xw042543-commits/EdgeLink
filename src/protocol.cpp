#include "edgelink/protocol.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace edgelink {
namespace {

void append_u16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 8U));
    out.push_back(static_cast<std::uint8_t>(value));
}

void append_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 24U));
    out.push_back(static_cast<std::uint8_t>(value >> 16U));
    out.push_back(static_cast<std::uint8_t>(value >> 8U));
    out.push_back(static_cast<std::uint8_t>(value));
}

void append_u64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    append_u32(out, static_cast<std::uint32_t>(value >> 32U));
    append_u32(out, static_cast<std::uint32_t>(value));
}

std::uint16_t read_u16(const std::uint8_t* data) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[0]) << 8U) | data[1]);
}

std::uint32_t read_u32(const std::uint8_t* data) {
    return (static_cast<std::uint32_t>(data[0]) << 24U) |
           (static_cast<std::uint32_t>(data[1]) << 16U) |
           (static_cast<std::uint32_t>(data[2]) << 8U) |
           static_cast<std::uint32_t>(data[3]);
}

std::uint64_t read_u64(const std::uint8_t* data) {
    return (static_cast<std::uint64_t>(read_u32(data)) << 32U) | read_u32(data + 4);
}

bool is_known_type(std::uint8_t type) {
    return type >= static_cast<std::uint8_t>(MessageType::hello) &&
           type <= static_cast<std::uint8_t>(MessageType::acknowledgment);
}

} // namespace

std::vector<std::uint8_t> encode_hello(const HelloPayload& hello) {
    if (hello.device_id.empty() || hello.device_id.size() > 64) {
        throw std::invalid_argument("device id must contain 1 to 64 bytes");
    }
    std::vector<std::uint8_t> payload;
    payload.reserve(8 + hello.device_id.size());
    append_u64(payload, hello.session_id);
    payload.insert(payload.end(), hello.device_id.begin(), hello.device_id.end());
    return payload;
}

std::optional<HelloPayload> decode_hello(std::span<const std::uint8_t> payload) {
    if (payload.size() < 9 || payload.size() > 72) {
        return std::nullopt;
    }
    return HelloPayload{read_u64(payload.data()), {payload.begin() + 8, payload.end()}};
}

std::vector<std::uint8_t> encode_telemetry(const TelemetryPayload& telemetry) {
    std::vector<std::uint8_t> payload;
    payload.reserve(16);
    append_u64(payload, telemetry.timestamp_ms);
    append_u16(payload, static_cast<std::uint16_t>(telemetry.temperature_centi_c));
    append_u16(payload, telemetry.humidity_centi_pct);
    append_u16(payload, telemetry.voltage_mv);
    append_u16(payload, telemetry.sensor_status);
    return payload;
}

std::optional<TelemetryPayload> decode_telemetry(std::span<const std::uint8_t> payload) {
    if (payload.size() != 16) {
        return std::nullopt;
    }
    return TelemetryPayload{
        read_u64(payload.data()),
        static_cast<std::int16_t>(read_u16(payload.data() + 8)),
        read_u16(payload.data() + 10),
        read_u16(payload.data() + 12),
        read_u16(payload.data() + 14),
    };
}

std::string Message::payload_as_string() const {
    return {payload.begin(), payload.end()};
}

std::uint32_t crc32(std::span<const std::uint8_t> data) {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const auto byte : data) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            const auto mask = static_cast<std::uint32_t>(-(static_cast<int>(crc & 1U)));
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

std::vector<std::uint8_t> encode(const Message& message) {
    if (message.payload.size() > kMaxPayloadSize) {
        throw std::invalid_argument("payload exceeds protocol limit");
    }

    std::vector<std::uint8_t> frame;
    frame.reserve(kHeaderSize + message.payload.size());
    append_u16(frame, kMagic);
    frame.push_back(kProtocolVersion);
    frame.push_back(static_cast<std::uint8_t>(message.type));
    append_u16(frame, static_cast<std::uint16_t>(message.payload.size()));
    append_u32(frame, message.sequence);
    frame.insert(frame.end(), message.payload.begin(), message.payload.end());

    const auto checksum = crc32(frame);
    frame.insert(frame.begin() + 10, {
        static_cast<std::uint8_t>(checksum >> 24U),
        static_cast<std::uint8_t>(checksum >> 16U),
        static_cast<std::uint8_t>(checksum >> 8U),
        static_cast<std::uint8_t>(checksum),
    });
    return frame;
}

const char* message_type_name(MessageType type) {
    switch (type) {
    case MessageType::hello: return "HELLO";
    case MessageType::telemetry: return "TELEMETRY";
    case MessageType::heartbeat: return "HEARTBEAT";
    case MessageType::acknowledgment: return "ACK";
    }
    return "UNKNOWN";
}

std::vector<Message> StreamParser::push(std::span<const std::uint8_t> bytes) {
    buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
    std::vector<Message> messages;
    static constexpr std::array<std::uint8_t, 2> magic_bytes{0x45, 0x44};

    while (buffer_.size() >= 2) {
        const auto magic_position = std::search(
            buffer_.begin(), buffer_.end(),
            magic_bytes.begin(), magic_bytes.end());

        if (magic_position != buffer_.begin()) {
            if (magic_position == buffer_.end()) {
                const bool keep_prefix = buffer_.back() == 0x45;
                buffer_.erase(buffer_.begin(), keep_prefix ? buffer_.end() - 1 : buffer_.end());
                break;
            }
            buffer_.erase(buffer_.begin(), magic_position);
        }

        if (buffer_.size() < kHeaderSize) {
            break;
        }

        const auto version = buffer_[2];
        const auto raw_type = buffer_[3];
        const auto payload_size = read_u16(buffer_.data() + 4);
        const auto frame_size = kHeaderSize + payload_size;
        if (version != kProtocolVersion || !is_known_type(raw_type) || payload_size > kMaxPayloadSize) {
            ++rejected_frames_;
            buffer_.erase(buffer_.begin());
            continue;
        }
        if (buffer_.size() < frame_size) {
            break;
        }

        const auto expected_crc = read_u32(buffer_.data() + 10);
        std::vector<std::uint8_t> crc_input(buffer_.begin(), buffer_.begin() + 10);
        crc_input.insert(crc_input.end(), buffer_.begin() + kHeaderSize, buffer_.begin() + frame_size);
        if (crc32(crc_input) != expected_crc) {
            ++rejected_frames_;
            buffer_.erase(buffer_.begin());
            continue;
        }

        Message message;
        message.type = static_cast<MessageType>(raw_type);
        message.sequence = read_u32(buffer_.data() + 6);
        message.payload.assign(buffer_.begin() + kHeaderSize, buffer_.begin() + frame_size);
        messages.push_back(std::move(message));
        buffer_.erase(buffer_.begin(), buffer_.begin() + frame_size);
    }
    return messages;
}

} // namespace edgelink
