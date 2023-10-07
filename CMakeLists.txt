cmake_minimum_required(VERSION 3.10)

# 设置项目名称
project(PnxNetworkStack C CXX)

set(CMAKE_CXX_STANDARD 17)





# 设置编译选项
# 开启所有警告
set(CMAKE_CXX_FLAGS "-Wall -Wextra")

# Debug构建类型
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    # 添加调试相关的编译选项
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
endif()

# Release构建类型
if (CMAKE_BUILD_TYPE STREQUAL "Release")
    # 添加发布相关的编译选项
    set(CMAKE_CXX_FLAGS_RELEASE "-O3")
endif()

# 添加includes目录
include_directories(src/includes thirdparty/includes)

# Find and link pthread
find_package(Threads REQUIRED)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")

# add LDFLAGS
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--wrap=socket -Wl,--wrap=bind -Wl,--wrap=listen -Wl,--wrap=connect -Wl,--wrap=accept -Wl,--wrap=read -Wl,--wrap=write -Wl,--wrap=close -Wl,--wrap=getaddrinfo -Wl,--wrap=setsockopt")


add_subdirectory(src)
add_subdirectory(tests)
message("${TARGET_TO_LINK}")