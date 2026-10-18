// Private to the slab implementation: block-state encoding shared by slab_setup.cpp and
// slab.cpp. State machine: PRD_shm_transport.md section 7.
#pragma once

#include <cstdint>

namespace fastbus::core::slab_detail {

inline constexpr std::uint32_t kFree = 0xFFFF'FFFFU;
inline constexpr std::uint32_t kLoaned = 0xFFFF'FFFEU;
inline constexpr std::uint32_t kReturned = 0;

constexpr std::uint64_t pack(std::uint32_t generation, std::uint32_t low) noexcept {
    return (static_cast<std::uint64_t>(generation) << 32U) | low;
}
constexpr std::uint32_t generation_of(std::uint64_t s) noexcept {
    return static_cast<std::uint32_t>(s >> 32U);
}
constexpr std::uint32_t low_of(std::uint64_t s) noexcept {
    return static_cast<std::uint32_t>(s);
}

}  // namespace fastbus::core::slab_detail
