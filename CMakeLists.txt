cmake_minimum_required(VERSION 3.25)
project(rdb)

set(CMAKE_CXX_STANDARD 17)

add_executable(rdb server/server.cpp client/client.cpp)
