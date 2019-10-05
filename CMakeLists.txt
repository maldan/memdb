cmake_minimum_required(VERSION 3.7)
project(memdb C)

set(CMAKE_C_STANDARD 11)
add_compile_options(-Wall -Wextra -Wpedantic -DDEBUG)
add_subdirectory(osoyanlib)

add_executable(memdb main.c)
target_link_libraries(memdb osoyanlib)