#include "edgelink/protocol.hpp"

#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <poll.h>
#include <span>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

struct Settings {
    std::string host{"127.0.0.1"};
    int port{9040};
    int clients{20};
    int messages_per_client{20};
};

bool parse_positive(const char* text, int maximum, int& value) {
    try {
        const int parsed = std::stoi(text);
        if (parsed < 1 || parsed > maximum) {
            return false;
        }
        value = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_settings(int argc, char** argv, Settings& settings) {
    if (argc > 1) {
        settings.host = argv[1];
    }
    if (argc > 2 && !parse_positive(argv[2], 65535, settings.port)) {
        return false;
    }
    if (argc > 3 && !parse_positive(argv[3], 10000, settings.clients)) {
        return false;
    }
    if (argc > 4 &&
        !parse_positive(argv[4], 100000, settings.messages_per_client)) {
        return false;
    }
    return argc <= 5;
}

int connect_to(const std::string& host, int port) {
    const int socket_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        return -1;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<std::uint16_t>(port));
    if (::inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1 ||
        ::connect(socket_fd,
                  reinterpret_cast<sockaddr*>(&address),
                  sizeof(address)) < 0) {
        ::close(socket_fd);
        return -1;
    }
    return socket_fd;
}

bool send_all(int socket_fd, std::span<const std::uint8_t> bytes) {
    std::size_t sent = 0;
    while (sent < bytes.size()) {
        const auto count =
            ::send(socket_fd, bytes.data() + sent, bytes.size() - sent, 0);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(count);
    }
    return true;
}

bool receive_ack(int socket_fd, std::uint32_t expected_sequence) {
    edgelink::StreamParser parser;
    std::uint8_t buffer[512];

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        pollfd connection{socket_fd, POLLIN, 0};
        const int ready = ::poll(
            &connection,
            1,
            static_cast<int>(remaining.count()));

        if (ready < 0 && errno == EINTR) {
            continue;
        }
        if (ready <= 0) {
            return false;
        }

        const auto count = ::recv(socket_fd, buffer, sizeof(buffer), 0);
        if (count <= 0) {
            return false;
        }

        const auto messages = parser.push(std::span(
            buffer,
            static_cast<std::size_t>(count)));
        for (const auto& message : messages) {
            if (message.type == edgelink::MessageType::acknowledgment &&
                message.sequence == expected_sequence) {
                return true;
            }
        }
    }
    return false;
}

bool send_message_and_wait(
    int socket_fd,
    const edgelink::Message& message) {
    return send_all(socket_fd, edgelink::encode(message)) &&
           receive_ack(socket_fd, message.sequence);
}

void run_client(
    const Settings& settings,
    int client_index,
    std::atomic_int& ready_clients,
    std::atomic_bool& start,
    std::atomic_int& successful_clients,
    std::atomic<std::uint64_t>& acknowledged_messages) {
    const int socket_fd = connect_to(settings.host, settings.port);
    if (socket_fd < 0) {
        return;
    }

    ++ready_clients;
    while (!start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }

    const std::string device_id = "load-device-" +
                                  std::to_string(client_index + 1);
    const std::uint64_t session_id =
        static_cast<std::uint64_t>(client_index + 1) << 32U;
    std::uint32_t sequence = 1;

    const edgelink::Message hello{
        edgelink::MessageType::hello,
        sequence++,
        edgelink::encode_hello({session_id, device_id})};

    if (!send_message_and_wait(socket_fd, hello)) {
        ::close(socket_fd);
        return;
    }

    for (int sample = 0; sample < settings.messages_per_client; ++sample) {
        const auto timestamp =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count();
        const edgelink::TelemetryPayload reading{
            static_cast<std::uint64_t>(timestamp),
            static_cast<std::int16_t>(2000 + client_index % 100),
            static_cast<std::uint16_t>(5000 + sample % 100),
            3300,
            0};
        const edgelink::Message telemetry{
            edgelink::MessageType::telemetry,
            sequence++,
            edgelink::encode_telemetry(reading)};

        if (!send_message_and_wait(socket_fd, telemetry)) {
            ::close(socket_fd);
            return;
        }
        ++acknowledged_messages;
    }

    ++successful_clients;
    ::close(socket_fd);
}

} // namespace

int main(int argc, char** argv) {
    std::signal(SIGPIPE, SIG_IGN);

    Settings settings;
    if (!parse_settings(argc, argv, settings)) {
        std::cerr << "Usage: load_generator [host] [port] [clients] "
                     "[messages_per_client]\n";
        return 2;
    }

    std::atomic_int ready_clients{0};
    std::atomic_bool start{false};
    std::atomic_int successful_clients{0};
    std::atomic<std::uint64_t> acknowledged_messages{0};
    std::vector<std::thread> workers;
    workers.reserve(static_cast<std::size_t>(settings.clients));

    for (int index = 0; index < settings.clients; ++index) {
        workers.emplace_back(
            run_client,
            std::cref(settings),
            index,
            std::ref(ready_clients),
            std::ref(start),
            std::ref(successful_clients),
            std::ref(acknowledged_messages));
    }

    const auto ready_deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (ready_clients.load() < settings.clients &&
           std::chrono::steady_clock::now() < ready_deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    const auto started_at = std::chrono::steady_clock::now();
    start.store(true, std::memory_order_release);
    for (auto& worker : workers) {
        worker.join();
    }
    const auto elapsed = std::chrono::steady_clock::now() - started_at;
    const double elapsed_seconds =
        std::chrono::duration<double>(elapsed).count();
    const auto messages = acknowledged_messages.load();
    const double messages_per_second =
        elapsed_seconds > 0.0 ? messages / elapsed_seconds : 0.0;

    std::cout << "Load test complete\n"
              << "clients=" << settings.clients
              << " successful_clients=" << successful_clients.load() << '\n'
              << "messages_per_client=" << settings.messages_per_client
              << " acknowledged_messages=" << messages << '\n'
              << "elapsed_seconds=" << elapsed_seconds << '\n'
              << "messages_per_second=" << messages_per_second << '\n';

    return successful_clients.load() == settings.clients ? 0 : 1;
}
