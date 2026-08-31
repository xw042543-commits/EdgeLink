#include <cerrno>
#include <cstring>
#include <iostream>
#include <sys/epoll.h>
#include <unistd.h>

int main() {
    const int epoll_fd = ::epoll_create1(0);

    if (epoll_fd < 0) {
        std::cerr << "epoll_create1: " << std::strerror(errno) << '\n';
        return 1;
    }

    std::cout << "epoll instance created: fd=" << epoll_fd << '\n';

    ::close(epoll_fd);
}
