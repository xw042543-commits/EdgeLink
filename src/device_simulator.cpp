#include "edgelink/protocol.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>
#include <exception>
#include <iostream>
#include <netinet/in.h>
#include <poll.h>
#include <random>
#include <span>
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

bool receive_ack(int socket_fd, std::uint32_t expected_sequence) {
    edgelink::StreamParser parser;
    std::uint8_t buffer[256];
    pollfd ack_socket{socket_fd, POLLIN, 0};

    while (true) {
        const int ready = ::poll(&ack_socket, 1, 2000);
        if (ready <= 0) {
            return false;
        }

        const auto count = ::recv(socket_fd, buffer, sizeof(buffer), 0);
        if (count <= 0) {
            return false;
        }

        const auto messages =
            parser.push(std::span(buffer, static_cast<std::size_t>(count)));

        for (const auto& received : messages) {
            if (received.type == edgelink::MessageType::acknowledgment &&
                received.sequence == expected_sequence) {
                return true;
            }
        }
    }
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

int connect_with_retry(const std::string& host, int port) {
    int socket_fd = -1;

    for (int attempt = 1; attempt <= 3; ++attempt) {
        socket_fd = connect_to(host, port);

        if (socket_fd >= 0) {
            return socket_fd;
        }

        std::cerr << "Connection attempt " << attempt << " failed.\n";

        if (attempt < 3) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    }

    return -1;
}

} // namespace

int main(int argc, char** argv) {
    const std::string device_id = argc > 1 ? argv[1] : "esp32-sim-001";
    const std::string host = argc > 2 ? argv[2] : "127.0.0.1";
    int port = 9000;
    double temperature = 30.0;
    double humidity = 58.0;
    try {
        if (argc > 3) {
            port = std::stoi(argv[3]);
        }
        if (argc > 4) {
            temperature = std::stod(argv[4]);
        }
        if (argc > 5) {
            humidity = std::stod(argv[5]);
        }
    } catch (const std::exception&) {
        std::cerr << "Port, temperature, and humidity must be numbers.\n";
        return 2;
    }

    if (temperature < -40.0 || temperature > 125.0) {
        std::cerr << "Temperature must be between -40 and 125.\n";
        return 2;
    }

    if (port < 1 || port > 65535) {
        std::cerr << "Port must be between 1 and 65535.\n";
        return 2;
    }

    if (humidity < 0.0 || humidity > 100.0) {
        std::cerr << "Humidity must be between 0 and 100.\n";
        return 2;
    }

    int socket_fd = connect_with_retry(host, port);

    if (socket_fd < 0) {
        std::cerr << "Cannot connect to " << host << ':' << port << '\n';
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
            static_cast<std::uint16_t>(std::lround(humidity * 100.0)),
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

        if (!receive_ack(socket_fd, telemetry.sequence)) {
            std::cerr << "ACK not received for seq=" << telemetry.sequence << '\n';
            ::close(socket_fd);
            return 1;
        }

        std::cout << "ACK received: seq=" << telemetry.sequence << '\n';

        std::cout << "Sent: temperature=" << temperature
                  << "C humidity=" << reading.humidity_centi_pct / 100.0 << "%\n";
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    for (int heartbeat_count = 0; heartbeat_count < 3; ++heartbeat_count) {
        std::this_thread::sleep_for(std::chrono::seconds(5));

        const auto heartbeat =
            message(edgelink::MessageType::heartbeat, sequence++);

        if (!send_all(socket_fd, edgelink::encode(heartbeat))) {
            std::cerr << "Failed to send heartbeat.\n";
            ::close(socket_fd);
            return 1;
        }

        if (!receive_ack(socket_fd, heartbeat.sequence)) {
            std::cerr << "Heartbeat ACK not received.\n";
            ::close(socket_fd);
            return 1;
        }

        std::cout << "Heartbeat acknowledged: seq="
                  << heartbeat.sequence << '\n';
    }
    ::close(socket_fd);
    std::cout << "Simulation complete.\n";
}
