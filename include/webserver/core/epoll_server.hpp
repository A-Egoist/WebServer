#pragma once

#include <sys/epoll.h>
#include <functional>
#include <unordered_map>
#include "webserver/timer/heaptimer.hpp"

// 回调函数类型定义
// 当有新连接或数据可读时，EpollServer会调用这些函数
using EventCallback = std::function<void(int)>;

class EpollServer {
private:
    int port_;
    int listen_fd_;
    int epoll_fd_;  // epoll 实例
    HeapTimer* timer_;  // 定时器

    void initSocket();

    EventCallback connection_callback_;
    EventCallback read_callback_;
    EventCallback write_callback_;
    EventCallback close_callback_;

public:
    EpollServer(int port);
    ~EpollServer();

    void run();

    // 注册定时器
    void setTimer(HeapTimer* timer);

    // 注册回调函数
    void setConnectionCallback(EventCallback callback);
    void setReadCallback(EventCallback callback);
    void setWriteCallback(EventCallback callback);
    void setCloseCallback(EventCallback callback);

    // 供外部调用的 Epoll 相关接口
    void addFd(int fd, uint32_t events);
    void updateFd(int fd, uint32_t events);
    void removeFd(int fd);
};