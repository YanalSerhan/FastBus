// CRC32C tests (M2.4, ADR-005): standard and RFC 3720 (iSCSI) vectors, chaining, and
// hardware == software on random inputs, lengths and alignments.
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <span>
#include <string_view>
#include <vector>

#include "fastbus/platform/crc32c.hpp"

namespace {

using fastbus::platform::crc32c;
using fastbus::platform::crc32c_is_hardware;
using fastbus::platform::crc32c_software;

std::span<const std::byte> bytes_of(std::string_view s) {
    return std::as_bytes(std::span{s});
}

template <std::size_t N>
std::array<std::byte, N> filled(std::uint8_t value) {
    std::array<std::byte, N> a{};
    a.fill(std::byte{value});
    return a;
}

TEST(Crc32c, ReportsWhichImplementationIsUsed) {
    RecordProperty("hardware", crc32c_is_hardware() ? "yes" : "no");
    SUCCEED();
}

TEST(Crc32c, StandardCheckValue) {
    EXPECT_EQ(crc32c(bytes_of("123456789")), 0xE306'9283U);
    EXPECT_EQ(crc32c_software(bytes_of("123456789")), 0xE306'9283U);
}

TEST(Crc32c, EmptyInputIsZeroAndKeepsTheRunningValue) {
    EXPECT_EQ(crc32c({}), 0U);
    EXPECT_EQ(crc32c({}, 0x1234'5678U), 0x1234'5678U);
}

TEST(Crc32c, Rfc3720IscsiVectors) {
    EXPECT_EQ(crc32c(filled<32>(0x00)), 0x8A91'36AAU);
    EXPECT_EQ(crc32c(filled<32>(0xFF)), 0x62A8'AB43U);
    std::array<std::byte, 32> ascending{};
    std::array<std::byte, 32> descending{};
    for (std::size_t i = 0; i < 32; ++i) {
        ascending.at(i) = static_cast<std::byte>(i);
        descending.at(i) = static_cast<std::byte>(31 - i);
    }
    EXPECT_EQ(crc32c(ascending), 0x46DD'794EU);
    EXPECT_EQ(crc32c(descending), 0x113F'DB5CU);
}

TEST(Crc32c, ChainingEqualsOneShot) {
    const std::string_view text = "The quick brown fox jumps over the lazy dog, twice over.";
    const auto all = bytes_of(text);
    for (std::size_t split = 0; split <= all.size(); ++split) {
        const std::uint32_t first = crc32c(all.first(split));
        EXPECT_EQ(crc32c(all.subspan(split), first), crc32c(all)) << "split at " << split;
    }
}

TEST(Crc32c, HardwareAndSoftwareAgreeOnRandomInputs) {
    // Fixed seed on purpose: a failure must be reproducible.
    std::mt19937_64 rng{20261018};  // NOLINT(cert-msc32-c,cert-msc51-cpp)
    std::vector<std::byte> buffer(4096 + 16);
    for (std::byte& b : buffer) {
        b = static_cast<std::byte>(rng());
    }
    const std::span<const std::byte> all{buffer};
    for (int trial = 0; trial < 2000; ++trial) {
        const std::size_t offset = rng() % 16;  // unaligned starts
        const std::size_t length = rng() % 4096;
        const auto part = all.subspan(offset, length);
        const auto seed = static_cast<std::uint32_t>(rng());
        ASSERT_EQ(crc32c(part, seed), crc32c_software(part, seed))
            << "offset " << offset << " length " << length;
    }
}

TEST(Crc32c, DetectsEverySingleBitFlip) {
    std::array<std::byte, 64> message = filled<64>(0x5A);
    const std::uint32_t good = crc32c(message);
    for (std::size_t bit = 0; bit < message.size() * 8; ++bit) {
        message.at(bit / 8) ^= std::byte{static_cast<std::uint8_t>(1U << (bit % 8))};
        EXPECT_NE(crc32c(message), good) << "bit " << bit;
        message.at(bit / 8) ^= std::byte{static_cast<std::uint8_t>(1U << (bit % 8))};
    }
}

}  // namespace
