cmake_minimum_required(VERSION 3.20)
project(GTOSolver VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build options
option(BUILD_TESTS "Build unit tests" OFF)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Find or fetch ftxui
find_package(ftxui QUIET)
if(NOT ftxui_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        ftxui
        GIT_REPOSITORY https://github.com/ArthurSonzogni/ftxui.git
        GIT_TAG v5.0.0
    )
    FetchContent_MakeAvailable(ftxui)
endif()

# Source files
set(CORE_SOURCES
    src/core/game_state.cpp
    src/core/hand.cpp
    src/core/hand_evaluator.cpp
)

set(UI_SOURCES
    src/ui/interactive_range_display.cpp
)

set(SOLVER_SOURCES
    src/solver/cfr_engine.cpp
)

set(SOURCES
    src/main.cpp
    ${CORE_SOURCES}
    ${UI_SOURCES}
    ${SOLVER_SOURCES}
)

# Executable
add_executable(${PROJECT_NAME} ${SOURCES})

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE ftxui::component ftxui::dom ftxui::screen)

# Compiler options
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -pedantic)
endif()

# Testing (optional)
if(BUILD_TESTS)
    enable_testing()
    find_package(GTest QUIET)
    if(GTest_FOUND)
        # Add test executables here
    endif()
endif()

# Installation
install(TARGETS ${PROJECT_NAME} DESTINATION bin)
