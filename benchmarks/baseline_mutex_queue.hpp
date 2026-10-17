/// @file baseline_mutex_queue.hpp
/// @brief The "obvious" thread-safe queue: std::mutex + std::deque, bounded to the same capacity
/// as the ring it is compared with. Kept as the reference point for M1.9 and M9 comparisons.
/// Deliberately simple: no tricks, so the comparison shows what the lock-free design buys.
#pragma once

#include <cstddef>
#include <deque>
#include <mutex>

namespace fastbus::bench {

template <typename T>
class MutexQueue {
public:
    explicit MutexQueue(std::size_t capacity) : capacity_(capacity) {}

    [[nodiscard]] bool try_push(const T& value) {
        const std::scoped_lock lock(mutex_);
        if (items_.size() >= capacity_) {
            return false;
        }
        items_.push_back(value);  // may allocate a new deque block
        return true;
    }

    [[nodiscard]] bool try_pop(T& out) {
        const std::scoped_lock lock(mutex_);
        if (items_.empty()) {
            return false;
        }
        out = items_.front();
        items_.pop_front();
        return true;
    }

private:
    std::size_t capacity_;
    std::mutex mutex_;
    std::deque<T> items_;
};

}  // namespace fastbus::bench
