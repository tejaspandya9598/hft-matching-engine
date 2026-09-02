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

| Path | Throughput | Measured |
|---|---|---|
| Synchronous core (`hft_sync_benchmark`) | **~21M msgs/sec** (Apple M2, 8 cores) | yes — 9 runs, see below |
| End-to-end lock-free pipeline (`hft_benchmark`) | not currently measured | needs Boost, which is not installed here |

The synchronous figure is what the included benchmark actually prints on the
machine described. Nine runs, built with the flags in `CMakeLists.txt`
(`-O3 -march=native -flto`):

```
19.55  19.71  20.80  21.21  21.75  21.82  21.88  22.45  22.50   (M msgs/sec)
median 21.75   range 19.55 – 22.50
```

Quote it as ~21M, not as a single number: the spread across runs on an
8-core laptop under a normal desktop load is about 15%, and a headline figure
narrower than the measurement noise is a figure that has not been measured.

The benchmark also runs correctness asserts (matching, partial fill, cancel)
before it starts timing, so a run that prints a throughput is a run whose book
behaved.

Two figures used to sit in this table that are not here any more: 26M msgs/sec
on the same M2, and 35M+ on Linux aarch64. The first does not reproduce — nine
attempts on the machine it names topped out at 22.5M. The second may well be
right, but there is no aarch64 Linux host to run it on, and an unreproduced
number is not a result.

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

Throughput methodology: the benchmark pre-generates a uniform add/cancel/execute
message mix, then times the pure matching loop — the number measures the engine,
not the RNG. The test suite (`tests/test_orderbook.cpp`, run via CTest) pins down
price-time priority, limit-respecting book walks, partial fills, and pool reuse
under a 10k-order churn.

## Further reading

- NASDAQ TotalView-ITCH 5.0 specification — the message model `ItchParser.hpp` targets.
- W.K. Selph, *How to Build a Fast Limit Order Book* — the classic write-up of the pointer-based design this follows.

---

*Author: Tejas Pandya — NYU MSFE.*
