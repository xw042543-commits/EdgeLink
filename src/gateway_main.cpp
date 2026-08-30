#include "edgelink/device_registry.hpp"
#include "edgelink/protocol.hpp"

#include <poll.h>
#include <arpa/inet.h>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <span>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace {

std::atomic_bool running{true};

void stop_server(int) {
    running = false;
}

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

std::string peer_name(const sockaddr_in& address) {
    char ip[INET_ADDRSTRLEN]{};
    inet_ntop(AF_INET, &address.sin_addr, ip, sizeof(ip));
    return std::string(ip) + ":" + std::to_string(ntohs(address.sin_port));
}

void handle_client(int client_socket, sockaddr_in address, edgelink::DeviceRegistry& registry) {
    const auto peer = peer_name(address);
    std::string device_id;
    edgelink::StreamParser parser;
    std::uint8_t buffer[2048];
    pollfd client_connection{client_socket, POLLIN, 0};
    std::cout << "[connection] accepted peer=" << peer << '\n';

    while (running) {
        const int ready = ::poll(&client_connection, 1, 15000);

        if (ready == 0) {
            std::cerr << "[timeout] no data from peer=" << peer << '\n';
            break;
        }

        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "[poll] peer=" << peer
                      << " error=" << std::strerror(errno) << '\n';
            break;
        }

        const auto count = ::recv(client_socket, buffer, sizeof(buffer), 0);
        if (count == 0) {
            break;
        }
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        for (const auto& message : parser.push(std::span(buffer, static_cast<std::size_t>(count)))) {
            if (message.type == edgelink::MessageType::hello) {
                const auto hello = edgelink::decode_hello(message.payload);
                if (!hello) {
                    std::cerr << "[protocol] invalid device id peer=" << peer << '\n';
                    break;
                }
                device_id = hello->device_id;
                registry.connected(device_id, peer);
                std::cout << "[device] online id=" << device_id << " peer=" << peer << '\n';
            } else if (device_id.empty()) {
                std::cerr << "[protocol] HELLO required peer=" << peer << '\n';
                continue;
            } else {
                registry.seen(device_id);
                if (message.type == edgelink::MessageType::telemetry) {
                    if (const auto data = edgelink::decode_telemetry(message.payload)) {
                        std::cout << "[telemetry] id=" << device_id
                                  << " seq=" << message.sequence
                                  << " temperature=" << data->temperature_centi_c / 100.0
                                  << "C humidity=" << data->humidity_centi_pct / 100.0 << "%\n";
                    }
                }
            }

            edgelink::Message ack{edgelink::MessageType::acknowledgment, message.sequence, {}};
            if (!send_all(client_socket, edgelink::encode(ack))) {
                break;
            }
        }
    }

    if (!device_id.empty()) {
        registry.disconnected(device_id);
        std::cout << "[device] offline id=" << device_id << '\n';
    }
    ::close(client_socket);
}

int parse_port(int argc, char** argv) {
    if (argc < 2) {
        return 9000;
    }
    try {
        const auto port = std::stoi(argv[1]);
        if (port < 1 || port > 65535) {
            throw std::out_of_range("port");
        }
        return port;
    } catch (...) {
        std::cerr << "Usage: edgelink_gateway [port]\n";
        return -1;
    }
}

} // namespace

int main(int argc, char** argv) {
    const int port = parse_port(argc, argv);
    if (port < 0) {
        return 2;
    }

    std::signal(SIGINT, stop_server);
    std::signal(SIGTERM, stop_server);
    std::signal(SIGPIPE, SIG_IGN);

    const int server_socket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        std::cerr << "socket: " << std::strerror(errno) << '\n';
        return 1;
    }

    int reuse = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in server_address{};
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(static_cast<std::uint16_t>(port));

    if (::bind(server_socket, reinterpret_cast<sockaddr*>(&server_address), sizeof(server_address)) < 0 ||
        ::listen(server_socket, 64) < 0) {
        std::cerr << "listen: " << std::strerror(errno) << '\n';
        ::close(server_socket);
        return 1;
    }

    edgelink::DeviceRegistry registry;
    std::cout << "EdgeLink gateway listening on 0.0.0.0:" << port << '\n';
    std::cout << "Press Ctrl+C to stop.\n";

    pollfd listener{server_socket, POLLIN, 0};
    while (running) {
        const int ready = ::poll(&listener, 1, 500);
        if (ready <= 0) {
            continue;
        }
        sockaddr_in client_address{};
        socklen_t address_size = sizeof(client_address);
        const int client = ::accept(server_socket, reinterpret_cast<sockaddr*>(&client_address), &address_size);
        if (client < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "accept: " << std::strerror(errno) << '\n';
            break;
        }
        std::thread(handle_client, client, client_address, std::ref(registry)).detach();
    }

    ::close(server_socket);
    std::cout << "EdgeLink gateway stopped.\n";
}
