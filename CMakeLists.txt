cmake_minimum_required(VERSION 2.8)
project(sernet C)

set(CMAKE_C_COMPILER gcc)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -std=gnu11 -D__USE_POSIX199309")

set(SOURCE_FILES main.c fwd.c dst.c src.c osc.c packet.c serial.c udp.c osc/tinyosc.c)
add_executable(sernet ${SOURCE_FILES})
target_link_libraries(sernet -lrt -lpthread)
