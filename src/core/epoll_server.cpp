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
    LOG_INFO("EpollServer initialized. Listening on port " + std::to_string(port_));
}

void EpollServer::addFd(int fd, uint32_t events) {
    epoll_event event{};
    event.data.fd = fd;
    event.events = events;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &event) == -1) {
        perror("epoll_ctl_add failed");
        LOG_ERROR("Failed to add fd " + std::to_string(fd) + " to epoll.");
    }
}

void EpollServer::updateFd(int fd, uint32_t events) {
    epoll_event event{};
    event.data.fd = fd;
    event.events = events;
    // event.events = events | EPOLLET | EPOLLONESHOT; // 保持边缘触发和 EPOLLONESHOT
    // event.events = events | EPOLLET; // 保持边缘触发
    
    // 使用 EPOLL_CTL_MOD 操作来修改文件描述符的事件
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &event) == -1) {
        // 错误处理，可以打印日志
    }
}

void EpollServer::removeFd(int fd) {
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) == -1) {
        perror("epoll_ctl_del failed");
        LOG_ERROR("Failed to remove fd " + std::to_string(fd) + " from epoll.");
    }
}

// 设置定时器
void EpollServer::setTimer(HeapTimer* timer) {
    timer_ = timer;
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

        int timeout_ms = (timer_ != nullptr) ? timer_->getNextTick() : -1;
        int nfds = epoll_wait(epoll_fd_, events, MAX_EVENTS, timeout_ms);  // 有一个 timeout 参数，决定了最长阻塞的时间
        if (nfds == -1) {
            perror("epoll_wait failed");
            LOG_ERROR("epoll_wait failed, shutting down server.");
            break;
        }

        for (int i = 0; i < nfds; ++i) {
            int fd = events[i].data.fd;
            uint32_t event_type = events[i].events;

            if (fd == listen_fd_) {
                // 处理新连接
                if (connection_callback_) {
                    connection_callback_(listen_fd_);
                }
            } else {
                // 处理错误和断开事件
                if (event_type & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                    if (close_callback_) {
                        LOG_INFO("Client[" + std::to_string(fd) + "] is closed due to EPOLLERR | EPOLLRDHUP | EPOLLHUP.");
                        close_callback_(fd);
                    }
                }

                // 处理可读事件
                if (event_type & EPOLLIN) {
                    if (read_callback_) {
                        read_callback_(fd);
                    }
                }

                // 处理可写事件
                if (event_type & EPOLLOUT) {
                    if (write_callback_) {
                        write_callback_(fd);
                    }
                }
            }
        }

        // 连接超时关闭
        if (timer_ != nullptr) {
            std::vector<int> expired_fds;
            timer_->tick(expired_fds);
            for (int fd : expired_fds) {
                // 调用回调函数来关闭连接
                if (close_callback_) {
                    LOG_INFO("Client[" + std::to_string(fd) + "] is closed due to timeout.");
                    close_callback_(fd);
                }
            }
        }
    }
}