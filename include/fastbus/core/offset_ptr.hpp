/// @file offset_ptr.hpp
/// @brief In-segment references as offsets (ADR-004 rule 2). A raw pointer stored in shared
/// memory is only valid in the process that wrote it (each process maps the segment at its own
/// address); an offset from the segment base is valid in all of them.
///
/// Offsets come from untrusted memory, so they are turned into pointers only by `resolve()`,
/// which checks bounds, overflow and alignment and returns nullptr / an empty span on any
/// violation.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace fastbus::core {

/// Types that may live in shared memory (same rule as RingElement).
template <typename T>
concept ShmPlaceable = std::is_trivially_copyable_v<std::remove_const_t<T>> &&
                       std::is_standard_layout_v<std::remove_const_t<T>>;

template <ShmPlaceable T>
struct OffsetPtr {
    std::uint64_t offset;  ///< bytes from the segment base; 0 means null

    [[nodiscard]] constexpr bool is_null() const noexcept { return offset == 0; }
    friend constexpr bool operator==(OffsetPtr, OffsetPtr) noexcept = default;
};
static_assert(sizeof(OffsetPtr<int>) == 8 && std::is_trivially_copyable_v<OffsetPtr<int>>);

/// The object at `p` inside `base`, or nullptr if `p` is null, out of bounds or misaligned.
template <ShmPlaceable T>
[[nodiscard]] T* resolve(OffsetPtr<T> p, std::span<std::byte> base) noexcept {
    if (p.is_null() || p.offset > base.size() || base.size() - p.offset < sizeof(T)) {
        return nullptr;
    }
    std::byte* at = base.subspan(static_cast<std::size_t>(p.offset)).data();
    if (reinterpret_cast<std::uintptr_t>(at) % alignof(T) != 0) {
        return nullptr;
    }
    return reinterpret_cast<T*>(at);
}

/// `count` consecutive objects starting at `p`, or an empty span on any violation (including
/// `count * sizeof(T)` overflowing).
template <ShmPlaceable T>
[[nodiscard]] std::span<T> resolve_array(OffsetPtr<T> p, std::uint64_t count,
                                         std::span<std::byte> base) noexcept {
    if (p.is_null() || count == 0 || p.offset > base.size()) {
        return {};
    }
    const std::uint64_t room = base.size() - p.offset;
    if (count > room / sizeof(T)) {  // also rules out count * sizeof(T) overflow
        return {};
    }
    T* first = resolve(p, base);
    return first == nullptr ? std::span<T>{} : std::span<T>{first, static_cast<std::size_t>(count)};
}

/// The offset of `object` inside `base` (precondition: it lies inside).
template <ShmPlaceable T>
[[nodiscard]] OffsetPtr<T> offset_of(const T* object, std::span<const std::byte> base) noexcept {
    const auto at = reinterpret_cast<std::uintptr_t>(object);
    const auto start = reinterpret_cast<std::uintptr_t>(base.data());
    return OffsetPtr<T>{.offset = static_cast<std::uint64_t>(at - start)};
}

}  // namespace fastbus::core
