cmake_minimum_required(VERSION 3.22)
project(kraken_submission CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Standardize C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Hardened Flags - Standardizing optimization and warnings
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -O3")

# --- CRITICAL: Ensure binary location matches Kraken script expectations ---
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

find_package(Threads REQUIRED)

set(HEADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Core Logic Library 
# Including headers here ensures VS Code tracks changes accurately
file(GLOB HEADERS "${HEADER_DIR}/*.hpp")
add_library(trading_engine_core STATIC
    ${SOURCE_DIR}/OrderBook.cpp
    ${SOURCE_DIR}/TradingEngine.cpp
    ${SOURCE_DIR}/OrderRegistry.cpp
    ${HEADERS}
)
target_include_directories(trading_engine_core PUBLIC ${HEADER_DIR})
target_link_libraries(trading_engine_core PRIVATE Threads::Threads)

# Official Binary Target
add_executable(kraken_submission ${SOURCE_DIR}/main.cpp)
target_link_libraries(kraken_submission PRIVATE trading_engine_core)

# Private Unit Test Target
find_package(GTest QUIET)
if(GTest_FOUND)
    enable_testing() # Essential for standard C++ test discovery
    file(GLOB TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
    
    if(TEST_SOURCES)
        add_executable(unit_tests ${TEST_SOURCES})
        target_link_libraries(unit_tests 
            PRIVATE 
                trading_engine_core 
                GTest::gtest_main 
                Threads::Threads
        )
    endif()
endif()