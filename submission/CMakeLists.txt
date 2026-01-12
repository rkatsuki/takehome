cmake_minimum_required(VERSION 3.22)
project(kraken_submission CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Standardize C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Optimization flags
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra")

# Ensure binary location matches Kraken script
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

find_package(Threads REQUIRED)

set(HEADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Core Logic Library 
file(GLOB HEADERS "${HEADER_DIR}/*.hpp")
add_library(trading_engine_core STATIC
    ${SOURCE_DIR}/OrderBook.cpp
    ${SOURCE_DIR}/TradingEngine.cpp
    ${SOURCE_DIR}/OrderRegistry.cpp
    ${HEADERS}
)
target_include_directories(trading_engine_core PUBLIC ${HEADER_DIR})
target_link_libraries(trading_engine_core PRIVATE Threads::Threads)

# Binary Target
add_executable(kraken_submission ${SOURCE_DIR}/main.cpp)
target_link_libraries(kraken_submission PRIVATE trading_engine_core)

# Private Unit Test Target
find_package(GTest QUIET)
if(GTest_FOUND)
    # Provides gtest_discover_tests
    include(GoogleTest) 
    enable_testing() 
    
    file(GLOB TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
    
    if(TEST_SOURCES)
        add_executable(unit_tests ${TEST_SOURCES})
        target_link_libraries(unit_tests 
            PRIVATE 
                trading_engine_core 
                GTest::gtest_main 
                Threads::Threads
        )

        # Automatically register every individual TEST() in your .cpp files, and let CTest see them individually.
        gtest_discover_tests(unit_tests)
    endif()
endif()


