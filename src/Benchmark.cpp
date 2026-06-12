#include "Backtester.hpp"
#include <chrono>
#include <iostream>
#include <vector>

using namespace trading;

int main() {
    std::cout << "--- High-Performance LOB Matching Engine Benchmark ---\n";
    
    Backtester backtester;
    backtester.start();
    
    const size_t NUM_EVENTS = 5'000'000;
    
    std::cout << "[*] Generating and ingesting " << NUM_EVENTS << " events..." << std::endl;
    auto start_time = std::chrono::high_resolution_clock::now();
    
    for (size_t i = 1; i <= NUM_EVENTS; ++i) {
        InboundEvent event;
        event.type = OperationType::ADD;
        event.id = i;
        event.price = 10000 + (i % 100); 
        event.qty = 100;
        event.side = (i % 2 == 0) ? Side::BUY : Side::SELL;
        
        backtester.enqueue_event(event);
    }
    
    std::cout << "[*] Draining queue..." << std::endl;
    while (!backtester.is_queue_empty()) {
        std::this_thread::yield();
    }
    
    backtester.stop();
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
    
    if (elapsed_us <= 0) elapsed_us = 1;
    
    std::cout << "[+] Processing complete.\n";
    std::cout << "------------------------------------------\n";
    std::cout << "Events processed : " << NUM_EVENTS << "\n";
    std::cout << "Total time (us)  : " << elapsed_us << "\n";
    std::cout << "Throughput       : " << (NUM_EVENTS * 1'000'000ULL / elapsed_us) << " msgs/sec\n";
    std::cout << "------------------------------------------\n";
    
    std::cout << "Final Top of Book:\n";
    backtester.get_engine().print_top_of_book();
    
    return 0;
}
