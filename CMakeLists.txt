cmake_minimum_required(VERSION 3.20)

project(vaporplane C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(SDL3 REQUIRED CONFIG REQUIRED COMPONENTS SDL3)

add_executable(vaporplane
    src/main.c
)

target_link_libraries(vaporplane PRIVATE SDL3::SDL3)
