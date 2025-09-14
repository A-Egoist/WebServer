# apps/qr_app/CMakeLists.txt

# 将 qr_processor.cpp 编译成一个库
add_library(qr_processor qr_processor.cpp)

# 链接依赖库
target_link_libraries(qr_processor PRIVATE ${OpenCV_LIBS})

# 设置头文件路径
target_include_directories(qr_processor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})