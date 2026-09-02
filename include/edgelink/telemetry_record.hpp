#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace edgelink {

// One validated sensor measurement ready for durable storage.
struct TelemetryRecord {
    std::string device_id;
    std::array<std::uint8_t, 8> session_id{};
    std::uint32_t sequence{};
    std::uint64_t device_timestamp_ms{};
    std::int64_t received_at_ms{};
    std::int16_t temperature_centi_c{};
    std::uint16_t humidity_centi_pct{};
    std::uint16_t voltage_mv{};
    std::uint16_t sensor_status{};
};

} // namespace edgelink
