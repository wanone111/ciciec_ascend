cmake_minimum_required(VERSION 3.16)

project(communication_zmq LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenCV REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBZMQ REQUIRED IMPORTED_TARGET libzmq)

add_library(communication_common INTERFACE)
target_include_directories(communication_common
	INTERFACE
		${CMAKE_CURRENT_SOURCE_DIR}/include
		${OpenCV_INCLUDE_DIRS}
)
target_link_libraries(communication_common
	INTERFACE
		${OpenCV_LIBS}
		PkgConfig::LIBZMQ
		Threads::Threads
)

add_executable(pynq_zmq_pub src/pynq_zmq_pub.cpp)
target_link_libraries(pynq_zmq_pub PRIVATE communication_common)

add_executable(zmq_sub_310b src/310B_zmq_sub.cpp)
target_link_libraries(zmq_sub_310b PRIVATE communication_common)
