# Use at least the version provided with Ubuntu focal (20.04LTS)
cmake_minimum_required(VERSION 3.16.3)
project(freertos NONE)
enable_language(C CXX ASM)
set(CMAKE_TOOLCHAIN_FILE "${CMAKE_SOURCE_DIR}/toolchain.cmake")

add_library(freertos 
    tasks.c
    timers.c
    list.c
    queue.c
    stream_buffer.c
    event_groups.c
)
target_include_directories(freertos PUBLIC include config)
if (WIN32)
    target_sources(freertos PUBLIC portable/MSVC-MingW/port.c)
    target_include_directories(freertos PUBLIC portable/MSVC-MingW)
    target_link_libraries(freertos INTERFACE Winmm)
else()
    target_sources(freertos PUBLIC
        portable/ThirdParty/GCC/Posix/port.c
        portable/ThirdParty/GCC/Posix/utils/wait_for_event.c
    )
    target_include_directories(freertos PUBLIC portable/ThirdParty/GCC/Posix)
    target_link_libraries(freertos PUBLIC pthread)
    target_compile_definitions(freertos PUBLIC _POSIX_SOURCE _POSIX_C_SOURCE=200809L)
endif()