#include "edgelink/device_registry.hpp"

namespace edgelink {

void DeviceRegistry::connected(const std::string& id, const std::string& peer) {
    std::scoped_lock lock(mutex_);
    devices_[id] = DeviceState{peer, 0, std::chrono::steady_clock::now()};
}

void DeviceRegistry::seen(const std::string& id) {
    std::scoped_lock lock(mutex_);
    if (const auto it = devices_.find(id); it != devices_.end()) {
        ++it->second.messages;
        it->second.last_seen = std::chrono::steady_clock::now();
    }
}

void DeviceRegistry::disconnected(const std::string& id) {
    std::scoped_lock lock(mutex_);
    devices_.erase(id);
}

std::vector<DeviceSnapshot> DeviceRegistry::snapshot() const {
    const auto now = std::chrono::steady_clock::now();
    std::scoped_lock lock(mutex_);
    std::vector<DeviceSnapshot> result;
    result.reserve(devices_.size());
    for (const auto& [id, state] : devices_) {
        result.push_back({id, state.peer, state.messages,
                          std::chrono::duration_cast<std::chrono::seconds>(now - state.last_seen)});
    }
    return result;
}

} // namespace edgelink

