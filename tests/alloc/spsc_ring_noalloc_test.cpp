// Zero-allocation test for the SPSC ring (M1.9, PRD_spsc_ring.md section 8). This binary
// replaces the global operator new/delete with counting versions. Counting is armed only after
// setup and warm-up, so the measured window contains nothing but ring operations; every heap
// allocation inside it (from any thread) is a failure.
//
// It is its own executable because replacing operator new is process-wide.
#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <thread>
#include <utility>

#include "fastbus/core/spsc_ring.hpp"

#if defined(_WIN32)
#include <malloc.h>  // _aligned_malloc / _aligned_free
#endif

namespace {

std::atomic<bool> g_armed{false};
std::atomic<std::uint64_t> g_allocations{0};

void* counted_alloc(std::size_t size, std::size_t alignment) {
    if (g_armed.load(std::memory_order_relaxed)) {
        g_allocations.fetch_add(1, std::memory_order_relaxed);
    }
    size = size == 0 ? 1 : size;
    void* p = nullptr;
    // NOLINTBEGIN(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
    if (alignment <= alignof(std::max_align_t)) {
        p = std::malloc(size);
    } else {
#if defined(_WIN32)
        p = _aligned_malloc(size, alignment);
#else
        p = std::aligned_alloc(alignment, (size + alignment - 1) / alignment * alignment);
#endif
    }
    // NOLINTEND(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
    return p;
}

void counted_free(void* p, std::size_t alignment) noexcept {
    // NOLINTBEGIN(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
#if defined(_WIN32)
    if (alignment > alignof(std::max_align_t)) {
        _aligned_free(p);
        return;
    }
#else
    static_cast<void>(alignment);
#endif
    std::free(p);
    // NOLINTEND(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
}

// The throwing operator new must never return null. A test binary that runs out of memory has
// no useful way to continue, so it aborts instead of throwing (fastbus code never throws, and
// the Windows clang-tidy run treats exceptions as disabled).
void* checked_alloc(std::size_t size, std::size_t alignment) {
    void* p = counted_alloc(size, alignment);
    if (p == nullptr) {
        std::abort();
    }
    return p;
}

/// Arms the counter for its lifetime and reports how many allocations happened inside.
class AllocationWindow {
public:
    AllocationWindow() noexcept {
        g_allocations.store(0);
        g_armed.store(true);
    }
    ~AllocationWindow() { g_armed.store(false); }
    AllocationWindow(const AllocationWindow&) = delete;
    AllocationWindow& operator=(const AllocationWindow&) = delete;
    AllocationWindow(AllocationWindow&&) = delete;
    AllocationWindow& operator=(AllocationWindow&&) = delete;

    [[nodiscard]] static std::uint64_t count() noexcept { return g_allocations.load(); }
};

}  // namespace

// ---- Replacements of every global allocation function ([new.delete]) --------------------------
// NOLINTBEGIN(misc-new-delete-overloads,cppcoreguidelines-owning-memory)
void* operator new(std::size_t size) {
    return checked_alloc(size, 0);
}
void* operator new[](std::size_t size) {
    return checked_alloc(size, 0);
}
void* operator new(std::size_t size, std::align_val_t al) {
    return checked_alloc(size, static_cast<std::size_t>(al));
}
void* operator new[](std::size_t size, std::align_val_t al) {
    return checked_alloc(size, static_cast<std::size_t>(al));
}
void* operator new(std::size_t size, const std::nothrow_t& /*tag*/) noexcept {
    return counted_alloc(size, 0);
}
void* operator new[](std::size_t size, const std::nothrow_t& /*tag*/) noexcept {
    return counted_alloc(size, 0);
}
void* operator new(std::size_t size, std::align_val_t al, const std::nothrow_t& /*tag*/) noexcept {
    return counted_alloc(size, static_cast<std::size_t>(al));
}
void* operator new[](std::size_t size, std::align_val_t al,
                     const std::nothrow_t& /*tag*/) noexcept {
    return counted_alloc(size, static_cast<std::size_t>(al));
}
void operator delete(void* p) noexcept {
    counted_free(p, 0);
}
void operator delete[](void* p) noexcept {
    counted_free(p, 0);
}
void operator delete(void* p, std::size_t /*size*/) noexcept {
    counted_free(p, 0);
}
void operator delete[](void* p, std::size_t /*size*/) noexcept {
    counted_free(p, 0);
}
void operator delete(void* p, std::align_val_t al) noexcept {
    counted_free(p, static_cast<std::size_t>(al));
}
void operator delete[](void* p, std::align_val_t al) noexcept {
    counted_free(p, static_cast<std::size_t>(al));
}
void operator delete(void* p, std::size_t /*size*/, std::align_val_t al) noexcept {
    counted_free(p, static_cast<std::size_t>(al));
}
void operator delete[](void* p, std::size_t /*size*/, std::align_val_t al) noexcept {
    counted_free(p, static_cast<std::size_t>(al));
}
void operator delete(void* p, const std::nothrow_t& /*tag*/) noexcept {
    counted_free(p, 0);
}
void operator delete[](void* p, const std::nothrow_t& /*tag*/) noexcept {
    counted_free(p, 0);
}
void operator delete(void* p, std::align_val_t al, const std::nothrow_t& /*tag*/) noexcept {
    counted_free(p, static_cast<std::size_t>(al));
}
void operator delete[](void* p, std::align_val_t al, const std::nothrow_t& /*tag*/) noexcept {
    counted_free(p, static_cast<std::size_t>(al));
}
// NOLINTEND(misc-new-delete-overloads,cppcoreguidelines-owning-memory)

