cmake_minimum_required(VERSION 3.14)
project(hft_matching_engine CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Performance flags. Tune to the build host; drop the -mtune flag if you need a
# portable binary.
#
# The host-tuning flag is spelled differently depending on compiler and target.
# clang on aarch64 rejects -march=native outright ("the clang compiler does not
# support '-march=native'"), so hardcoding it broke the documented build on
# Linux ARM with clang - which is exactly the platform this engine is fastest on.
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-march=native" HAVE_MARCH_NATIVE)
check_cxx_compiler_flag("-mcpu=native" HAVE_MCPU_NATIVE)
if(HAVE_MARCH_NATIVE)
    set(HOST_TUNE "-march=native")
elseif(HAVE_MCPU_NATIVE)
    set(HOST_TUNE "-mcpu=native")
else()
    set(HOST_TUNE "")
    message(STATUS "No host-tuning flag available; building generic.")
endif()
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 ${HOST_TUNE} -flto -Wall -Wextra")

include_directories(include)

# --- Core synchronous benchmark: no third-party deps, always built. -----------
add_executable(hft_sync_benchmark
    bench/sync_benchmark.cpp
    src/OrderBook.cpp
)

# --- Tests -------------------------------------------------------------------
enable_testing()
add_executable(orderbook_tests
    tests/test_orderbook.cpp
    src/OrderBook.cpp
)
add_test(NAME orderbook COMMAND orderbook_tests)

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
