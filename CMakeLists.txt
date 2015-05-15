cmake_minimum_required(VERSION 2.8)

project(EPServer)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

if (WIN32)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -lws2_32")
endif()

include_directories(EPServer)

file(GLOB EPServer_SRC "EPServer/*.h" "EPServer/*.cpp")

add_executable(EPServer ${EPServer_SRC})

if (WIN32)
target_link_libraries(EPServer ws2_32)
endif()
