cmake_minimum_required(VERSION 3.10)  # 设置CMake最低版本要求
project(WebServer)  # 定义项目名称

# 设置C++标准（根据你的项目需求调整）
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_BUILD_TYPE Debug)
# set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找MySQL Connector/C++库
# find_package(MySQL REQUIRED COMPONENTS cppconn)
find_package(Threads REQUIRED)

# 添加头文件搜索路径（关键步骤！）
include_directories(${PROJECT_SOURCE_DIR}/http)
include_directories(${PROJECT_SOURCE_DIR}/sql)
include_directories(${PROJECT_SOURCE_DIR}/log)
include_directories(${PROJECT_SOURCE_DIR}/timer)
include_directories(${PROJECT_SOURCE_DIR}/pool)

# 添加可执行文件
add_executable(webserver main.cpp server.cpp http/http_request.cpp http/HTTPConnection.cpp sql/MySQLConnector.cpp log/log.cpp timer/heaptimer.cpp pool/ThreadPool.cpp)

target_link_libraries(webserver PRIVATE mysqlcppconn)
target_link_libraries(webserver PRIVATE Threads::Threads)

# MySQL连接测试
# add_executable(mysql_test mysql_test.cpp)
# target_link_libraries(mysql_test PRIVATE mysqlcppconn)
