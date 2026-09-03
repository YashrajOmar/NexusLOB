#pragma once

#include <cstdint>
#include <vector>
#include <stack>
#include <mutex>

namespace app {

// MemoryPool<T>: a simple arena allocator that pre-allocates objects.
//
// Instead of calling `new` for every order, the pool pre-allocates a block
// of N objects at startup. allocate() returns a pre-built object from the
// free list. deallocate() returns it to the pool.
//
// This achieves deterministic zero-allocation latency — no system calls to
// the heap allocator during matching. Inspired by LMAX Disruptor's approach.
//
// Thread safety: internal mutex. In a single-threaded matching engine
// (the raft apply path), the mutex is uncontended.
template <typename T>
class MemoryPool {
public:
    explicit MemoryPool(size_t initialSize = 1024) {
        freeList_.reserve(initialSize);
        for (size_t i = 0; i < initialSize; ++i) {
            freeList_.push_back(new T());
        }
    }

    ~MemoryPool() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (T* obj : freeList_) delete obj;
        freeList_.clear();
    }

    T* allocate() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (freeList_.empty()) {
            T* obj = new T();
            return obj;
        }
        T* obj = freeList_.back();
        freeList_.pop_back();
        return obj;
    }

    void deallocate(T* obj) {
        if (!obj) return;
        std::lock_guard<std::mutex> lock(mutex_);
        freeList_.push_back(obj);
    }

    size_t available() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return freeList_.size();
    }

private:
    mutable std::mutex mutex_;
    std::vector<T*> freeList_;
};

} // namespace app
