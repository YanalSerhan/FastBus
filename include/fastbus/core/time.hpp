/// @file time.hpp
/// @brief `Duration` and `Timestamp`: nanosecond time with only meaningful arithmetic.
///
/// Allowed: Timestamp ± Duration → Timestamp, Timestamp − Timestamp → Duration,
/// Duration ± Duration → Duration, Duration × integer. Not allowed (does not compile):
/// Timestamp + Timestamp, Timestamp × anything, implicit conversion from/to integers.
/// Both types are a single int64_t (trivially copyable), so they can be stored in shared memory.
///
/// Precondition for all arithmetic: the result fits in int64 nanoseconds (about ±292 years).
/// Overflow is undefined behaviour (UBSan reports it); it is not checked on the hot path.
#pragma once

#include <compare>
#include <cstdint>

namespace fastbus::core {

/// A signed span of time in nanoseconds.
class Duration {
public:
    constexpr Duration() noexcept = default;

    [[nodiscard]] static constexpr Duration nanoseconds(std::int64_t n) noexcept {
        return Duration{n};
    }
    [[nodiscard]] static constexpr Duration microseconds(std::int64_t n) noexcept {
        return Duration{n * 1'000};
    }
    [[nodiscard]] static constexpr Duration milliseconds(std::int64_t n) noexcept {
        return Duration{n * 1'000'000};
    }
    [[nodiscard]] static constexpr Duration seconds(std::int64_t n) noexcept {
        return Duration{n * 1'000'000'000};
    }

    [[nodiscard]] constexpr std::int64_t count_ns() const noexcept { return ns_; }

    friend constexpr Duration operator+(Duration a, Duration b) noexcept {
        return Duration{a.ns_ + b.ns_};
    }
    friend constexpr Duration operator-(Duration a, Duration b) noexcept {
        return Duration{a.ns_ - b.ns_};
    }
    friend constexpr Duration operator-(Duration a) noexcept { return Duration{-a.ns_}; }
    friend constexpr Duration operator*(Duration a, std::int64_t k) noexcept {
        return Duration{a.ns_ * k};
    }
    friend constexpr Duration operator*(std::int64_t k, Duration a) noexcept { return a * k; }

    friend constexpr bool operator==(Duration, Duration) noexcept = default;
    friend constexpr auto operator<=>(Duration, Duration) noexcept = default;

private:
    constexpr explicit Duration(std::int64_t ns) noexcept : ns_{ns} {}
    std::int64_t ns_{0};
};

/// A point in time: nanoseconds since the epoch of the clock that produced it (ADR-007).
/// Timestamps from different clocks must not be compared; the type system does not track that.
class Timestamp {
public:
    constexpr Timestamp() noexcept = default;

    [[nodiscard]] static constexpr Timestamp from_ns(std::int64_t ns_since_epoch) noexcept {
        return Timestamp{ns_since_epoch};
    }
    [[nodiscard]] constexpr std::int64_t ns_since_epoch() const noexcept { return ns_; }

    friend constexpr Timestamp operator+(Timestamp t, Duration d) noexcept {
        return Timestamp{t.ns_ + d.count_ns()};
    }
    friend constexpr Timestamp operator+(Duration d, Timestamp t) noexcept { return t + d; }
    friend constexpr Timestamp operator-(Timestamp t, Duration d) noexcept {
        return Timestamp{t.ns_ - d.count_ns()};
    }
    friend constexpr Duration operator-(Timestamp a, Timestamp b) noexcept {
        return Duration::nanoseconds(a.ns_ - b.ns_);
    }

    friend constexpr bool operator==(Timestamp, Timestamp) noexcept = default;
    friend constexpr auto operator<=>(Timestamp, Timestamp) noexcept = default;

private:
    constexpr explicit Timestamp(std::int64_t ns) noexcept : ns_{ns} {}
    std::int64_t ns_{0};
};

}  // namespace fastbus::core
