cmake_minimum_required(VERSION 3.16)                                            # 指定 CMake 最低版本要求

project(udp_pc_pynq LANGUAGES CXX)                                               # 定义项目名称和语言

set(CMAKE_CXX_STANDARD 17)                                                        # 设置 C++ 标准为 C++17
set(CMAKE_CXX_STANDARD_REQUIRED ON)                                               # 要求必须支持指定标准
set(CMAKE_CXX_EXTENSIONS OFF)                                                     # 关闭编译器私有扩展，保证可移植性

add_executable(pc_udp src/client_udp.cpp)                                         # 添加 PC 端可执行文件目标
target_include_directories(pc_udp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)    # 为 PC 目标添加头文件搜索路径

add_executable(pynq_udp src/pynq_udp.cpp)                                         # 添加 PYNQ 端可执行文件目标
target_include_directories(pynq_udp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)  # 为 PYNQ 目标添加头文件搜索路径
