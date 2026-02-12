cmake_minimum_required(VERSION 3.14)
project(poker_solver VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Fetch ftxui for TUI
include(FetchContent)
FetchContent_Declare(
    ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
    GIT_TAG v5.0.0
)
FetchContent_MakeAvailable(ftxui)

# Main executable - New TUI
add_executable(poker_solver
    src/main.cpp
    src/app/poker_app.cpp
    src/core/game_state.cpp
    src/components/grid.cpp
    src/components/action_panel.cpp
    src/card/card.cpp
    src/card/hand.cpp
    src/card/evaluator.cpp
    src/game/range.cpp
    src/game/board.cpp
    src/game/action.cpp
    src/game/game_tree.cpp
    src/solver/solver.cpp
    src/solver/cfr.cpp
    src/solver/abstraction.cpp
)

target_include_directories(poker_solver PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(poker_solver PRIVATE
    ftxui::screen
    ftxui::dom
    ftxui::component
)

# Compiler flags for optimization
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(poker_solver PRIVATE -O3 -march=native)
elseif(MSVC)
    target_compile_options(poker_solver PRIVATE /O2)
endif()

# Create solves directory
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/solves)

# Component test executable
add_executable(test_components
    test/test_components.cpp
    src/core/game_state.cpp
    src/components/grid.cpp
    src/card/card.cpp
    src/card/hand.cpp
    src/card/evaluator.cpp
    src/game/range.cpp
    src/game/board.cpp
    src/game/game_tree.cpp
    src/game/action.cpp
)

target_include_directories(test_components PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/test
)

target_link_libraries(test_components PRIVATE
    ftxui::screen
    ftxui::dom
    ftxui::component
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_components PRIVATE -O3 -march=native)
elseif(MSVC)
    target_compile_options(test_components PRIVATE /O2)
endif()
