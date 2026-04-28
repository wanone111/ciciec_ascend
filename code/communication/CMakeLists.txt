cmake_minimum_required(VERSION 3.16)                                            # 指定 CMake 最低版本要求

project(udp_ack_demo LANGUAGES CXX)                                             # 定义项目名称和语言

set(CMAKE_CXX_STANDARD 17)                                                      # 设置 C++ 标准为 C++17
set(CMAKE_CXX_STANDARD_REQUIRED ON)                                             # 要求必须支持指定标准
set(CMAKE_CXX_EXTENSIONS OFF)                                                   # 关闭编译器私有扩展，保证可移植性

# 查找 pybind11
find_package(pybind11 CONFIG QUIET)
if(NOT pybind11_FOUND)
    message(STATUS "pybind11 not found, will try to fetch it")
    include(FetchContent)
    FetchContent_Declare(
        pybind11
        GIT_REPOSITORY https://github.com/pybind/pybind11.git
        GIT_TAG v2.11.1
    )
    FetchContent_MakeAvailable(pybind11)
endif()

add_executable(pc_udp src/pc_udp.cpp)                                           # 添加 PC 端可执行文件
target_include_directories(pc_udp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)  # 添加头文件搜索路径

add_executable(310B_udp src/310B_udp.cpp)                                       # 添加 310B 端可执行文件
target_include_directories(310B_udp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include) # 添加头文件搜索路径

add_executable(pynq_udp src/pynq_udp.cpp)                                       # 添加 PYNQ 端可执行文件
target_include_directories(pynq_udp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include) # 添加头文件搜索路径

# 添加 pybind11 模块
pybind11_add_module(video_stream_interface src/video_stream_interface_pybind.cpp)
target_include_directories(video_stream_interface PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
