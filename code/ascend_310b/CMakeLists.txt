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
if(NOT ASCEND_INC_PATH)
    set(ASCEND_INC_PATH "${ASCEND_HOME}/../include")
endif()

set(ASCEND_AARCH64_LIB "$ENV{ASCEND_AARCH64_LIB}" CACHE PATH "Ascend stub library path")
if(NOT ASCEND_AARCH64_LIB)
    set(ASCEND_AARCH64_LIB "${ASCEND_HOME}/../runtime/lib64/stub/aarch64")
endif()

unset(ASCENDCL_LIB CACHE)
find_library(ASCENDCL_LIB NAMES ascendcl PATHS
    "${ASCEND_AARCH64_LIB}"
    "/home/bb/Ascend/ascend-toolkit/latest/runtime/lib64/stub/aarch64"
    "/home/bb/Ascend/ascend-toolkit/latest/lib64"
    "${ASCEND_HOME}/../lib64"
    "${ASCEND_HOME}/lib64"
    REQUIRED
)

add_executable(ascend310b_pose
    src/main.cpp
)

target_include_directories(ascend310b_pose PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${ASCEND_INC_PATH}
)

target_link_libraries(ascend310b_pose PRIVATE
    ${ASCENDCL_LIB}
    pthread
    dl
)
