#pragma once

#include "Types.hpp"
#include "MemoryPool.hpp"
#include <unordered_map>
#include <map>
#include <vector>

namespace trading {

class OrderBook {
private:
    MemoryPool<Order> order_pool_;
    MemoryPool<PriceLevel> level_pool_;

    // Fast mapping from ID to Order pointers
    std::unordered_map<OrderId, Order*> orders_;
    
    // Fast price level lookup. 
    // In strict HFT, a direct-addressed array mapping price to level is typically used, 
    // but a hash map provides a solid balance for prototype requirements.
    std::unordered_map<Price, PriceLevel*> bid_levels_;
    std::unordered_map<Price, PriceLevel*> ask_levels_;

    // Ordered maps to quickly find the best bid/ask
    std::map<Price, PriceLevel*, std::greater<Price>> bids_;
    std::map<Price, PriceLevel*, std::less<Price>> asks_;

    void add_order_to_level(PriceLevel* level, Order* order);
    void remove_order_from_level(PriceLevel* level, Order* order);

public:
    OrderBook();
    ~OrderBook() = default;

    // Core matching engine operations
    void add_order(OrderId id, Price price, Quantity qty, Side side);
    void cancel_order(OrderId id);
    void execute_order(OrderId id, Quantity qty);
    
    // Accessors
    Price get_best_bid() const;
    Price get_best_ask() const;
    
    void print_top_of_book() const;
};

} // namespace trading
