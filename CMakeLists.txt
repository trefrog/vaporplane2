cmake_minimum_required(VERSION 3.20)
project(vaporplane LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOUNDSTRETCH OFF CACHE BOOL "" FORCE)
set(SOUNDTOUCH_DLL OFF CACHE BOOL "" FORCE)
set(INTEGER_SAMPLES OFF CACHE BOOL "" FORCE)
set(OPENMP OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/soundtouch EXCLUDE_FROM_ALL)

find_package(SDL3 REQUIRED CONFIG REQUIRED COMPONENTS SDL3)
add_executable(vaporplane
    src/main.c src/app.c src/input.c src/audio_engine.c src/transport.c src/clip.c src/waveform.c src/project_validation.c
    src/stretch_soundtouch.cpp
)
target_include_directories(vaporplane PRIVATE include)
target_link_libraries(vaporplane PRIVATE SDL3::SDL3 SoundTouch)

enable_testing()
add_executable(test_stretch_soundtouch
    tests/test_stretch_soundtouch.c
    src/stretch_soundtouch.cpp
)
target_include_directories(test_stretch_soundtouch PRIVATE include)
target_link_libraries(test_stretch_soundtouch PRIVATE SoundTouch)
add_test(NAME stretch_soundtouch COMMAND test_stretch_soundtouch)

if(UNIX)
    find_library(MATH_LIBRARY m)
    if(MATH_LIBRARY)
        target_link_libraries(vaporplane PRIVATE ${MATH_LIBRARY})
        target_link_libraries(test_stretch_soundtouch PRIVATE ${MATH_LIBRARY})
    endif()
endif()
