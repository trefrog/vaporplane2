cmake_minimum_required(VERSION 3.20)
project(vaporplane C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
find_package(SDL3 REQUIRED CONFIG REQUIRED COMPONENTS SDL3)
add_executable(vaporplane
    src/main.c src/app.c src/input.c src/audio_engine.c src/transport.c src/clip.c src/timeline.c src/waveform.c
)
target_include_directories(vaporplane PRIVATE include)
target_link_libraries(vaporplane PRIVATE SDL3::SDL3)

if(UNIX)
    find_library(MATH_LIBRARY m)
    if(MATH_LIBRARY)
        target_link_libraries(vaporplane PRIVATE ${MATH_LIBRARY})
    endif()
endif()
