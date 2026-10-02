#include "OrderBook.hpp"
#include <algorithm>
#include <cassert>

namespace trading {

OrderBook::OrderBook() {
    orders_.reserve(1000000);
    bid_levels_.reserve(100000);
    ask_levels_.reserve(100000);
}

void OrderBook::add_order_to_level(PriceLevel* level, Order* order) {
    if (!level || !order) return;
    if (!level->head) {
        level->head = order;
        level->tail = order;
        order->prev = nullptr;
        order->next = nullptr;
    } else {
        level->tail->next = order;
        order->prev = level->tail;
        order->next = nullptr;
        level->tail = order;
    }
    level->total_qty += order->qty;
}

void OrderBook::remove_order_from_level(PriceLevel* level, Order* order) {
    if (!level || !order) return;

    if (order->prev) {
        order->prev->next = order->next;
    } else {
        level->head = order->next; 
    }

    if (order->next) {
        order->next->prev = order->prev;
    } else {
        level->tail = order->prev; 
    }

    // Quantity is uint32_t, so a desynced book would silently wrap to ~4 billion here
    // rather than going negative. Assert in debug, clamp in release: a level that is
    // short on quantity is a bug, but a level claiming 4e9 lots would corrupt every
    // downstream size decision.
    assert(level->total_qty >= order->qty && "level quantity underflow on removal");
    level->total_qty = (level->total_qty >= order->qty) ? level->total_qty - order->qty : 0;
    order->next = nullptr;
    order->prev = nullptr;
}

void OrderBook::destroy_level(PriceLevel* level) {
    if (!level) return;
    if (level->side_is_bid) {
        bid_levels_.erase(level->price);
        bids_.erase(level->bid_it);
    } else {
        ask_levels_.erase(level->price);
        asks_.erase(level->ask_it);
    }
    level_pool_.deallocate(level);
}

void OrderBook::add_order(OrderId id, Price price, Quantity qty, Side side) {
    // A zero-quantity order has no book effect. Returning early keeps it out of
    // `orders_`, so a later cancel for the same id is a clean no-op rather than a
    // lookup that half-succeeds.
    if (qty == 0) return;

    Quantity remaining_qty = qty;

    // Check if we can match this order immediately against the opposite side
    if (side == Side::BUY) {
        while (remaining_qty > 0 && !asks_.empty()) {
            auto best_ask_it = asks_.begin();
            PriceLevel* level = best_ask_it->second;
            
            // If the best ask is higher than our buy price, we can't match
            if (level->price > price) break;

            Order* resting = level->head;
            while (resting && remaining_qty > 0) {
                Quantity exec_qty = std::min(remaining_qty, resting->qty);
                resting->qty -= exec_qty;
                level->total_qty -= exec_qty;
                remaining_qty -= exec_qty;

                Order* next_resting = resting->next;
                if (resting->qty == 0) {
                    // Fully filled, remove it from the book
                    remove_order_from_level(level, resting);
                    orders_.erase(resting->id);
                    order_pool_.deallocate(resting);
                }
                resting = next_resting;
            }

            // If the whole price level is empty, clean it up
            if (level->total_qty == 0) {
                destroy_level(level);
            }
        }
    } else {
        while (remaining_qty > 0 && !bids_.empty()) {
            auto best_bid_it = bids_.begin();
            PriceLevel* level = best_bid_it->second;
            
            // If the best bid is lower than our sell price, no match
            if (level->price < price) break;

            Order* resting = level->head;
            while (resting && remaining_qty > 0) {
                Quantity exec_qty = std::min(remaining_qty, resting->qty);
                resting->qty -= exec_qty;
                level->total_qty -= exec_qty;
                remaining_qty -= exec_qty;

                Order* next_resting = resting->next;
                if (resting->qty == 0) {
                    remove_order_from_level(level, resting);
                    orders_.erase(resting->id);
                    order_pool_.deallocate(resting);
                }
                resting = next_resting;
            }

            if (level->total_qty == 0) {
                destroy_level(level);
            }
        }
    }

    // If there's still quantity left, add it as a resting order
    if (remaining_qty == 0) return;

    Order* new_order = order_pool_.allocate(id, price, remaining_qty, side);
    orders_[id] = new_order;

    PriceLevel* level = nullptr;
    if (side == Side::BUY) {
        auto it = bid_levels_.find(price);
        if (it == bid_levels_.end()) {
            // New price level needed
            level = level_pool_.allocate(price);
            level->side_is_bid = true;
            bid_levels_[price] = level;
            level->bid_it = bids_.emplace(price, level).first;
        } else {
            level = it->second;
        }
    } else {
        auto it = ask_levels_.find(price);
        if (it == ask_levels_.end()) {
            level = level_pool_.allocate(price);
            level->side_is_bid = false;
            ask_levels_[price] = level;
            level->ask_it = asks_.emplace(price, level).first;
        } else {
            level = it->second;
        }
    }
    add_order_to_level(level, new_order);
}

void OrderBook::cancel_order(OrderId id) {
    auto it = orders_.find(id);
    if (it == orders_.end()) return;

    Order* order = it->second;
    auto& side_levels = (order->side == Side::BUY) ? bid_levels_ : ask_levels_;
    auto lit = side_levels.find(order->price);
    if (lit != side_levels.end()) {
        PriceLevel* level = lit->second;
        remove_order_from_level(level, order);
        if (level->total_qty == 0) destroy_level(level);
    }
    orders_.erase(it);
    order_pool_.deallocate(order);
}

void OrderBook::execute_order(OrderId id, Quantity qty) {
    auto it = orders_.find(id);
    if (it == orders_.end()) return;

    Order* order = it->second;
    if (qty >= order->qty) {
        // An execution for at least the resting size retires the order outright. Any
        // excess is ignored: the exchange cannot fill more than is on the book, so a
        // larger qty means the feed and the book have diverged.
        cancel_order(id);
        return;
    }

    order->qty -= qty;
    auto& side_levels = (order->side == Side::BUY) ? bid_levels_ : ask_levels_;
    auto lit = side_levels.find(order->price);
    if (lit != side_levels.end()) {
        PriceLevel* level = lit->second;
        assert(level->total_qty >= qty && "level quantity underflow on execution");
        level->total_qty = (level->total_qty >= qty) ? level->total_qty - qty : 0;
    }
}

Price OrderBook::get_best_bid() const {
    return bids_.empty() ? 0 : bids_.begin()->first;
}

Price OrderBook::get_best_ask() const {
    return asks_.empty() ? 0 : asks_.begin()->first;
}

void OrderBook::print_top_of_book() const {
    Price bb = get_best_bid();
    Quantity bb_qty = 0;
    if (bb > 0) {
        auto it = bid_levels_.find(bb);
        if (it != bid_levels_.end()) bb_qty = it->second->total_qty;
    }

    Price ba = get_best_ask();
    Quantity ba_qty = 0;
    if (ba > 0) {
        auto it = ask_levels_.find(ba);
        if (it != ask_levels_.end()) ba_qty = it->second->total_qty;
    }

    // qty @ price on both sides (the ask used to print price @ qty).
    std::cout << "BBO: " << bb_qty << " @ " << bb << " | " << ba_qty << " @ " << ba << "\n";
}

} // namespace trading
