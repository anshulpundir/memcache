cmake_minimum_required (VERSION 2.6)

project(memcache)

file(GLOB SRC_FILES *.cpp *.h)

#debug, use -O3 for optimized.
set( CMAKE_CXX_FLAGS "-std=c++1y -g -DUSE_EPOLL" )

add_library(mclib ${SRC_FILES})
add_executable(memcache ${SRC_FILES})

target_link_libraries(memcache pthread)

enable_testing()
add_subdirectory(unittest)
