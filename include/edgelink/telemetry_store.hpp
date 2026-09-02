#pragma once

#include "edgelink/telemetry_record.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace edgelink {

enum class StoreResult {
    inserted,
    duplicate,
    conflict,
    error,
};

class TelemetryStore {
public:
    virtual ~TelemetryStore() = default;

    [[nodiscard]] virtual StoreResult save(
        const TelemetryRecord& record) = 0;

    [[nodiscard]] virtual std::optional<TelemetryRecord> latest(
        const std::string& device_id) = 0;

    [[nodiscard]] virtual std::vector<TelemetryRecord> history(
        const std::string& device_id,
        std::size_t limit) = 0;

    [[nodiscard]] virtual const std::string& last_error() const noexcept = 0;
};

} // namespace edgelink
