cmake_minimum_required(VERSION 3.22)
project(kraken_submission CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- PILLAR 1: LANGUAGE STANDARDS ---
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF) # Keep it strict to ensure portability

# --- PILLAR 2: HFT-SPECIFIC OPTIMIZATION ---
# -O3: Aggressive optimization (vectorization, loop unrolling).
# -march=native: Crucial for 'alignas(64)'. It tells the compiler to use the 
#                specific cache-line and instruction set (AVX/SSE) of YOUR CPU.
# -flto: Link Time Optimization. Allows the compiler to optimize across .cpp 
#        files (e.g., inlining Parser functions directly into the Engine).
# -fno-plt: Minor latency win by reducing jumps through the Procedure Linkage Table.
if(NOT MSVC)
    set(HFT_OPTIMIZATIONS "-O3 -march=native -flto -fno-plt -DNDEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "${HFT_OPTIMIZATIONS}")
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra -Wpedantic -fsanitize=address")
endif()

# --- PILLAR 3: PROJECT STRUCTURE ---
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
find_package(Threads REQUIRED)

# Define directories explicitly
set(HEADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

# --- PILLAR 4: CORE LOGIC LIBRARY ---
# Note: Avoid GLOB for sources in production; explicit lists prevent 
# "ghost" files from being compiled.
add_library(trading_engine_core STATIC
    ${SOURCE_DIR}/TradingApp.cpp
    ${SOURCE_DIR}/UDPServer.cpp
    ${SOURCE_DIR}/CSVParser.cpp
    ${SOURCE_DIR}/TradingEngine.cpp
    ${SOURCE_DIR}/OrderBook.cpp
)

target_include_directories(trading_engine_core PUBLIC ${HEADER_DIR})
target_link_libraries(trading_engine_core PRIVATE Threads::Threads)

# Enable LTO (Interprocedural Optimization) for the core library
if(NOT MSVC)
    set_target_properties(trading_engine_core PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# --- PILLAR 5: BINARY TARGET ---
add_executable(kraken_submission ${SOURCE_DIR}/main.cpp)
target_link_libraries(kraken_submission PRIVATE trading_engine_core)

# --- PILLAR 6: TESTING FRAMEWORK ---
enable_testing()
find_package(GTest QUIET)
if(GTest_FOUND)
    include(GoogleTest) 
    file(GLOB TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
    
    if(TEST_SOURCES)
        add_executable(unit_tests ${TEST_SOURCES})
        target_link_libraries(unit_tests 
            PRIVATE 
                trading_engine_core 
                GTest::gtest_main 
                Threads::Threads
        )
        gtest_discover_tests(unit_tests)
    endif()
endif()