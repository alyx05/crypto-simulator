cmake_minimum_required(VERSION 3.15)
project(merkelrex VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(SQLite3 REQUIRED)
message(STATUS "SQLite3_FOUND: ${SQLite3_FOUND}")
message(STATUS "SQLite3_INCLUDE_DIRS: ${SQLite3_INCLUDE_DIRS}")
message(STATUS "SQLite3_LIBRARIES: ${SQLite3_LIBRARIES}")

include(FetchContent)

FetchContent_Declare(
    Crow
    GIT_REPOSITORY https://github.com/CrowCpp/Crow.git
    GIT_TAG v1.2.0
)
set(CROW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CROW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(Crow)

find_package(Threads REQUIRED)

# Core trading logic as a static lib, separate from main.cpp, so both the
# `merkelrex` executable and the test/benchmark binaries below can link
# against it without pulling in (or duplicating) main().
file(GLOB CORE_SOURCES "src/*.cpp")
list(REMOVE_ITEM CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")

add_library(merkelrex_core STATIC ${CORE_SOURCES})
target_include_directories(merkelrex_core PUBLIC include)

if(TARGET SQLite::SQLite3)
    target_link_libraries(merkelrex_core PUBLIC SQLite::SQLite3)
else()
    target_include_directories(merkelrex_core PUBLIC ${SQLite3_INCLUDE_DIRS})
    target_link_libraries(merkelrex_core PUBLIC ${SQLite3_LIBRARIES})
endif()

target_link_libraries(merkelrex_core PUBLIC Crow::Crow Threads::Threads)
target_link_options(merkelrex_core PUBLIC -static-libgcc -static-libstdc++)

target_compile_options(merkelrex_core PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -O2>
)

add_executable(merkelrex src/main.cpp)
target_link_libraries(merkelrex PRIVATE merkelrex_core)

# ---- Tests ---------------------------------------------------------------
option(MERKELREX_BUILD_TESTS "Build the GoogleTest suite" ON)

if(MERKELREX_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()

    file(GLOB TEST_SOURCES "tests/*.cpp")
    add_executable(merkelrex_tests ${TEST_SOURCES})
    target_link_libraries(merkelrex_tests PRIVATE merkelrex_core GTest::gtest_main)
    target_include_directories(merkelrex_tests PRIVATE include)
    # Absolute path so fixture CSVs load correctly regardless of ctest's cwd.
    target_compile_definitions(merkelrex_tests PRIVATE
        FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")

    include(GoogleTest)
    gtest_discover_tests(merkelrex_tests)
endif()

# ---- Latency benchmark ----------------------------------------------------
option(MERKELREX_BUILD_BENCH "Build the matching-engine latency benchmark" ON)

if(MERKELREX_BUILD_BENCH)
    add_executable(latency_bench bench/latency_bench.cpp)
    target_link_libraries(latency_bench PRIVATE merkelrex_core)
endif()