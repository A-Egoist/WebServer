# WebServer


## TODO

### 基本要求

- [x] 连接数据库，存储用户账号和密码
    - [ ] 应该保证 username 不能重复，让 username 成为 key

- [x] 完成注册和登录功能，登录成功的用户应该返回 `welcome.html` 界面
- [x] 完成日志功能
- [ ] 完成连接池
- [ ] 完成线程池
- [ ] 完成定时器，关闭非活动连接
    - [ ] 目前对 HTTP 请求的响应都带有 `"Connection: close\r\n\r\n"`，这样的频繁的连接、断开连接、再连接的过程非常耗费资源，下一步应该添加定时器以关闭长时间没有使用的连接。
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
webbench -c 1000 -t 30 http://127.0.0.1:8080/
```



## Version

### Version 0.1

[Link]()

已完成 WebServer 的基础功能，包括：

*   处理 GET 和 POST 请求，返回对应的静态内容；
*   连接 MySQL 数据库，支持用户的注册和登录；
*   加入异步日志系统，有助于调试BUG，定位错误，数据分析；
*   完成一次 `webbench-1.5` 服务器压力测试，完成时间：2025-6-18 21:20:55
    ![image-20250618212041657](https://amonologue-image-bed.oss-cn-chengdu.aliyuncs.com/2025/202506182120838.png)

