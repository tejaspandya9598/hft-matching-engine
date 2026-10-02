# High-Performance Limit Order Book (LOB) Matching Engine

A deterministic, low-latency limit-order-book and matching engine in **C++20**.
Cancels, executions and adds at an existing price level run in **O(1)** average time
through intrusive linked lists, hash maps, and a custom memory pool; opening a new price
level costs O(log L) for best-price tracking (see *Complexity, precisely*).

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

Everything below comes from `./bench/run_benchmarks.sh`, which builds and runs both
paths five times each and prints every run. `--linux` repeats the whole thing inside
a `linux/arm64` container so the two platforms are measured by the same script.

| Path | macOS (Apple M2, 8 cores, Apple clang 21) | Linux aarch64 (Debian 12, 4 cores, g++ 12) |
|---|---|---|
| Synchronous core (`hft_sync_benchmark`) | **~21M msgs/sec** (19.6 – 22.5) | **~36M msgs/sec** (36.0 – 36.5) |
| Lock-free pipeline (`hft_benchmark`, Boost) | **~10.7M msgs/sec** (10.3 – 11.0) | **~8.5M msgs/sec** (8.0 – 9.1) |

With clang 14 instead of g++ on the same Linux target the synchronous core reaches
**~39.8M msgs/sec** (38.8 – 39.9), so the platform gap is not a compiler artefact.

A second macOS session on 2026-10-02 (same M2, Apple clang 21, five runs each) measured
the synchronous core at 19.2 – 25.0M (median 24.6M) and the pipeline at 9.9 – 10.4M:
inside the spread above, and a reminder of how much a laptop under desktop load moves.

Three things in that table are worth more than the headline numbers.

**The spread is part of the measurement.** On macOS the synchronous core varies about
15% run to run; on Linux it varies under 1%. Same silicon. A single-threaded loop that
swings 15% is telling you about the scheduler and the allocator it is sitting on, not
about the matching engine, and any figure quoted tighter than that swing was never
really measured.

**The same silicon runs this 1.7x faster under Linux.** 21M against 36M, and the Linux
side is a 4-core VM on the same laptop, which if anything should cost it. The engine's
hot path allocates nothing, so the difference is not malloc on the critical path; it is
everything around it. Worth knowing before quoting a throughput without naming an OS.

**The two paths rank differently on the two platforms.** macOS is slower on the
synchronous core and faster on the lock-free pipeline (10.7M vs 8.5M). The async path
is bounded by the queue handoff between producer and consumer rather than by matching,
so it is measuring a different thing, and the ordering flips.

Both benchmarks run correctness asserts (matching, partial fill, cancel) before they
start timing, so a run that prints a throughput is a run whose book behaved.

### Corrections

This table used to read *26M msgs/sec (Apple M2) / 35M+ (Linux aarch64)* for the
synchronous core and *~8.5M (Apple M2)* for the pipeline. Measured properly:

- **26M on the M2 was not reproduced.** Fourteen runs on the machine it names topped out
  at 22.5M, and a later session's best was 25.0M. It reads ~21M now.
- **35M+ on Linux aarch64 was right**, and was verified rather than dropped: 36M with
  g++, 39.8M with clang.
- **~8.5M for the pipeline was a Linux number wearing a macOS label.** macOS measures
  ~10.7M; Linux measures ~8.5M.

## Layout

```
hft-matching-engine/
├── include/
│   ├── Types.hpp        # Order / PriceLevel (64B-aligned), Price/Qty/Id types
│   ├── OrderBook.hpp    # the matching engine interface
│   ├── MemoryPool.hpp   # pre-warmed O(1) object pool
│   ├── Backtester.hpp   # lock-free async pipeline (Boost)
│   └── ItchParser.hpp   # ITCH 5.0 Add Order decoding (sketch; not used by the benchmarks)
├── src/
│   ├── OrderBook.cpp     # matching, cancel, execute
│   └── Benchmark.cpp     # async pipeline benchmark (Boost)
├── bench/
│   ├── sync_benchmark.cpp  # dependency-free core benchmark + smoke test
│   └── run_benchmarks.sh   # reproduces every throughput figure above
├── tests/
│   └── test_orderbook.cpp  # 20 checks, run by CTest
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
  hot path. Float comparison in a price-priority book is a correctness bug, not just
  a slowdown.
- Matching is strict price-time priority: a marketable order walks the opposite book
  from the best level, filling resting orders FIFO within each level.
- The hash-map price lookup is a pragmatic choice; a production system on a known
  tick grid would direct-address an array of levels.

## Complexity, precisely

Claiming "O(1)" deserves the fine print. With $L$ = live price levels:

| Operation | Cost | Why |
|---|---|---|
| Add (resting, existing level) | O(1) avg | hash lookup + tail append |
| Add (resting, **new** level) | O(log L) | ordered-map insert for BBO tracking |
| Cancel | O(1) avg | hash lookup + intrusive-list splice |
| Execute against best | O(1) per fill | `map.begin()` + FIFO head pops |
| Best bid / ask | O(1) | ordered-map front |

The O(1) cancel is the intrusive-list trick: the order struct carries its own
`prev`/`next` pointers, so removal is two pointer writes — no search, no allocator
call. The memory pool converts `new`/`delete` into free-list pops/pushes, which is
what keeps *tail* latency flat, not just the average — allocation is where p99
spikes come from.

Throughput methodology: both benchmarks fire 5,000,000 limit orders of 100 lots,
alternating buy and sell across 100 overlapping price levels, so most orders cross and
match. That exercises insertion, the matching loop and pool reuse; cancels and
executions are covered by the tests, not by the timed loop. Orders are computed inside
the loop from the index, with no random numbers to generate. The test suite (`tests/test_orderbook.cpp`, run via CTest) pins down
price-time priority, limit-respecting book walks, partial fills, and pool reuse
under a 10k-order churn.

## Further reading

- NASDAQ TotalView-ITCH 5.0 specification — the message model `ItchParser.hpp` targets.
- W.K. Selph, *How to Build a Fast Limit Order Book* — the classic write-up of the pointer-based design this follows.

---

*Author: Tejas Pandya — NYU MSFE.*
