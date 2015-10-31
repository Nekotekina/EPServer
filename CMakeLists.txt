cmake_minimum_required(VERSION 2.8)

project(EPServer)

add_custom_target(git_version ALL DEPENDS git_version_aux)

add_custom_command(OUTPUT git_version_aux COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/git-version.cmake)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -pthread -Wold-style-cast")

if (WIN32)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -lws2_32")
endif()

add_definitions(-msse -msse2 -mssse3)

include_directories(EPServer)
include_directories(mpir)

file(GLOB EPServer_SRC "EPServer/*.h" "EPServer/*.cpp" "EPServer/format.cc")

add_executable(epserver ${EPServer_SRC})

target_link_libraries(epserver libmpirxx.a libmpir.a)

if (WIN32)
	target_link_libraries(epserver ws2_32)
endif()

add_dependencies(epserver git_version)
