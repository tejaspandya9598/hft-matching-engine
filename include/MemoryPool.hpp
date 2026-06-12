#pragma once

#include <vector>
#include <cassert>
#include <new>
#include <utility>

namespace trading {

/**
 * @class MemoryPool
 * @brief Zero-allocation, pre-warmed memory pool for low-latency systems.
 * 
 * Pre-allocates blocks of memory to serve Object instantiation in O(1) time
 * avoiding systemic allocation latency from `malloc`/`new`.
 */
template <typename T, size_t BlockSize = 65536>
class MemoryPool {
private:
    union Chunk {
        T data;
        Chunk* next;
        
        // Constructor and Destructor handled explicitly to manage the union
        Chunk() {} 
        ~Chunk() {}
    };

    Chunk* free_list_{nullptr};
    std::vector<Chunk*> blocks_;

    void allocate_block() {
        // Allocate a cache-aligned block of memory
        void* raw_mem = ::operator new(BlockSize * sizeof(Chunk), std::align_val_t(alignof(T)));
        Chunk* new_block = static_cast<Chunk*>(raw_mem);
        blocks_.push_back(new_block);
        
        // Thread the chunks into a free list
        for (size_t i = 0; i < BlockSize - 1; ++i) {
            new_block[i].next = &new_block[i + 1];
        }
        new_block[BlockSize - 1].next = free_list_;
        free_list_ = new_block;
    }

public:
    MemoryPool() {
        allocate_block(); // Pre-warm the pool
    }

    ~MemoryPool() {
        for (auto block : blocks_) {
            ::operator delete(block, std::align_val_t(alignof(T)));
        }
    }

    // Explicitly disable copy and move to ensure strict pool ownership
    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;
    MemoryPool(MemoryPool&&) = delete;
    MemoryPool& operator=(MemoryPool&&) = delete;

    /**
     * @brief Get an object from the pool. 
     * Uses placement new to initialize it in our pre-allocated memory.
     */
    template<typename... Args>
    T* allocate(Args&&... args) {
        if (!free_list_) {
            // If we run out of space, we have to allocate a new block.
            // In a real HFT system, we'd probably want to pre-allocate everything 
            // at startup to avoid this during the hot path.
            allocate_block(); 
        }
        
        Chunk* chunk = free_list_;
        free_list_ = free_list_->next;
        
        return new (&chunk->data) T(std::forward<Args>(args)...);
    }

    /**
     * @brief O(1) return of an object memory to the free list.
     */
    void deallocate(T* ptr) {
        if (ptr) {
            ptr->~T(); // Explicitly call destructor
            Chunk* chunk = reinterpret_cast<Chunk*>(ptr);
            chunk->next = free_list_;
            free_list_ = chunk;
        }
    }
};

} // namespace trading
