cmake_minimum_required(VERSION 3.16)
project(sector_control C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_C_FLAGS_RELEASE "-O2")

# ---------------------------------------------------------------------------
# Raylib: use a system install if present, otherwise fetch and build 5.5.
# ---------------------------------------------------------------------------
find_package(raylib 5.5 QUIET)

if(NOT raylib_FOUND)
    include(FetchContent)
    set(FETCHCONTENT_QUIET OFF)
    FetchContent_Declare(
        raylib
        GIT_REPOSITORY https://github.com/raysan5/raylib.git
        GIT_TAG        5.5
        GIT_SHALLOW    TRUE
    )
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_GAMES    OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(raylib)
endif()

# ---------------------------------------------------------------------------
# Library: the headless simulation core (also used by the test target).
# ---------------------------------------------------------------------------
add_library(atc_core STATIC
    src/pqueue.c
    src/rng.c
    src/sim.c
    src/dataset.c
)
target_include_directories(atc_core PUBLIC include)

# ---------------------------------------------------------------------------
# Main application (core + raylib render layer).
# ---------------------------------------------------------------------------
add_executable(sector_control
    src/main.c
    src/render.c
)
target_link_libraries(sector_control PRIVATE atc_core raylib)

# raylib needs a few platform frameworks/libs.
if(APPLE)
    target_link_libraries(sector_control PRIVATE
        "-framework IOKit" "-framework Cocoa" "-framework OpenGL")
elseif(UNIX)
    target_link_libraries(sector_control PRIVATE m pthread dl)
endif()

# ---------------------------------------------------------------------------
# Tests (headless, no raylib dependency).
# ---------------------------------------------------------------------------
enable_testing()
add_executable(test_core tests/test_core.c)
target_link_libraries(test_core PRIVATE atc_core)
if(UNIX AND NOT APPLE)
    target_link_libraries(test_core PRIVATE m)
endif()
add_test(NAME core_tests COMMAND test_core)
