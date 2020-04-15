cmake_minimum_required(VERSION 3.15)
project(CN_SemesterProject C)

set(CMAKE_C_STANDARD 99)

add_executable(client client_udp.c)
add_executable(server server_udp.c)