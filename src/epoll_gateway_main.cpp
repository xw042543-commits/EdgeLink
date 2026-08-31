#include "edgelink/protocol.hpp"

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <span>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unordered_map>
#include <unistd.h>
#include <string>

bool send_all(int socket_fd, std::span<const std::uint8_t> bytes) {
    std::size_t sent = 0;

    while (sent < bytes.size()) {
        const auto count = ::send(
            socket_fd,
            bytes.data() + sent,
            bytes.size() - sent,
            MSG_NOSIGNAL);

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

int main() {
    const int epoll_fd = ::epoll_create1(0);

    if (epoll_fd < 0) {
        std::cerr << "epoll_create1: " << std::strerror(errno) << '\n';
        return 1;
    }

    const int server_socket = ::socket(AF_INET, SOCK_STREAM, 0);

    if (server_socket < 0) {
        std::cerr << "socket: " << std::strerror(errno) << '\n';
        ::close(epoll_fd);
        return 1;
    }

    constexpr int port = 9040;

    int reuse = 1;
    if (::setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR,
                     &reuse, sizeof(reuse)) < 0) {
        std::cerr << "setsockopt: " << std::strerror(errno) << '\n';
        ::close(server_socket);
        ::close(epoll_fd);
        return 1;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(static_cast<std::uint16_t>(port));

    if (::bind(server_socket,
               reinterpret_cast<sockaddr*>(&address),
               sizeof(address)) < 0 ||
        ::listen(server_socket, 64) < 0) {
        std::cerr << "bind/listen: " << std::strerror(errno) << '\n';
        ::close(server_socket);
        ::close(epoll_fd);
        return 1;
    }

    epoll_event listener_event{};
    listener_event.events = EPOLLIN;
    listener_event.data.fd = server_socket;

    if (::epoll_ctl(epoll_fd,
                    EPOLL_CTL_ADD,
                    server_socket,
                    &listener_event) < 0) {
        std::cerr << "epoll_ctl: " << std::strerror(errno) << '\n';
        ::close(server_socket);
        ::close(epoll_fd);
        return 1;
    }

    std::cout << "epoll instance created: fd=" << epoll_fd << '\n';
    std::cout << "server socket listening on 0.0.0.0:"
              << port << " fd=" << server_socket << '\n';
    std::cout << "listener registered with epoll\n";

    std::array<epoll_event, 16> events{};
    std::unordered_map<int, edgelink::StreamParser> parsers;
    std::unordered_map<int, std::string> device_ids;

    while (true) {
        const int ready = ::epoll_wait(
            epoll_fd,
            events.data(),
            static_cast<int>(events.size()),
            -1);

        if (ready < 0) {
            std::cerr << "epoll_wait: " << std::strerror(errno) << '\n';
            ::close(server_socket);
            ::close(epoll_fd);
            return 1;
        }

        for (int index = 0; index < ready; ++index) {
            const int event_fd =
                events[static_cast<std::size_t>(index)].data.fd;

            std::cout << "event ready on fd=" << event_fd << '\n';

            if (event_fd == server_socket) {
                const int client_socket =
                    ::accept(server_socket, nullptr, nullptr);

                if (client_socket < 0) {
                    std::cerr << "accept: " << std::strerror(errno) << '\n';
                    ::close(server_socket);
                    ::close(epoll_fd);
                    return 1;
                }

                std::cout << "accepted client socket: fd="
                          << client_socket << '\n';

                epoll_event client_event{};
                client_event.events = EPOLLIN;
                client_event.data.fd = client_socket;

                if (::epoll_ctl(epoll_fd,
                                EPOLL_CTL_ADD,
                                client_socket,
                                &client_event) < 0) {
                    std::cerr << "epoll_ctl client: "
                              << std::strerror(errno) << '\n';
                    ::close(client_socket);
                    ::close(server_socket);
                    ::close(epoll_fd);
                    return 1;
                }

                std::cout << "client registered with epoll: fd="
                          << client_socket << '\n';
                parsers.try_emplace(client_socket);
            } else {
                std::array<std::uint8_t, 2048> buffer{};

                const auto count = ::recv(
                    event_fd,
                    buffer.data(),
                    buffer.size(),
                    0);

                if (count <= 0) {
                    std::cout << "client disconnected: fd="
                              << event_fd << '\n';
                    parsers.erase(event_fd);
                    device_ids.erase(event_fd);
                    ::close(event_fd);
                } else {
                    std::cout << "received " << count
                              << " bytes from fd=" << event_fd << '\n';
                    auto& parser = parsers.at(event_fd);

                    const auto messages = parser.push(std::span(
                        buffer.data(),
                        static_cast<std::size_t>(count)));

                    for (const auto& message : messages) {
                        std::cout << "parsed message: type="
                                  << edgelink::message_type_name(message.type)
                                  << " seq=" << message.sequence << '\n';
                                  if (message.type == edgelink::MessageType::telemetry) {
    const auto reading =
        edgelink::decode_telemetry(message.payload);

    if (reading.has_value()) {
        if (message.type == edgelink::MessageType::hello) {
    const auto hello = edgelink::decode_hello(message.payload);

    if (hello.has_value()) {
        device_ids[event_fd] = hello->device_id;

        std::cout << "device online: id="
                  << hello->device_id
                  << " fd=" << event_fd << '\n';
    }
}
        std::cout << "temperature="
                  << reading->temperature_centi_c / 100.0
                  if (message.type == edgelink::MessageType::telemetry) {
    const auto reading =
        edgelink::decode_telemetry(message.payload);
    const auto device = device_ids.find(event_fd);

    if (reading.has_value() && device != device_ids.end()) {
        std::cout << "telemetry: id="
                  << device->second
                  << " temperature="
                  << reading->temperature_centi_c / 100.0
                  << "C humidity="
                  << reading->humidity_centi_pct / 100.0
                  << "%\n";
    }
}
    }
}

                        const edgelink::Message ack{
                            edgelink::MessageType::acknowledgment,
                            message.sequence,
                            {}};

                        const auto ack_frame = edgelink::encode(ack);

                        if (send_all(event_fd, ack_frame)) {
                            std::cout << "ACK sent: seq="
                                      << message.sequence << '\n';
                        }
                    }
                }
            }
        }
    }

    ::close(server_socket);
    ::close(epoll_fd);
}
