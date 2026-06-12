# High-Performance Limit Order Book (LOB) Matching Engine

A deterministic, low-latency limit-order-book and matching engine in **C++20**.
Every primary book operation — insertion, cancellation, execution — runs in **O(1)**
through intrusive linked lists, hash maps, and a custom memory pool.

## Why it's fast

- **O(1) operations.** Each price level is an intrusive doubly-linked list, so
  removing a resting order is a pointer splice — no search. Orders and levels are
  reached by hash map; best bid/ask come off ordered `std::map` fronts.
- **Zero-allocation hot path.** A pre-warmed `MemoryPool` hands out `Order` and
  `PriceLevel` objects with placement-new and reclaims them to a free list, so the
  matching loop never touches `malloc`/`new`.
- **Cache-aligned data.** `Order` and `PriceLevel` are 64-byte aligned to avoid
  false sharing and keep each to a single cache line.
- **Lock-free ingestion.** An optional async pipeline (`Backtester`) decouples order
  intake from matching via a `boost::lockfree::queue`.

## Performance

| Path | Throughput | Notes |
|---|---|---|
| Synchronous core (`hft_sync_benchmark`) | ~26M msgs/sec (Apple M2) · 35M+ (Linux aarch64) | no dependencies |
| End-to-end lock-free pipeline (`hft_benchmark`) | ~8.5M msgs/sec (Apple M2) | needs Boost |

The synchronous figure is reproducible with the included benchmark — it also runs a
set of correctness asserts (matching, partial fill, cancel) before timing.

## Layout

```
hft-matching-engine/
├── include/
│   ├── Types.hpp        # Order / PriceLevel (64B-aligned), Price/Qty/Id types
│   ├── OrderBook.hpp    # the matching engine interface
│   ├── MemoryPool.hpp   # pre-warmed O(1) object pool
│   ├── Backtester.hpp   # lock-free async pipeline (Boost)
│   └── ItchParser.hpp   # ITCH message decoding
├── src/
│   ├── OrderBook.cpp     # matching, cancel, execute
│   └── Benchmark.cpp     # async pipeline benchmark (Boost)
├── bench/
│   └── sync_benchmark.cpp  # dependency-free core benchmark + smoke test
└── CMakeLists.txt
```

## Build & run

The **core engine needs only a C++20 compiler**; Boost is optional and used solely
for the async pipeline benchmark.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

./build/hft_sync_benchmark   # core throughput + correctness (always available)
./build/hft_benchmark        # async pipeline (built only if Boost is found)
```

Or compile the core directly, no CMake:

```bash
g++ -O3 -std=c++20 -march=native -Iinclude src/OrderBook.cpp bench/sync_benchmark.cpp -o sync_benchmark
./sync_benchmark
```

## Design notes

- Prices are fixed-point `uint64_t` (4 implied decimals) — no floating point in the
  hot path.
- Matching is strict price-time priority: a marketable order walks the opposite book
  from the best level, filling resting orders FIFO within each level.
- The hash-map price lookup is a pragmatic choice; a production system on a known
  tick grid would direct-address an array of levels.

---

*Author: Tejas Pandya — NYU MSFE.*
