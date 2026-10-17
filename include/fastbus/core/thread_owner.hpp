/// @file thread_owner.hpp
/// @brief Debug-only check that one role (e.g. "SPSC producer") is used from a single thread.
/// The first call binds the role to the calling thread; later calls from another thread fail fast.
/// In release builds (`kDebugChecks == false`) `check()` compiles to nothing.
#pragma once

#include <atomic>
#include <cstdint>

#include "fastbus/core/assert.hpp"
#include "fastbus/platform/thread.hpp"

namespace fastbus::core {

class ThreadOwner {
public:
    ThreadOwner() noexcept = default;
    ~ThreadOwner() = default;
    // Copies take over the binding (views are copied before they are shared between threads).
    ThreadOwner(const ThreadOwner& other) noexcept
        : id_(other.id_.load(std::memory_order_relaxed)) {}
    ThreadOwner& operator=(const ThreadOwner& other) noexcept {
        if (this != &other) {
            id_.store(other.id_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        }
        return *this;
    }
    ThreadOwner(ThreadOwner&&) = delete;  // copy is cheap; keep one well-defined semantic
    ThreadOwner& operator=(ThreadOwner&&) = delete;

    /// Binds on first use, then fails fast with `what` if called from a different thread.
    void check(const char* what) noexcept {
        if constexpr (kDebugChecks) {
            const std::uint64_t me = current_id();
            std::uint64_t expected = 0;  // OS thread ids are never 0
            if (!id_.compare_exchange_strong(expected, me, std::memory_order_relaxed) &&
                expected != me) {
                fail_fast(what);
            }
        }
    }

private:
    static std::uint64_t current_id() noexcept {
        thread_local const std::uint64_t kId = platform::current_thread_id();  // one syscall
        return kId;
    }

    std::atomic<std::uint64_t> id_{0};
};

}  // namespace fastbus::core
