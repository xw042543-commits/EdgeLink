#include "edgelink/protocol.hpp"

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <span>
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unordered_map>
#include <unistd.h>
#include <vector>

bool set_nonblocking(int socket_fd) {
    const int flags = ::fcntl(socket_fd, F_GETFL, 0);

    if (flags < 0) {
        return false;
    }

    return ::fcntl(
               socket_fd,
               F_SETFL,
               flags | O_NONBLOCK) == 0;
}

enum class FlushResult {
    complete,
    would_block,
    error,
};

FlushResult flush_pending(
    int socket_fd,
    std::vector<std::uint8_t>& pending) {
    std::size_t sent = 0;

    while (sent < pending.size()) {
        const auto count = ::send(
            socket_fd,
            pending.data() + sent,
            pending.size() - sent,
            MSG_NOSIGNAL);

        if (count > 0) {
            sent += static_cast<std::size_t>(count);
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else if (count < 0 &&
                   (errno == EAGAIN || errno == EWOULDBLOCK)) {
            pending.erase(pending.begin(), pending.begin() + sent);
            return FlushResult::would_block;
        } else {
            return FlushResult::error;
        }
    }

    pending.clear();
    return FlushResult::complete;
}

bool update_client_events(int epoll_fd, int socket_fd, bool want_write) {
    epoll_event event{};
    event.events = EPOLLIN;
    if (want_write) {
        event.events |= EPOLLOUT;
    }
    event.data.fd = socket_fd;

    return ::epoll_ctl(
               epoll_fd,
               EPOLL_CTL_MOD,
               socket_fd,
               &event) == 0;
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
    if (!set_nonblocking(server_socket)) {
        std::cerr << "set_nonblocking: "
                  << std::strerror(errno) << '\n';
        ::close(server_socket);
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
    std::unordered_map<int, std::vector<std::uint8_t>> pending_writes;

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
            const auto& event = events[static_cast<std::size_t>(index)];
            const int event_fd = event.data.fd;
            const std::uint32_t event_flags = event.events;

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
                if (!set_nonblocking(client_socket)) {
                    std::cerr << "set_nonblocking client: "
                              << std::strerror(errno) << '\n';
                    ::close(client_socket);
                    continue;
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
                pending_writes.try_emplace(client_socket);
            } else {
                if ((event_flags & EPOLLIN) != 0) {
                std::array<std::uint8_t, 2048> buffer{};

                const auto count = ::recv(
                    event_fd,
                    buffer.data(),
                    buffer.size(),
                    0);

                if (count == 0) {
                    std::cout << "client disconnected: fd="
                              << event_fd << '\n';
                    parsers.erase(event_fd);
                    device_ids.erase(event_fd);
                    pending_writes.erase(event_fd);
                    ::close(event_fd);
                    continue;
                } else if (count < 0 &&
                           errno != EAGAIN && errno != EWOULDBLOCK) {
                    std::cerr << "recv error: fd="
                              << event_fd << " error="
                              << std::strerror(errno) << '\n';
                    parsers.erase(event_fd);
                    device_ids.erase(event_fd);
                    pending_writes.erase(event_fd);
                    ::close(event_fd);
                    continue;
                } else if (count > 0) {
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

                        if (message.type == edgelink::MessageType::hello) {
                            const auto hello =
                                edgelink::decode_hello(message.payload);

                            if (hello.has_value()) {
                                device_ids[event_fd] = hello->device_id;

                                std::cout << "device online: id="
                                          << hello->device_id
                                          << " fd=" << event_fd << '\n';
                            }
                        } else if (message.type ==
                                   edgelink::MessageType::telemetry) {
                            const auto reading =
                                edgelink::decode_telemetry(message.payload);
                            const auto device = device_ids.find(event_fd);

                            if (reading.has_value() &&
                                device != device_ids.end()) {
                                std::cout << "telemetry: id="
                                          << device->second
                                          << " temperature="
                                          << reading->temperature_centi_c / 100.0
                                          << "C humidity="
                                          << reading->humidity_centi_pct / 100.0
                                          << "%\n";
                            }
                        }

                        const edgelink::Message ack{
                            edgelink::MessageType::acknowledgment,
                            message.sequence,
                            {}};

                        const auto ack_frame = edgelink::encode(ack);

                        auto& pending = pending_writes.at(event_fd);

                        pending.insert(
                            pending.end(),
                            ack_frame.begin(),
                            ack_frame.end());

                        std::cout << "ACK queued: seq="
                                  << message.sequence
                                  << " pending_bytes="
                                  << pending.size() << '\n';
                    }
                }
                }

                auto& pending = pending_writes.at(event_fd);
                if (!pending.empty()) {
                    const auto result = flush_pending(event_fd, pending);
                    if (result == FlushResult::error) {
                        std::cerr << "send error: fd="
                                  << event_fd << " error="
                                  << std::strerror(errno) << '\n';
                        parsers.erase(event_fd);
                        device_ids.erase(event_fd);
                        pending_writes.erase(event_fd);
                        ::close(event_fd);
                        continue;
                    } else if (result == FlushResult::would_block) {
                        std::cout << "ACK waiting for EPOLLOUT: fd="
                                  << event_fd
                                  << " pending_bytes=" << pending.size()
                                  << '\n';
                    } else {
                        std::cout << "ACK queue flushed: fd="
                                  << event_fd << '\n';
                    }
                }

                if (!update_client_events(
                        epoll_fd, event_fd, !pending.empty())) {
                    std::cerr << "epoll_ctl update: fd="
                              << event_fd << " error="
                              << std::strerror(errno) << '\n';
                    parsers.erase(event_fd);
                    device_ids.erase(event_fd);
                    pending_writes.erase(event_fd);
                    ::close(event_fd);
                }
            }
        }
    }

    ::close(server_socket);
    ::close(epoll_fd);
}
