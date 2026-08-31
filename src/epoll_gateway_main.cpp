#include <cerrno>
#include <cstring>
#include <iostream>
#include <sys/epoll.h>
#include <sys/epoll.h>
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

    std::cout << "epoll instance created: fd=" << epoll_fd << '\n';
    std::cout << "server socket created: fd=" << server_socket << '\n';

    ::close(server_socket);
    ::close(epoll_fd);
    
    

    
}
