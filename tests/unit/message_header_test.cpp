// MessageHeader tests (M2.4): layout is checked at compile time in the header itself; these
// tests cover type ids, construction, validation of untrusted headers and checksums.
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string_view>

#include "fastbus/core/error.hpp"
#include "fastbus/core/message_header.hpp"

namespace {

using fastbus::core::compute_checksum;
using fastbus::core::ErrorCode;
using fastbus::core::fnv1a_64;
using fastbus::core::make_header;
using fastbus::core::MessageHeader;
using fastbus::core::seal;
using fastbus::core::validate;
using fastbus::core::verify;
namespace flags = fastbus::core::message_flags;

template <typename R>
ErrorCode error_of(const R& r) {
    return r.has_value() ? ErrorCode{0} : r.error().code;
}

// Published FNV-1a 64 test vectors.
static_assert(fnv1a_64("") == 0xCBF2'9CE4'8422'2325ULL);
static_assert(fnv1a_64("a") == 0xAF63'DC4C'8601'EC8CULL);
static_assert(fnv1a_64("foobar") == 0x8594'4171'F739'67E8ULL);
// Usable as a compile-time type id.
constexpr std::uint64_t kPoseId = fnv1a_64("fastbus.demo.Pose");
static_assert(kPoseId != fnv1a_64("fastbus.demo.Twist"));

constexpr std::array<std::byte, 16> kPayload = [] {
    std::array<std::byte, 16> p{};
    for (std::size_t i = 0; i < p.size(); ++i) {
        p.at(i) = static_cast<std::byte>(0xA0 + i);
    }
    return p;
}();

MessageHeader sample_header() {
    return make_header(kPoseId, 3, 42, 1'700'000'000'000'000'000, kPayload.size());
}

TEST(MessageHeader, MakeHeaderFillsEveryFieldAndZeroesReserved) {
    // Start from garbage so leftover bytes would be noticed.
    MessageHeader h{};
    std::memset(&h, 0xEE, sizeof(h));
    h = sample_header();
    EXPECT_EQ(h.magic, MessageHeader::kMagic);
    EXPECT_EQ(h.header_version, MessageHeader::kVersion);
    EXPECT_EQ(h.schema_version, 3U);
    EXPECT_EQ(h.type_id, kPoseId);
    EXPECT_EQ(h.sequence, 42U);
    EXPECT_EQ(h.timestamp_ns, 1'700'000'000'000'000'000);
    EXPECT_EQ(h.payload_size, 16U);
    EXPECT_EQ(h.checksum, 0U);
    EXPECT_EQ(h.flags, 0U);
    for (const std::uint8_t b : h.reserved) {
        EXPECT_EQ(b, 0U);
    }
}

TEST(MessageHeader, MagicSpellsFbmsInMemory) {
    const MessageHeader h = sample_header();
    std::array<char, 4> bytes{};
    std::memcpy(bytes.data(), &h, bytes.size());
    EXPECT_EQ(std::string_view(bytes.data(), bytes.size()), "FBMS");
}

TEST(MessageHeader, MakeHeaderNeverClaimsAChecksum) {
    const MessageHeader h = make_header(1, 1, 1, 1, 0, flags::kReplayed | flags::kChecksummed);
    EXPECT_EQ(h.flags, flags::kReplayed);
}

TEST(MessageHeader, ValidateAcceptsAGoodHeader) {
    EXPECT_TRUE(validate(sample_header(), sizeof(MessageHeader) + kPayload.size()).has_value());
}

TEST(MessageHeader, ValidateRejectsCorruptedHeaders) {
    const std::size_t room = sizeof(MessageHeader) + kPayload.size();
    MessageHeader h = sample_header();
    h.magic ^= 1U;
    EXPECT_EQ(error_of(validate(h, room)), ErrorCode::Corrupted);

    h = sample_header();
    h.header_version = 2;
    EXPECT_EQ(error_of(validate(h, room)), ErrorCode::VersionMismatch);

    h = sample_header();
    h.flags = 0x8000;
    EXPECT_EQ(error_of(validate(h, room)), ErrorCode::Corrupted);

    h = sample_header();
    h.payload_size = std::numeric_limits<std::uint32_t>::max();  // would read far past the end
    EXPECT_EQ(error_of(validate(h, room)), ErrorCode::Corrupted);
    EXPECT_EQ(error_of(validate(sample_header(), room - 1)), ErrorCode::Corrupted);
    EXPECT_EQ(error_of(validate(sample_header(), 10)), ErrorCode::Corrupted);  // < header size
}

TEST(MessageHeader, SealedMessageVerifiesAndAnyChangeIsDetected) {
    MessageHeader h = sample_header();
    seal(h, kPayload);
    EXPECT_NE(h.flags & flags::kChecksummed, 0U);
    EXPECT_TRUE(verify(h, kPayload));
    EXPECT_EQ(compute_checksum(h, kPayload), h.checksum);

    std::array<std::byte, 16> damaged = kPayload;
    damaged.at(7) ^= std::byte{0x01};
    EXPECT_FALSE(verify(h, damaged));

    MessageHeader other = h;
    other.sequence += 1;  // header fields are covered too
    EXPECT_FALSE(verify(other, kPayload));
}

TEST(MessageHeader, UnchecksummedMessageAlwaysVerifies) {
    const MessageHeader h = sample_header();
    std::array<std::byte, 16> anything{};
    EXPECT_TRUE(verify(h, anything));
}

}  // namespace
