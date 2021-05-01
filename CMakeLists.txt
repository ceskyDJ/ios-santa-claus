cmake_minimum_required(VERSION 3.19)
project(IOS\ |\ 2nd\ project C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_COMPILER gcc)
set(CMAKE_C_FLAGS "-std=gnu99 -Wall -Wextra -Werror -pedantic")

add_executable(proj2 proj2.c)

target_link_libraries(proj2 pthread)
