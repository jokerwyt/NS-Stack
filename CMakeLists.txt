cmake_minimum_required(VERSION 3.10)

# 设置项目名称
project(PnxNetworkStack C)

# 设置C标准为C11
set(CMAKE_C_STANDARD 11)





# 设置编译选项
# 开启所有警告
set(CMAKE_C_FLAGS "-Wall -Wextra -Wpedantic")

# Debug构建类型
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    # 添加调试相关的编译选项
    set(CMAKE_C_FLAGS_DEBUG "-g -O0")
endif()

# Release构建类型
if (CMAKE_BUILD_TYPE STREQUAL "Release")
    # 添加发布相关的编译选项
    set(CMAKE_C_FLAGS_RELEASE "-O3")
endif()

# 添加includes目录
include_directories(src/includes)

# Find and link pthread
find_package(Threads REQUIRED)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pthread")


add_subdirectory(src)
add_subdirectory(tests)