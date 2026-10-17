/// @file clock.hpp
/// @brief Injectable time (ADR-007): modules take `const Clock&` and never read OS clocks.
///
///   SystemMonotonicClock  system-wide monotonic time; use for latency, timeouts, deadlines
///   WallClock             UTC since 1970; use only for human-readable timestamps (it can jump)
///   ManualClock           test/simulation time, moved explicitly with set()/advance()
#pragma once

#include <atomic>
#include <cstdint>

#include "fastbus/core/time.hpp"
#include "fastbus/platform/time_source.hpp"

namespace fastbus::core {

/// Source of time. Implementations must be thread-safe: `now()` may be called concurrently.
class Clock {
public:
    virtual ~Clock() = default;
    [[nodiscard]] virtual Timestamp now() const noexcept = 0;

protected:
    Clock() = default;
    Clock(const Clock&) = default;
    Clock& operator=(const Clock&) = default;
    Clock(Clock&&) = default;
    Clock& operator=(Clock&&) = default;
};

/// Monotonic, system-wide time (the same timeline in every process on the machine).
class SystemMonotonicClock final : public Clock {
public:
    [[nodiscard]] Timestamp now() const noexcept override { return read(); }
    /// Non-virtual read for hot loops that cannot afford the indirect call.
    [[nodiscard]] static Timestamp read() noexcept {
        return Timestamp::from_ns(platform::monotonic_now_ns());
    }
    /// Tick length of the underlying OS counter.
    [[nodiscard]] static Duration resolution() noexcept {
        return Duration::nanoseconds(platform::monotonic_resolution_ns());
    }
};

/// UTC wall-clock time (nanoseconds since 1970). Not monotonic: never use it for intervals.
class WallClock final : public Clock {
public:
    [[nodiscard]] Timestamp now() const noexcept override {
        return Timestamp::from_ns(platform::wall_now_ns());
    }
};

/// Time that only moves when told to. Thread-safe: one thread may advance while others read.
class ManualClock final : public Clock {
public:
    constexpr explicit ManualClock(Timestamp start = Timestamp{}) noexcept
        : ns_{start.ns_since_epoch()} {}

    [[nodiscard]] Timestamp now() const noexcept override {
        // acquire pairs with the release in set()/advance(): a reader that sees the new time
        // also sees everything the advancing thread did before moving the clock.
        return Timestamp::from_ns(ns_.load(std::memory_order_acquire));
    }
    void set(Timestamp t) noexcept { ns_.store(t.ns_since_epoch(), std::memory_order_release); }
    /// Moves time forward (or backward for a negative `d`; tests of non-monotonic clocks only).
    void advance(Duration d) noexcept { ns_.fetch_add(d.count_ns(), std::memory_order_acq_rel); }

private:
    static_assert(std::atomic<std::int64_t>::is_always_lock_free);
    std::atomic<std::int64_t> ns_;
};

}  // namespace fastbus::core
