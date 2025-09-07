cmake_minimum_required(VERSION 3.10)

# 项目名称
project(WebServer VERSION 0.8 LANGUAGES CXX)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置构建类型为 Debug (或者 Release)
set(CMAKE_BUILD_TYPE Debug)

# 查找并连接所需的第三方库
find_package(Threads REQUIRED)
find_package(fmt REQUIRED)
# find_package(MySQL REQUIRED COMPONENTS cppconn)

# 定义头文件搜索路径
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/webserver
)

# 定义项目源文件
# 使用 file(GLOB ...) 自动查找源文件，避免手动添加
# 这样当你添加新的 .cpp 文件时，就不需要修改 CMakeLists.txt 了
file(GLOB_RECURSE SOURCES
    "src/*.cpp"
    "src/*/*.cpp"
)

# 添加主可执行程序
add_executable(${PROJECT_NAME} ${SOURCES} main.cpp)

# 链接依赖库
target_link_libraries(${PROJECT_NAME} PRIVATE
    Threads::Threads
    fmt::fmt
    mysqlcppconn
)

# 添加测试目标
# 查找 tests 目录下的所有 .cpp 文件
# file(GLOB_RECURSE TEST_SOURCES
#     "tests/*.cpp"
#     "tests/*/*.cpp"
# )

# 添加测试可执行文件
# add_executable(run_tests ${TEST_SOURCES})

# 链接测试依赖库
# target_link_libraries(run_tests PRIVATE
#     Threads::Threads
#     mysqlcppconn
#     fmt::fmt
# )

# 可选：如果你的测试使用了 Google Test，可以添加以下配置
# find_package(GTest REQUIRED)
# target_link_libraries(run_tests PRIVATE GTest::GTest)
# include(GoogleTest)
# gtest_discover_tests(run_tests)