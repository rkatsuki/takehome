cmake_minimum_required(VERSION 3.22)
project(kraken_submission)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -O3")

# Find Threading support (Required for mutexes and atomics)
find_package(Threads REQUIRED)

# Define variables for source and header directories
set(PROJECT_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(HEADER_DIR ${PROJECT_SOURCE_DIR}/include)
set(SOURCE_DIR ${PROJECT_SOURCE_DIR}/src)

# Tell the compiler where to find the header files
include_directories(${HEADER_DIR})

# Define the source files for the main library
set(ENGINE_SOURCES
    ${SOURCE_DIR}/OrderBook.cpp
    ${SOURCE_DIR}/TradingEngine.cpp
    ${SOURCE_DIR}/OrderRegistry.cpp
)

# Create a static library target for the core logic
add_library(trading_engine_core STATIC ${ENGINE_SOURCES})

add_executable(kraken_submission src/main.cpp)

# Link the test executable against the core library and GTest
target_link_libraries(kraken_submission 
    PRIVATE 
        trading_engine_core 
        Threads::Threads
)
