#include "webserver/core/epoll_server.hpp"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include "webserver/utils/log.hpp"

constexpr int MAX_EVENTS = 1024;

EpollServer::EpollServer(int port) : port_(port), listen_fd_(-1), epoll_fd_(-1) {
    initSocket();
}

EpollServer::~EpollServer() {
    if (listen_fd_ >= 0) {
        close(listen_fd_);
    }
    if (epoll_fd_ >= 0) {
        close(epoll_fd_);
    }
}

// 设置文件描述符非阻塞
void EpollServer::setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void EpollServer::initSocket() {
    listen_fd_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (listen_fd_ == -1) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listen_fd_, (sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(listen_fd_, SOMAXCONN) == -1) {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }

    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ == -1) {
        perror("epoll_create failed");
        exit(EXIT_FAILURE);
    }

    addFd(listen_fd_, EPOLLIN | EPOLLET);
    Logger::getInstance().log("INFO", "EpollServer initialized. Listening on port " + std::to_string(port_));
}

void EpollServer::addFd(int fd, uint32_t events) {
    epoll_event event{};
    event.data.fd = fd;
    event.events = events;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &event) == -1) {
        perror("epoll_ctl_add failed");
        Logger::getInstance().log("ERROR", "Failed to add fd " + std::to_string(fd) + " to epoll.");
    }
}

void EpollServer::removeFd(int fd) {
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) == -1) {
        perror("epoll_ctl_del failed");
        Logger::getInstance().log("ERROR", "Failed to remove fd " + std::to_string(fd) + " from epoll.");
    }
}

void EpollServer::setConnectionCallback(EventCallback callback) {
    connection_callback_ = std::move(callback);
}

void EpollServer::setReadCallback(EventCallback callback) {
    read_callback_ = std::move(callback);
}

void EpollServer::setWriteCallback(EventCallback callback) {
    write_callback_ = std::move(callback);
}

void EpollServer::setCloseCallback(EventCallback callback) {
    close_callback_ = std::move(callback);
}

void EpollServer::run() {
    epoll_event events[MAX_EVENTS];
    while (true) {
        int nfds = epoll_wait(epoll_fd_, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            perror("epoll_wait failed");
            Logger::getInstance().log("ERROR", "epoll_wait failed, shutting down server.");
            break;
        }

        for (int i = 0; i < nfds; ++i) {
            int fd = events[i].data.fd;
            uint32_t event_type = events[i].events;

            if (fd == listen_fd_) {
                if (connection_callback_) {
                    connection_callback_(listen_fd_);
                }
            } else if (event_type & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                if (close_callback_) {
                    close_callback_(fd);
                }
            } else if (event_type & EPOLLIN) {
                if (read_callback_) {
                    read_callback_(fd);
                }
            } else if (event_type & EPOLLOUT) {
                if (write_callback_) {
                    write_callback_(fd);
                }
            }
        }
    }
}