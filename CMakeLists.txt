cmake_minimum_required(VERSION 3.0)
project(sliding_window_protocol C)

set(src crc32.c lprintf.c protocol.c)

add_executable(datalink datalink.c ${src})
add_executable(gobackn gobackn.c ${src})

target_link_libraries(datalink m)
target_link_libraries(gobackn m)