namespace {

using Ring = fastbus::core::SpscRing<std::uint64_t>;

Ring make_ring(std::size_t capacity) {
    auto ring = Ring::create(capacity);  // the one allocation, outside any window
    EXPECT_TRUE(ring.has_value());
    return std::move(ring).value();
}

TEST(NoAlloc, CounterSeesAllocations) {
    // Proves the hook is live; otherwise "0 allocations" below would prove nothing.
    std::uint64_t seen = 0;
    {
        const AllocationWindow window;
        auto* p = new int{42};  // NOLINT(cppcoreguidelines-owning-memory): deliberate
        delete p;               // NOLINT(cppcoreguidelines-owning-memory)
        seen = AllocationWindow::count();
    }
    EXPECT_EQ(seen, 1U);
}

TEST(NoAlloc, CreateAllocatesExactlyOnce) {
    std::uint64_t seen = 0;
    {
        const AllocationWindow window;
        auto ring = Ring::create(1024);
        seen = AllocationWindow::count();
        EXPECT_TRUE(ring.has_value());
    }
    EXPECT_EQ(seen, 1U);
}

TEST(NoAlloc, SingleThreadOperationsDoNotAllocate) {
    auto ring = make_ring(64);
    std::uint64_t out = 0;
    ASSERT_TRUE(ring.try_push(0));  // warm-up: binds producer/consumer owners (debug builds)
    ASSERT_TRUE(ring.try_pop(out));

    std::uint64_t seen = 0;
    std::uint64_t sum = 0;
    {
        const AllocationWindow window;
        for (std::uint64_t i = 0; i < 100'000; ++i) {
            if (i % 2 == 0) {
                static_cast<void>(ring.try_push(i));
                static_cast<void>(ring.try_pop(out));
            } else if (std::uint64_t* slot = ring.try_claim(); slot != nullptr) {
                *slot = i;
                ring.commit();
                if (const std::uint64_t* peeked = ring.try_peek(); peeked != nullptr) {
                    out = *peeked;
                    ring.release();
                }
            }
            sum += out + ring.size_approx() + static_cast<std::uint64_t>(ring.empty_approx());
        }
        seen = AllocationWindow::count();
    }
    EXPECT_EQ(seen, 0U);
    EXPECT_GT(sum, 0U);
}

TEST(NoAlloc, TwoThreadTransferDoesNotAllocate) {
    constexpr std::uint64_t kItems = 200'000;
    auto ring = make_ring(256);
    auto& view = ring.view();
    std::atomic<int> ready{0};
    std::atomic<bool> go{false};
    std::uint64_t received = 0;

    // Threads are created and warmed up before the window opens: thread creation allocates.
    std::thread consumer([&] {
        std::uint64_t out = 0;
        static_cast<void>(view.try_pop(out));  // binds the consumer owner
        ready.fetch_add(1);
        while (!go.load()) {
            std::this_thread::yield();
        }
        while (received < kItems) {
            if (view.try_pop(out)) {
                ++received;
            }
        }
    });
    std::thread producer([&] {
        static_cast<void>(view.try_claim());  // binds the producer owner; publishes nothing
        ready.fetch_add(1);
        while (!go.load()) {
            std::this_thread::yield();
        }
        for (std::uint64_t i = 0; i < kItems;) {
            if (view.try_push(i)) {
                ++i;
            }
        }
    });
    while (ready.load() < 2) {
        std::this_thread::yield();
    }

    std::uint64_t seen = 0;
    {
        const AllocationWindow window;
        go.store(true);
        producer.join();
        while (view.size_approx() != 0) {
            std::this_thread::yield();
        }
        seen = AllocationWindow::count();
    }
    consumer.join();
    EXPECT_EQ(seen, 0U);
    EXPECT_EQ(received, kItems);
}

}  // namespace
