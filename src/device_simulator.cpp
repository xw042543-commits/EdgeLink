#include "edgelink/protocol.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <span>
#include <random>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace {

bool send_all(int socket, std::span<const std::uint8_t> bytes) {
    std::size_t sent = 0;
    while (sent < bytes.size()) {
        const auto count = ::send(socket, bytes.data() + sent, bytes.size() - sent, 0);
        if (count <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(count);
    }
    return true;
}

edgelink::Message message(edgelink::MessageType type, std::uint32_t sequence,
                          const std::string& payload = {}) {
    return {type, sequence, {payload.begin(), payload.end()}};
}

int connect_to(const std::string& host, int port) {
    const int socket_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        return -1;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<std::uint16_t>(port));
    if (inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1 ||
        ::connect(socket_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        ::close(socket_fd);
        return -1;
    }
    return socket_fd;
}

} // namespace

int main(int argc, char** argv) {
    const std::string device_id = argc > 1 ? argv[1] : "esp32-sim-001";
    const std::string host = argc > 2 ? argv[2] : "127.0.0.1";
    const int port = argc > 3 ? std::stoi(argv[3]) : 9000;
    const double temperature  = argc > 4 ? std::stod(argv[4]) : 30.0;

    const int socket_fd = connect_to(host, port);
    if (socket_fd < 0) {
        std::cerr << "Cannot connect to " << host << ':' << port << ": "
                  << std::strerror(errno) << '\n';
        return 1;
    }

    std::cout << "Connected as " << device_id << " to " << host << ':' << port << '\n';
    std::uint32_t sequence = 1;
    std::random_device random;
    const auto session_id = (static_cast<std::uint64_t>(random()) << 32U) | random();
    edgelink::Message hello{edgelink::MessageType::hello, sequence++,
                            edgelink::encode_hello({session_id, device_id})};
    if (!send_all(socket_fd, edgelink::encode(hello))) {
        return 1;
    }

    for (int sample = 0; sample < 10; ++sample) {
        const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        edgelink::TelemetryPayload reading{
            static_cast<std::uint64_t>(now),
            static_cast<std::int16_t>(std::lround(temperature * 100.0)),
            static_cast<std::uint16_t>((58 + sample % 4) * 100),
            3300,
            0,
        };
        edgelink::Message telemetry{edgelink::MessageType::telemetry, sequence++,
                                     edgelink::encode_telemetry(reading)};
        const auto frame = edgelink::encode(telemetry);
        if (!send_all(socket_fd, frame)) {
            std::cerr << "Connection lost.\n";
            ::close(socket_fd);
            return 1;
        }
        std::cout << "Sent: temperature=" << temperature
                  << "C humidity=" << reading.humidity_centi_pct / 100.0 << "%\n";
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    send_all(socket_fd, edgelink::encode(message(edgelink::MessageType::heartbeat, sequence)));
    ::close(socket_fd);
    std::cout << "Simulation complete.\n";
}
