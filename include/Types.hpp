#pragma once

#include <cstdint>
#include <cstddef>
#include <iostream>

namespace trading {

enum class Side : uint8_t {
    BUY = 0,
    SELL = 1
};

// Represents prices up to 4 implied decimal places, avoiding floating point operations.
using Price = uint64_t;
using Quantity = uint32_t;
using OrderId = uint64_t;

// Cache-aligned (64 bytes) to prevent false sharing and ensure strict 1-cache-line fetches.
struct alignas(64) Order {
    OrderId id;
    Price price;
    Quantity qty;
    Side side;
    
    // Intrusive doubly-linked list pointers for O(1) removal
    Order* next = nullptr;
    Order* prev = nullptr;

    Order() = default;
    Order(OrderId i, Price p, Quantity q, Side s) 
        : id(i), price(p), qty(q), side(s), next(nullptr), prev(nullptr) {}
};

// Represents an aggregated price level
struct alignas(64) PriceLevel {
    Price price = 0;
    Quantity total_qty = 0;
    Order* head = nullptr;
    Order* tail = nullptr;

    PriceLevel() = default;
    explicit PriceLevel(Price p) : price(p), total_qty(0), head(nullptr), tail(nullptr) {}
};

// Enum representing the type of operation parsed from an ITCH message
enum class OperationType : uint8_t {
    ADD,
    EXECUTE,
    CANCEL,
    DELETE,
    REPLACE
};

} // namespace trading
