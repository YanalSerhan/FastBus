// CRC32C implementations and runtime dispatch (ADR-005). CPU-specific code is selected by
// architecture macros here, in the platform layer, and nowhere else.
#include "fastbus/platform/crc32c.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

#if defined(__x86_64__) || defined(_M_X64)
#define FASTBUS_CRC32C_X86 1  // NOLINT(cppcoreguidelines-macro-usage): architecture switch
#include <nmmintrin.h>
#if defined(_WIN32)
#include <intrin.h>  // __cpuid
#else
#include <cpuid.h>  // __get_cpuid
#endif
#if defined(__GNUC__) || defined(__clang__)
// GCC, Clang and clang-cl need the SSE4.2 target on the function that uses the intrinsics (the
// rest of the build targets baseline x86-64); MSVC accepts the intrinsics without it.
#define FASTBUS_TARGET_SSE42 \
    __attribute__((target("sse4.2")))  // NOLINT(cppcoreguidelines-macro-usage)
#else
#define FASTBUS_TARGET_SSE42  // NOLINT(cppcoreguidelines-macro-usage)
#endif
#elif defined(__aarch64__) && defined(__ARM_FEATURE_CRC32)
#define FASTBUS_CRC32C_ARM 1  // NOLINT(cppcoreguidelines-macro-usage): architecture switch
#include <arm_acle.h>
#endif

namespace fastbus::platform {
namespace {

static_assert(std::endian::native == std::endian::little, "ADR-004: little-endian only");

constexpr std::uint32_t kPolynomial = 0x82F6'3B78U;  // reflected Castagnoli polynomial
using Table = std::array<std::array<std::uint32_t, 256>, 8>;

// Table lookups index with runtime values that are masked to 0..255 (or loop bounds) by
// construction; bounds-checked .at() would cost a branch per byte on the hottest loop.
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index)
constexpr Table make_tables() {
    Table t{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t c = i;
        for (int bit = 0; bit < 8; ++bit) {
            c = (c & 1U) != 0 ? (c >> 1U) ^ kPolynomial : c >> 1U;
        }
        t[0][i] = c;
    }
    for (std::size_t k = 1; k < 8; ++k) {
        for (std::size_t i = 0; i < 256; ++i) {
            t[k][i] = (t[k - 1][i] >> 8U) ^ t[0][t[k - 1][i] & 0xFFU];
        }
    }
    return t;
}
// NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
constexpr Table kTables = make_tables();

std::uint64_t load64(std::span<const std::byte> bytes) noexcept {
    std::uint64_t v = 0;
    std::memcpy(&v, bytes.data(), sizeof(v));
    return v;
}

#if defined(FASTBUS_CRC32C_X86)
bool cpu_has_sse42() noexcept {
    constexpr unsigned kSse42Bit = 1U << 20U;  // CPUID leaf 1, ECX bit 20
#if defined(_WIN32)
    std::array<int, 4> regs{};
    __cpuid(regs.data(), 1);
    return (static_cast<unsigned>(regs[2]) & kSse42Bit) != 0;
#else
    unsigned eax = 0;
    unsigned ebx = 0;
    unsigned ecx = 0;
    unsigned edx = 0;
    return __get_cpuid(1, &eax, &ebx, &ecx, &edx) != 0 && (ecx & kSse42Bit) != 0;
#endif
}

FASTBUS_TARGET_SSE42 std::uint32_t crc32c_hw(std::span<const std::byte> data,
                                             std::uint32_t crc) noexcept {
    std::uint64_t c = ~crc;
    while (data.size() >= 8) {
        c = _mm_crc32_u64(c, load64(data));
        data = data.subspan(8);
    }
    auto c32 = static_cast<std::uint32_t>(c);
    for (const std::byte b : data) {
        c32 = _mm_crc32_u8(c32, static_cast<std::uint8_t>(b));
    }
    return ~c32;
}
#elif defined(FASTBUS_CRC32C_ARM)
std::uint32_t crc32c_hw(std::span<const std::byte> data, std::uint32_t crc) noexcept {
    std::uint32_t c = ~crc;
    while (data.size() >= 8) {
        c = __crc32cd(c, load64(data));
        data = data.subspan(8);
    }
    for (const std::byte b : data) {
        c = __crc32cb(c, static_cast<std::uint8_t>(b));
    }
    return ~c;
}
#endif

using Impl = std::uint32_t (*)(std::span<const std::byte>, std::uint32_t) noexcept;

Impl select_impl() noexcept {
#if defined(FASTBUS_CRC32C_X86)
    return cpu_has_sse42() ? &crc32c_hw : &crc32c_software;
#elif defined(FASTBUS_CRC32C_ARM)
    return &crc32c_hw;
#else
    return &crc32c_software;
#endif
}

}  // namespace

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index): masked indices, see above
std::uint32_t crc32c_software(std::span<const std::byte> data, std::uint32_t crc) noexcept {
    std::uint32_t c = ~crc;
    while (data.size() >= 8) {  // slice-by-8: one table lookup per input byte, 8 in parallel
        const std::uint64_t v = load64(data) ^ c;
        c = kTables[7][v & 0xFFU] ^ kTables[6][(v >> 8U) & 0xFFU] ^ kTables[5][(v >> 16U) & 0xFFU] ^
            kTables[4][(v >> 24U) & 0xFFU] ^ kTables[3][(v >> 32U) & 0xFFU] ^
            kTables[2][(v >> 40U) & 0xFFU] ^ kTables[1][(v >> 48U) & 0xFFU] ^ kTables[0][v >> 56U];
        data = data.subspan(8);
    }
    for (const std::byte b : data) {
        c = kTables[0][(c ^ static_cast<std::uint8_t>(b)) & 0xFFU] ^ (c >> 8U);
    }
    return ~c;
}
// NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)

std::uint32_t crc32c(std::span<const std::byte> data, std::uint32_t crc) noexcept {
    static const Impl kImpl = select_impl();
    return kImpl(data, crc);
}

bool crc32c_is_hardware() noexcept {
    static const bool kHardware = select_impl() != &crc32c_software;
    return kHardware;
}

}  // namespace fastbus::platform
