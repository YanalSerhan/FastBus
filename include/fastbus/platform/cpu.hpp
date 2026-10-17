/// @file cpu.hpp
/// @brief CPU hints for busy-wait loops.
#pragma once

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86) || defined(_M_ARM64))
#include <intrin.h>
#endif

namespace fastbus::platform {

/// Tells the CPU we are spinning: x86 `pause`, ARM `yield`. Reduces power and the penalty when
/// the awaited store arrives, and lets a hyper-threaded sibling run. A no-op elsewhere.
/// Thread-safety: trivially safe. Never blocks, never makes a syscall.
inline void cpu_relax() noexcept {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    _mm_pause();
#elif defined(_MSC_VER) && defined(_M_ARM64)
    __yield();
#elif defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
    __asm__ __volatile__("yield" ::: "memory");
#endif
}

}  // namespace fastbus::platform
