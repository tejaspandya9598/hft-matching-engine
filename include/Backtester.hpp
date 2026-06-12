#pragma once

#include "OrderBook.hpp"
#include "ItchParser.hpp"
#include <boost/lockfree/queue.hpp>
#include <thread>
#include <atomic>
#include <iostream>

namespace trading {

struct alignas(64) InboundEvent {
    OperationType type;
    OrderId id;
    Price price;
    Quantity qty;
    Side side;
};

class Backtester {
private:
    OrderBook engine_;
    
    // Use dynamic allocation for the lock-free queue to avoid tagged pointer limits on some platforms
    boost::lockfree::queue<InboundEvent> inbound_queue_{1048576};
    std::atomic<bool> running_{false};
    std::thread worker_thread_;

    void process_queue() {
        InboundEvent event;
        while (running_.load(std::memory_order_acquire)) {
            while (inbound_queue_.pop(event)) {
                switch (event.type) {
                    case OperationType::ADD:
                        engine_.add_order(event.id, event.price, event.qty, event.side);
                        break;
                    case OperationType::CANCEL:
                        engine_.cancel_order(event.id);
                        break;
                    case OperationType::EXECUTE:
                        engine_.execute_order(event.id, event.qty);
                        break;
                    default:
                        break;
                }
            }
            // In a strict HFT setting we busy wait. For testing, we may yield slightly.
        }
    }

public:
    Backtester() = default;
    
    ~Backtester() { 
        stop(); 
    }

    void start() {
        if (!running_.exchange(true)) {
            // Start the background matching thread
            worker_thread_ = std::thread(&Backtester::process_queue, this);
            
            // Try to pin the thread to a specific core for better performance
            #ifdef __linux__
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(1, &cpuset); 
            pthread_setaffinity_np(worker_thread_.native_handle(), sizeof(cpu_set_t), &cpuset);
            #endif
        }
    }

    void stop() {
        if (running_.exchange(false)) {
            // Give the worker a chance to drain remaining items
            while (!inbound_queue_.empty()) {
                std::this_thread::yield();
            }
            if (worker_thread_.joinable()) {
                worker_thread_.join();
            }
        }
    }

    bool is_queue_empty() const { return inbound_queue_.empty(); }

    void enqueue_event(const InboundEvent& event) {
        while (!inbound_queue_.push(event)) {
            // Spin-wait until space frees up in the ring buffer
        }
    }

    OrderBook& get_engine() { return engine_; }
};

} // namespace trading
