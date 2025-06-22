# WebServer


## TODO

### 基本要求

- [x] 连接数据库，存储用户账号和密码
    - [ ] 应该保证 username 不能重复，让 username 成为 key

- [x] 完成注册和登录功能，登录成功的用户应该返回 `welcome.html` 界面
- [x] 完成日志功能
- [ ] 完成连接池
    - [ ] TCP 连接池

- [ ] 完成线程池
    - [ ] 工作线程池

- [x] 完成定时器，关闭非活动连接
    - [x] 目前对 HTTP 请求的响应都带有 `"Connection: close\r\n\r\n"`，这样的频繁的连接、断开连接、再连接的过程非常耗费资源，下一步应该添加定时器以关闭长时间没有使用的连接。
    - [ ] 已经成功添加定时器，但是在压力测试后之后发现，性能大幅度降低，现在需要分析性能降低的原因并解决这个问题。

- [ ] `webbench-1.5` 服务器压力测试
- [ ] 部署到腾讯云

### 额外功能

- [ ] 实现聊天服务器的功能，参考[GitHub](https://github.com/archibate/co_http)，考虑在现有WebServer的基础上增加聊天室的功能
- [ ] 实现音乐播放器的功能



## Notes

编译并运行 WebServer

```bash
mkdir build
cd build
cmake ..
make
./webserver
```

服务器压力测试

```bash
cd webbench-1.5
make
./webbench -c 1000 -t 5 http://127.0.0.1:8080/
```



## Version

### Version 0.5

[Link]()

已完成 WebServer 的基础功能，包括：

*   处理 GET 和 POST 请求，返回对应的静态内容；
*   连接 MySQL 数据库，支持用户的注册和登录；
*   加入异步日志系统，有助于调试BUG，定位错误，数据分析；
*   持久连接，将 Connection 设置为 keep-alive，节约 TCP 连接的开销
*   添加 Timer 控制连接时间
*   拓展为多线程 Reactor 模型，但是目前没有解决多线程在使用 webbench 压力测试的时候崩溃的问题



### Version 0.4

[Link](https://github.com/A-Egoist/WebServer/tree/8750b027ba9acd0f8eb961842a216ff6814ecc82)

已完成 WebServer 的基础功能，包括：

*   处理 GET 和 POST 请求，返回对应的静态内容；
*   连接 MySQL 数据库，支持用户的注册和登录；
*   加入异步日志系统，有助于调试BUG，定位错误，数据分析；
*   持久连接，将 Connection 设置为 keep-alive，节约 TCP 连接的开销
*   添加 Timer 控制连接时间
*   `webbench-1.5` 服务器压力测试，完成时间：2025-6-21 19:33:44
    ![image-20250621193406828](https://amonologue-image-bed.oss-cn-chengdu.aliyuncs.com/2025/202506211934008.png)

测试是否 keep-alive：

```bash
curl -v --http1.1 http://127.0.0.1:8080/ http://127.0.0.1:8080/picture http://127.0.0.1:8080/login http://127.0.0.1:8080/welcome > http11NUL
```

```bash
curl -v --http1.0 http://127.0.0.1:8080/ http://127.0.0.1:8080/picture http://127.0.0.1:8080/login http://127.0.0.1:8080/welcome > http10NUL
```

```bash
curl -v --header "Connection: close" http://127.0.0.1:8080

curl -v --header "Connection: keep-alive" http://127.0.0.1:8080
```



### Version 0.3

[Link](https://github.com/A-Egoist/WebServer/tree/5a25bb874cd17e16a1e446188b69a10c97e098b7)

已完成 WebServer 的基础功能，包括：

*   处理 GET 和 POST 请求，返回对应的静态内容；
*   连接 MySQL 数据库，支持用户的注册和登录；
*   加入异步日志系统，有助于调试BUG，定位错误，数据分析；
*   完成一次 `webbench-1.5` 服务器压力测试，完成时间：2025-6-18 21:20:55
    ![image-20250618212041657](https://amonologue-image-bed.oss-cn-chengdu.aliyuncs.com/2025/202506182120838.png)

### Version 0.2

[Link](https://github.com/A-Egoist/WebServer/tree/11830bcd8217816af960a1f5e7cb15d7dda5bdcc)

已完成 WebServer 的基础功能，包括：

*   处理 GET 和 POST 请求，返回对应的静态内容；
*   连接 MySQL 数据库，支持用户的注册和登录；

### Version 0.1

[Link](https://github.com/A-Egoist/WebServer/tree/4dbadc7e63c5665b921c8cce4055bf8d54db595f)

已完成 WebServer 的基础功能，包括：

*   处理 GET 和 POST 请求，返回对应的静态内容；