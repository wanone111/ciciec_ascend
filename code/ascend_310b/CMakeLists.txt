cmake_minimum_required(VERSION 3.16)

project(ascend_310b_pose LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(ASCEND_HOME "$ENV{ASCEND_HOME}" CACHE PATH "Ascend toolkit root")
if(NOT ASCEND_HOME)
    set(ASCEND_HOME "/home/bb/Ascend/ascend-toolkit/latest")
endif()

set(ASCEND_INC_PATH "$ENV{ASCEND_INC_PATH}" CACHE PATH "Ascend include path")
if(ASCEND_INC_PATH AND EXISTS "${ASCEND_INC_PATH}/acl/acl.h")
    message(STATUS "Using ASCEND_INC_PATH=${ASCEND_INC_PATH}")
else()
    unset(ASCEND_INC_PATH CACHE)
    find_path(ASCEND_INC_PATH acl/acl.h
        PATHS
            "${ASCEND_HOME}/x86_64-linux/include"
            "${ASCEND_HOME}/runtime/include"
            "${ASCEND_HOME}/include"
            "${ASCEND_HOME}/../x86_64-linux/include"
            "/home/bb/Ascend/ascend-toolkit/latest/x86_64-linux/include"
            "/home/bb/Ascend/ascend-toolkit/latest/runtime/include"
            "/usr/local/Ascend/ascend-toolkit/latest/x86_64-linux/include"
            "/usr/local/Ascend/ascend-toolkit/latest/runtime/include"
    )
endif()

if(NOT ASCEND_INC_PATH)
    message(FATAL_ERROR "Cannot find acl/acl.h, set ASCEND_INC_PATH to Ascend include root")
endif()

set(ASCEND_AARCH64_LIB "$ENV{ASCEND_AARCH64_LIB}" CACHE PATH "Ascend stub library path")
if(NOT ASCEND_AARCH64_LIB)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64")
        set(ASCEND_AARCH64_LIB "${ASCEND_HOME}/../runtime/lib64/stub/x86_64")
    else()
        set(ASCEND_AARCH64_LIB "${ASCEND_HOME}/../runtime/lib64/stub/aarch64")
    endif()
endif()

unset(ASCENDCL_LIB CACHE)
find_library(ASCENDCL_LIB NAMES ascendcl PATHS
    "${ASCEND_AARCH64_LIB}"
    "/home/bb/Ascend/ascend-toolkit/latest/runtime/lib64/stub/x86_64"
    "/home/bb/Ascend/ascend-toolkit/latest/runtime/lib64/stub/aarch64"
    "/home/bb/Ascend/ascend-toolkit/latest/lib64"
    "${ASCEND_HOME}/../lib64"
    "${ASCEND_HOME}/lib64"
    REQUIRED
)

add_executable(ascend310b_pose
    src/main.cpp
    src/ip_video_player.cpp
)

target_include_directories(ascend310b_pose PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../communication/include
    ${ASCEND_INC_PATH}
)

target_link_libraries(ascend310b_pose PRIVATE
    ${ASCENDCL_LIB}
    pthread
    dl
)
