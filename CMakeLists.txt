cmake_minimum_required(VERSION 2.8)

project(EPServer)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -pthread")

if (WIN32)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -lws2_32")
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -lX11")
endif()

add_definitions(-msse -msse2 -mssse3)

include_directories(EPServer)
include_directories(mpir)

file(GLOB EPServer_SRC "EPServer/*.h" "EPServer/*.cpp")

add_executable(epserver ${EPServer_SRC})

target_link_libraries(epserver libmpirxx.a libmpir.a)

if (WIN32)
	target_link_libraries(epserver ws2_32)
endif()
