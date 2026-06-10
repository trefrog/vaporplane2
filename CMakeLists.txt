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
option(VAPORPLANE_ENABLE_SNDFILE "Enable libsndfile audio import support when available" ON)
add_subdirectory(third_party/soundtouch EXCLUDE_FROM_ALL)

find_package(SDL3 REQUIRED CONFIG REQUIRED COMPONENTS SDL3)
set(VAPORPLANE_SNDFILE_TARGET "")
if(VAPORPLANE_ENABLE_SNDFILE)
    find_package(SndFile CONFIG QUIET)
    if(TARGET SndFile::sndfile)
        set(VAPORPLANE_SNDFILE_TARGET SndFile::sndfile)
    else()
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(SNDFILE QUIET IMPORTED_TARGET sndfile)
            if(TARGET PkgConfig::SNDFILE)
                set(VAPORPLANE_SNDFILE_TARGET PkgConfig::SNDFILE)
            endif()
        endif()
    endif()
endif()
add_executable(vaporplane
    src/main.c src/app.c src/input.c src/audio_engine.c src/transport.c src/clip.c src/audio_file.c src/waveform.c src/project_validation.c
    src/stretch_soundtouch.cpp
)
target_include_directories(vaporplane PRIVATE include)
target_link_libraries(vaporplane PRIVATE SDL3::SDL3 SoundTouch)
if(VAPORPLANE_SNDFILE_TARGET)
    target_link_libraries(vaporplane PRIVATE ${VAPORPLANE_SNDFILE_TARGET})
    target_compile_definitions(vaporplane PRIVATE VAPORPLANE_HAVE_SNDFILE=1)
    message(STATUS "libsndfile import support enabled")
else()
    message(STATUS "libsndfile import support disabled; WAV import fallback only")
endif()

enable_testing()
add_executable(test_stretch_soundtouch
    tests/test_stretch_soundtouch.c
    src/stretch_soundtouch.cpp
)
target_include_directories(test_stretch_soundtouch PRIVATE include)
target_link_libraries(test_stretch_soundtouch PRIVATE SoundTouch)
add_test(NAME stretch_soundtouch COMMAND test_stretch_soundtouch)

add_executable(test_timeline_math
    tests/test_timeline_math.c
)
target_include_directories(test_timeline_math PRIVATE include)
target_link_libraries(test_timeline_math PRIVATE SDL3::SDL3)
add_test(NAME timeline_math COMMAND test_timeline_math)

add_executable(test_timeline_audio
    tests/test_timeline_audio.c
    src/audio_engine.c
    src/transport.c
)
target_include_directories(test_timeline_audio PRIVATE include)
target_link_libraries(test_timeline_audio PRIVATE SDL3::SDL3)
add_test(NAME timeline_audio COMMAND test_timeline_audio)

add_executable(test_timeline_guarded_edits
    tests/test_timeline_guarded_edits.c
    src/app.c src/input.c src/audio_engine.c src/transport.c src/clip.c src/audio_file.c src/waveform.c src/project_validation.c
    src/stretch_soundtouch.cpp
)
target_include_directories(test_timeline_guarded_edits PRIVATE include)
target_link_libraries(test_timeline_guarded_edits PRIVATE SDL3::SDL3 SoundTouch)
add_test(NAME timeline_guarded_edits COMMAND test_timeline_guarded_edits)

if(UNIX)
    find_library(MATH_LIBRARY m)
    if(MATH_LIBRARY)
        target_link_libraries(vaporplane PRIVATE ${MATH_LIBRARY})
        target_link_libraries(test_stretch_soundtouch PRIVATE ${MATH_LIBRARY})
        target_link_libraries(test_timeline_math PRIVATE ${MATH_LIBRARY})
        target_link_libraries(test_timeline_audio PRIVATE ${MATH_LIBRARY})
        target_link_libraries(test_timeline_guarded_edits PRIVATE ${MATH_LIBRARY})
    endif()
endif()
