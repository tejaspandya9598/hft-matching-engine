#!/usr/bin/env bash
# Reproduce every throughput figure in the README.
#
#   ./bench/run_benchmarks.sh            # this host
#   ./bench/run_benchmarks.sh --linux    # also inside a linux/arm64 container
#
# Each path is run REPS times and every run is printed. A single number hides the
# run-to-run spread, and on a laptop under desktop load that spread is the story:
# the synchronous core varies ~15% on macOS and under 1% on Linux.
set -euo pipefail

REPS=${REPS:-5}
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${TMPDIR:-/tmp}/hft-bench"
mkdir -p "$OUT"

CXX=${CXX:-c++}
# clang on aarch64 rejects -march=native; pick whichever the compiler accepts.
if echo 'int main(){}' | "$CXX" -march=native -x c++ - -o /dev/null 2>/dev/null; then
    TUNE="-march=native"
elif echo 'int main(){}' | "$CXX" -mcpu=native -x c++ - -o /dev/null 2>/dev/null; then
    TUNE="-mcpu=native"
else
    TUNE=""
fi
FLAGS="-std=c++20 -O3 $TUNE -flto"

echo "host      : $(uname -s) $(uname -m), $( (nproc 2>/dev/null || sysctl -n hw.ncpu) ) cores"
echo "compiler  : $($CXX --version | head -1)"
echo "flags     : $FLAGS"
echo

echo "== synchronous core (no dependencies) =="
$CXX $FLAGS -I"$ROOT/include" "$ROOT/src/OrderBook.cpp" "$ROOT/bench/sync_benchmark.cpp" -o "$OUT/sync"
for _ in $(seq "$REPS"); do "$OUT/sync" | grep -i throughput; done

echo
echo "== end-to-end lock-free pipeline (needs Boost.Lockfree) =="
BOOST_INC=""
for d in /opt/homebrew/include /usr/local/include /usr/include; do
    [ -f "$d/boost/lockfree/queue.hpp" ] && BOOST_INC="-I$d" && break
done
if [ -z "$BOOST_INC" ]; then
    echo "  Boost.Lockfree not found - skipping."
    echo "  macOS: brew install boost   Debian/Ubuntu: apt-get install libboost-dev"
else
    $CXX $FLAGS -I"$ROOT/include" $BOOST_INC "$ROOT/src/OrderBook.cpp" "$ROOT/src/Benchmark.cpp" \
        -o "$OUT/async" -lpthread
    for _ in $(seq "$REPS"); do "$OUT/async" | grep -i throughput; done
fi

if [ "${1:-}" = "--linux" ]; then
    echo
    echo "== same benchmarks inside a linux/arm64 container =="
    command -v docker >/dev/null || { echo "  docker not found - skipping."; exit 0; }
    docker run --rm --platform linux/arm64 -v "$ROOT":/src -w /src debian:bookworm-slim bash -c "
        apt-get update -qq >/dev/null && apt-get install -y -qq g++ libboost-dev >/dev/null
        REPS=$REPS ./bench/run_benchmarks.sh"
fi
