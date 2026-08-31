#include <cerrno>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

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

    std::cout << "epoll instance created: fd=" << epoll_fd << '\n';
    std::cout << "server socket listening on 0.0.0.0:"
              << port << " fd=" << server_socket << '\n';

    ::close(server_socket);
    ::close(epoll_fd);
}
