cmake_minimum_required(VERSION 3.20)
project(gto-solver VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Optimization flags
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native -ffast-math")
endif()

# FetchContent for dependencies
include(FetchContent)

# FTXUI for TUI
FetchContent_Declare(
    ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
    GIT_TAG v5.0.0
)
FetchContent_MakeAvailable(ftxui)

# Header-only library structure
add_library(gto-core INTERFACE)
target_include_directories(gto-core INTERFACE 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Main executable
add_executable(gto-solver src/main.cpp)

target_link_libraries(gto-solver PRIVATE
    gto-core
    ftxui::screen
    ftxui::dom
    ftxui::component
)

# Tests (optional)
option(BUILD_TESTS "Build tests" OFF)
if(BUILD_TESTS)
    enable_testing()
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest
        GIT_TAG v1.14.0
    )
    FetchContent_MakeAvailable(googletest)
    
    add_executable(gto-tests tests/test_main.cpp)
    target_link_libraries(gto-tests PRIVATE gto-core gtest gtest_main)
endif()
