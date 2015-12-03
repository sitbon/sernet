cmake_minimum_required(VERSION 2.8)
project(sernet C)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu11 -D__USE_POSIX199309")

set(SOURCE_FILES main.c serial.c udp.c)
add_executable(sernet ${SOURCE_FILES})
target_link_libraries(sernet -lrt -lpthread)
