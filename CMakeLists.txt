cmake_minimum_required(VERSION 3.14)
project(hft_matching_engine CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Performance flags. -march=native tunes to the build host; drop it if you need a
# portable binary.
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native -flto -Wall -Wextra")

include_directories(include)

# --- Core synchronous benchmark: no third-party deps, always built. -----------
add_executable(hft_sync_benchmark
    bench/sync_benchmark.cpp
    src/OrderBook.cpp
)

# --- Async lock-free pipeline benchmark: needs Boost (header-only lockfree). ---
find_package(Boost QUIET)
if(Boost_FOUND)
    add_executable(hft_benchmark
        src/Benchmark.cpp
        src/OrderBook.cpp
    )
    target_include_directories(hft_benchmark PRIVATE ${Boost_INCLUDE_DIRS})
    target_link_libraries(hft_benchmark PRIVATE ${Boost_LIBRARIES} pthread)
    message(STATUS "Boost found - building async pipeline benchmark (hft_benchmark).")
else()
    message(STATUS "Boost not found - skipping async benchmark; core + hft_sync_benchmark still build.")
endif()
