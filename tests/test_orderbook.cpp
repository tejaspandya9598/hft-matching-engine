// Correctness tests for the matching engine. No framework - a CHECK macro and
// main() keep the repo dependency-free; CTest just runs the binary.

#include "OrderBook.hpp"

#include <cstdio>
#include <cstdlib>

static int failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);   \
            ++failures;                                                   \
        }                                                                 \
    } while (0)

using namespace trading;

// Prices are fixed-point with 4 implied decimals: 100.50 -> 1005000
static constexpr Price px(double p) { return static_cast<Price>(p * 10000); }

static void test_resting_orders_set_bbo() {
    OrderBook book;
    book.add_order(1, px(100.50), 10, Side::BUY);
    book.add_order(2, px(101.00), 5, Side::SELL);
    CHECK(book.get_best_bid() == px(100.50));
    CHECK(book.get_best_ask() == px(101.00));

    // tighter quotes improve the BBO
    book.add_order(3, px(100.75), 7, Side::BUY);
    book.add_order(4, px(100.90), 3, Side::SELL);
    CHECK(book.get_best_bid() == px(100.75));
    CHECK(book.get_best_ask() == px(100.90));
}

static void test_crossing_order_executes() {
    OrderBook book;
    book.add_order(1, px(101.00), 10, Side::SELL);
    // aggressive buy lifts the offer completely and rests nothing
    book.add_order(2, px(101.00), 10, Side::BUY);
    CHECK(book.get_best_ask() == 0);
    CHECK(book.get_best_bid() == 0);
}

static void test_partial_fill_rests_remainder() {
    OrderBook book;
    book.add_order(1, px(101.00), 4, Side::SELL);
    book.add_order(2, px(101.00), 10, Side::BUY);
    // 4 filled, 6 rest on the bid at 101.00
    CHECK(book.get_best_ask() == 0);
    CHECK(book.get_best_bid() == px(101.00));
}

static void test_walk_the_book_respects_limit() {
    OrderBook book;
    book.add_order(1, px(101.00), 5, Side::SELL);
    book.add_order(2, px(101.50), 5, Side::SELL);
    book.add_order(3, px(102.00), 5, Side::SELL);
    // buy 15 limit 101.50: fills 101.00 and 101.50, must NOT touch 102.00
    book.add_order(4, px(101.50), 15, Side::BUY);
    CHECK(book.get_best_ask() == px(102.00));
    // unfilled 5 lots rest at the limit price
    CHECK(book.get_best_bid() == px(101.50));
}

static void test_fifo_within_level() {
    OrderBook book;
    book.add_order(1, px(101.00), 5, Side::SELL);
    book.add_order(2, px(101.00), 5, Side::SELL);
    // incoming buy for 5 must fill order 1 (time priority); order 2 remains
    book.add_order(3, px(101.00), 5, Side::BUY);
    // cancelling the survivor empties the level -> proves order 2 was the one left
    book.cancel_order(2);
    CHECK(book.get_best_ask() == 0);
    // cancelling order 1 now is a no-op (already filled and removed)
    book.cancel_order(1);
    CHECK(book.get_best_ask() == 0);
}

static void test_cancel_removes_level_when_empty() {
    OrderBook book;
    book.add_order(1, px(100.00), 10, Side::BUY);
    book.add_order(2, px(99.50), 10, Side::BUY);
    book.cancel_order(1);
    CHECK(book.get_best_bid() == px(99.50));
    book.cancel_order(2);
    CHECK(book.get_best_bid() == 0);
}

static void test_execute_partial_and_full() {
    OrderBook book;
    book.add_order(1, px(100.00), 10, Side::BUY);
    book.execute_order(1, 4);           // partial: 6 left, level stays
    CHECK(book.get_best_bid() == px(100.00));
    book.execute_order(1, 6);           // full: order and level removed
    CHECK(book.get_best_bid() == 0);
}

static void test_unknown_ids_are_noops() {
    OrderBook book;
    book.cancel_order(42);
    book.execute_order(42, 1);
    CHECK(book.get_best_bid() == 0);
    CHECK(book.get_best_ask() == 0);
}

static void test_pool_reuse_after_churn() {
    OrderBook book;
    // churn through more orders than any initial pool block to force reuse
    for (OrderId id = 1; id <= 10000; ++id) {
        book.add_order(id, px(100.00) + (id % 50) * 100, 10, Side::BUY);
        if (id % 2 == 0) book.cancel_order(id - 1);
    }
    CHECK(book.get_best_bid() != 0);
    // book still consistent: cancel everything that could remain
    for (OrderId id = 1; id <= 10000; ++id) book.cancel_order(id);
    CHECK(book.get_best_bid() == 0);
}

int main() {
    test_resting_orders_set_bbo();
    test_crossing_order_executes();
    test_partial_fill_rests_remainder();
    test_walk_the_book_respects_limit();
    test_fifo_within_level();
    test_cancel_removes_level_when_empty();
    test_execute_partial_and_full();
    test_unknown_ids_are_noops();
    test_pool_reuse_after_churn();

    if (failures == 0) {
        std::printf("all tests passed\n");
        return EXIT_SUCCESS;
    }
    std::printf("%d check(s) failed\n", failures);
    return EXIT_FAILURE;
}
