#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace edgelink {

struct DeviceSnapshot {
    std::string id;
    std::string peer;
    std::uint64_t messages{};
    std::chrono::seconds idle_for{};
};

class DeviceRegistry {
public:
    void connected(const std::string& id, const std::string& peer);
    void seen(const std::string& id);
    void disconnected(const std::string& id);
    [[nodiscard]] std::vector<DeviceSnapshot> snapshot() const;

private:
    struct DeviceState {
        std::string peer;
        std::uint64_t messages{};
        std::chrono::steady_clock::time_point last_seen;
    };

    mutable std::mutex mutex_;
    std::unordered_map<std::string, DeviceState> devices_;
};

} // namespace edgelink

