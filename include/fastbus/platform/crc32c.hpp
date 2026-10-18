/// @file crc32c.hpp
/// @brief CRC32C (Castagnoli, polynomial 0x1EDC6F41, reflected 0x82F63B78) — ADR-005.
///
/// `crc32c()` picks the fastest implementation once, at first use:
///   x86-64  SSE4.2 `crc32` instruction (CPUID check at runtime)
///   ARM64   CRC32C instructions when the compiler targets them (`__ARM_FEATURE_CRC32`)
///   other   software slice-by-8 tables
/// All paths return identical results (tested). Standard check value: crc32c("123456789") ==
/// 0xE3069283.
///
/// Chaining: `crc32c(b, crc32c(a)) == crc32c(a + b)`, so a message can be checksummed in parts
/// (header, then payload) without copying.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace fastbus::platform {

/// CRC32C of `data`, continuing from `crc` (0 to start).
[[nodiscard]] std::uint32_t crc32c(std::span<const std::byte> data, std::uint32_t crc = 0) noexcept;

/// The portable implementation, always available (reference for tests and benchmarks).
[[nodiscard]] std::uint32_t crc32c_software(std::span<const std::byte> data,
                                            std::uint32_t crc = 0) noexcept;

/// True if crc32c() uses a hardware instruction on this machine.
[[nodiscard]] bool crc32c_is_hardware() noexcept;

}  // namespace fastbus::platform
