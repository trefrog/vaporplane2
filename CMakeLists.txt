cmake_minimum_required(VERSION 3.20)
project(vaporplane C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
find_package(SDL3 REQUIRED CONFIG REQUIRED COMPONENTS SDL3)

add_executable(vaporplane
    src/main.c src/app.c src/transport.c src/audio_engine.c src/clip.c src/waveform.c src/input.c
)
target_include_directories(vaporplane PRIVATE include)
target_link_libraries(vaporplane PRIVATE SDL3::SDL3 m)
