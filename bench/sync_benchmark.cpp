// Synchronous throughput benchmark for the core matching engine.
//
// Unlike Benchmark.cpp (which drives the full lock-free async pipeline and needs
// Boost), this hits OrderBook directly, so it builds with nothing but a C++20
// compiler. It doubles as a smoke test: a few asserts on matching behaviour run
// before the timing loop.
#include "OrderBook.hpp"

#include <cassert>
#include <chrono>
#include <iostream>

using namespace trading;

static void correctness_checks() {
    OrderBook ob;
    ob.add_order(1, 10'000, 100, Side::BUY);
    ob.add_order(2, 10'010, 100, Side::SELL);
    assert(ob.get_best_bid() == 10'000);
    assert(ob.get_best_ask() == 10'010);

    // A buy that crosses the ask should fill against the resting sell.
    ob.add_order(3, 10'010, 60, Side::BUY);   // lifts 60 of order #2
    assert(ob.get_best_ask() == 10'010);      // 40 still resting
    assert(ob.get_best_bid() == 10'000);      // our buy was fully filled, nothing rests

    ob.cancel_order(1);                        // remove the only bid
    assert(ob.get_best_bid() == 0);
    std::cout << "[correctness] matching, partial fill, and cancel: OK\n";
}

int main() {
    correctness_checks();

    OrderBook ob;
    const std::size_t kEvents = 5'000'000;
    std::cout << "[*] Firing " << kEvents << " synchronous orders...\n";

    const auto t0 = std::chrono::high_resolution_clock::now();
    for (std::size_t i = 1; i <= kEvents; ++i) {
        // Overlapping bid/ask prices so a healthy fraction crosses and matches —
        // this exercises insertion, the matching loop, and pool de/allocation.
        const Price price = 10'000 + (i % 100);
        const Side side = (i % 2 == 0) ? Side::BUY : Side::SELL;
        ob.add_order(i, price, 100, side);
    }
    const auto t1 = std::chrono::high_resolution_clock::now();

    auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    if (us <= 0) us = 1;
    std::cout << "------------------------------------------\n"
              << "Events     : " << kEvents << "\n"
              << "Time (us)  : " << us << "\n"
              << "Throughput : " << (kEvents * 1'000'000ULL / us) << " msgs/sec\n"
              << "------------------------------------------\n";
    ob.print_top_of_book();
    return 0;
}
