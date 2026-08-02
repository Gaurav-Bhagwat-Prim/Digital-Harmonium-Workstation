# ==============================================================================
# firmware/CMakeLists.txt — Master Build Configuration
# Project: Digital Harmonium Workstation Firmware
# Stage 5 — Input Layer Architecture
# ==============================================================================
cmake_minimum_required(VERSION 3.16)

# Include ESP-IDF project boilerplate if available
if(DEFINED ENV{IDF_PATH})
    include($ENV{IDF_PATH}/tools/cmake/project.cmake)
    project(digital_harmonium_firmware)
else()
    # Standard CMake fallback for host build/verification
    project(digital_harmonium_firmware C)
    set(CMAKE_C_STANDARD 11)
    set(CMAKE_C_STANDARD_REQUIRED ON)

    # Compiler warning flags enforced (-Werror)
    if(MSVC)
        add_compile_options(/W4 /WX)
    else()
        add_compile_options(
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-unused-parameter
        )
    endif()

    add_subdirectory(components/vhl_core)
    add_subdirectory(components/vhl_runtime)
    add_subdirectory(components/vhl_input)
    add_subdirectory(main)
endif()
