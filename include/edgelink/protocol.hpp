#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <optional>
#include <vector>

namespace edgelink {

inline constexpr std::uint16_t kMagic = 0x4544; // "ED"
inline constexpr std::uint8_t kProtocolVersion = 1;
inline constexpr std::size_t kHeaderSize = 14;
inline constexpr std::size_t kMaxPayloadSize = 4096;

enum class MessageType : std::uint8_t {
    hello = 1,
    telemetry = 2,
    heartbeat = 3,
    acknowledgment = 4,
};

struct Message {
    MessageType type{};
    std::uint32_t sequence{};
    std::vector<std::uint8_t> payload;

    [[nodiscard]] std::string payload_as_string() const;
};

struct HelloPayload {
    std::uint64_t session_id{};
    std::string device_id;
};

struct TelemetryPayload {
    std::uint64_t timestamp_ms{};
    std::int16_t temperature_centi_c{};
    std::uint16_t humidity_centi_pct{};
    std::uint16_t voltage_mv{};
    std::uint16_t sensor_status{};
};

[[nodiscard]] std::vector<std::uint8_t> encode_hello(const HelloPayload& hello);
[[nodiscard]] std::optional<HelloPayload> decode_hello(std::span<const std::uint8_t> payload);
[[nodiscard]] std::vector<std::uint8_t> encode_telemetry(const TelemetryPayload& telemetry);
[[nodiscard]] std::optional<TelemetryPayload> decode_telemetry(std::span<const std::uint8_t> payload);

[[nodiscard]] std::uint32_t crc32(std::span<const std::uint8_t> data);
[[nodiscard]] std::vector<std::uint8_t> encode(const Message& message);
[[nodiscard]] const char* message_type_name(MessageType type);

class StreamParser {
public:
    [[nodiscard]] std::vector<Message> push(std::span<const std::uint8_t> bytes);
    [[nodiscard]] std::size_t rejected_frames() const noexcept { return rejected_frames_; }
    [[nodiscard]] std::size_t buffered_bytes() const noexcept { return buffer_.size(); }

private:
    std::vector<std::uint8_t> buffer_;
    std::size_t rejected_frames_{};
};

} // namespace edgelink
