// transport::Segment tests (M2.3): lifecycle, waiting for ready, closing, stale creators (real
// child processes that get killed), creator death during initialization, and header validation
// against corrupted bytes.
#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "fastbus/core/error.hpp"
#include "fastbus/core/time.hpp"
#include "fastbus/platform/process.hpp"
#include "fastbus/platform/shared_memory.hpp"
#include "fastbus/transport/segment.hpp"

#include "ipc_test_support.hpp"

namespace {

using namespace std::chrono_literals;
using fastbus::core::Duration;
using fastbus::core::ErrorCode;
using fastbus::platform::current_process;
using fastbus::platform::Process;
using fastbus::platform::SharedMemory;
using fastbus::test::error_of;
using fastbus::test::eventually;
using fastbus::test::kTestChild;
using fastbus::test::unique_shm_name;
using fastbus::transport::Segment;
using fastbus::transport::SegmentHeader;
using fastbus::transport::SegmentState;
using fastbus::transport::validate_segment;
namespace segment_kind = fastbus::transport::segment_kind;

constexpr Duration kShort = Duration::milliseconds(50);
constexpr Duration kLong = Duration::seconds(10);

Segment create_ready(const std::string& name, std::size_t payload) {
    auto seg = Segment::create(name, payload, segment_kind::kTest);
    EXPECT_TRUE(seg.has_value()) << "create(" << name << ") failed";
    seg->mark_ready();
    return std::move(seg).value();
}

Process spawn_child(std::initializer_list<std::string_view> args) {
    const std::vector<std::string_view> list{args};
    auto child = Process::spawn(kTestChild, list);
    EXPECT_TRUE(child.has_value());
    return std::move(child).value();
}

TEST(Segment, CreateThenOpenSeesTheHeaderAndPayload) {
    const std::string name = unique_shm_name("seg");
    auto creator = Segment::create(name, 1000, segment_kind::kTest);
    ASSERT_TRUE(creator.has_value());
    EXPECT_EQ(creator->state(), SegmentState::Initializing);
    EXPECT_EQ(creator->payload().size(), 1000U);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(creator->payload().data()) % 64U, 0U);
    creator->payload()[999] = std::byte{0x3C};
    creator->mark_ready();

    auto opener = Segment::open(name, kLong);
    ASSERT_TRUE(opener.has_value());
    EXPECT_FALSE(opener->is_creator());
    const SegmentHeader& h = opener->header();
    EXPECT_EQ(h.magic, SegmentHeader::kMagic);
    EXPECT_EQ(h.kind, segment_kind::kTest);
    EXPECT_EQ(h.payload_size, 1000U);
    EXPECT_EQ(h.creator, current_process());
    EXPECT_NE(h.epoch, 0U);
    EXPECT_EQ(opener->payload()[999], std::byte{0x3C});
}

TEST(Segment, OpenWaitsForReadyAndTimesOutWithoutIt) {
    const std::string name = unique_shm_name("wait");
    auto creator = Segment::create(name, 64, segment_kind::kTest);
    ASSERT_TRUE(creator.has_value());
    EXPECT_EQ(error_of(Segment::open(name, kShort)), ErrorCode::Timeout);

    std::thread late([&] {
        std::this_thread::sleep_for(50ms);  // NOLINT: test pacing only
        creator->mark_ready();
    });
    const auto opened = Segment::open(name, kLong);
    late.join();
    EXPECT_TRUE(opened.has_value());
}

TEST(Segment, ClosingIsVisibleToOpenersAndBlocksNewOpens) {
    const std::string name = unique_shm_name("close");
    auto creator = create_ready(name, 64);
    auto opener = Segment::open(name, kLong);
    ASSERT_TRUE(opener.has_value());
    { const Segment gone = std::move(creator); }
    EXPECT_EQ(opener->state(), SegmentState::Closing);
    // Same answer on both OSes (PRD 3.1): POSIX has no name any more, Windows sees `closing`.
    EXPECT_EQ(error_of(Segment::open(name, kShort)), ErrorCode::NotFound);
}

TEST(Segment, CreateFailsWhileTheCreatorIsAlive) {
    const std::string name = unique_shm_name("alive");
    const auto first = create_ready(name, 64);
    EXPECT_EQ(error_of(Segment::create(name, 64, segment_kind::kTest)), ErrorCode::AlreadyExists);
}

TEST(Segment, RejectsBadArguments) {
    EXPECT_EQ(error_of(Segment::create(unique_shm_name("zero"), 0, segment_kind::kTest)),
              ErrorCode::InvalidArgument);
    EXPECT_EQ(error_of(Segment::create("bad/name", 64, segment_kind::kTest)),
              ErrorCode::InvalidArgument);
    EXPECT_EQ(error_of(Segment::open(unique_shm_name("none"), kShort)), ErrorCode::NotFound);
}

TEST(Segment, StaleSegmentOfAKilledCreatorIsDetectedAndRecreated) {
    const std::string name = unique_shm_name("stale");
    auto child = spawn_child({"segment-create", name, "4096"});
    ASSERT_TRUE(eventually([&] { return Segment::open(name, kShort).has_value(); }))
        << "the child never made its segment ready";
    ASSERT_TRUE(child.kill().has_value());  // crash: no destructor runs in the child

    // POSIX: the name is still there, but its creator is dead.
    EXPECT_EQ(error_of(Segment::open(name, kShort)), ErrorCode::NotFound);
    auto mine = Segment::create(name, 4096, segment_kind::kTest);  // reclaims the stale name
    ASSERT_TRUE(mine.has_value()) << fastbus::core::to_string(mine.error().code);
    EXPECT_EQ(mine->header().creator, current_process());
}

TEST(Segment, CreatorDyingDuringInitializationIsDetected) {
    const std::string name = unique_shm_name("stall");
    auto child = spawn_child({"segment-stall", name, "4096"});
    ASSERT_TRUE(eventually([&] { return SharedMemory::open(name).has_value(); }));
    EXPECT_EQ(error_of(Segment::open(name, kShort)), ErrorCode::Timeout);  // alive, not ready
    ASSERT_TRUE(child.kill().has_value());
    EXPECT_EQ(error_of(Segment::open(name, kLong)), ErrorCode::NotFound);  // no 10 s wait
    EXPECT_TRUE(Segment::create(name, 64, segment_kind::kTest).has_value());
}

TEST(Segment, NameHoldingSomethingElseIsReportedNotOverwritten) {
    const std::string name = unique_shm_name("foreign");
    const auto foreign = SharedMemory::create(name, 4096);
    ASSERT_TRUE(foreign.has_value());
    std::memset(foreign->data(), 0xAB, foreign->size());
    EXPECT_EQ(error_of(Segment::create(name, 64, segment_kind::kTest)), ErrorCode::Corrupted);
    EXPECT_EQ(error_of(Segment::open(name, kShort)), ErrorCode::Corrupted);  // bad state value
    EXPECT_EQ(foreign->bytes()[0], std::byte{0xAB});                         // untouched
}

// ---- Validation of untrusted header bytes ---------------------------------------------------
class SegmentValidation : public ::testing::Test {
protected:
    static constexpr std::size_t kPayload = 256;
    static constexpr std::size_t kSize = sizeof(SegmentHeader) + kPayload;

    void SetUp() override {
        const Segment seg = create_ready(unique_shm_name("valid"), kPayload);
        std::memcpy(bytes_.data(), &seg.header(), kSize);  // a real, valid header + payload
    }
    [[nodiscard]] std::span<const std::byte> mapping() const { return {bytes_.data(), kSize}; }
    template <typename T>
    void put(std::size_t offset, T value) {
        std::memcpy(bytes_.data() + offset, &value, sizeof(T));
    }

    [[nodiscard]] std::array<std::byte, kSize>& bytes() { return bytes_; }

private:
    alignas(64) std::array<std::byte, kSize> bytes_{};
};

TEST_F(SegmentValidation, AcceptsTheRealHeader) {
    EXPECT_TRUE(validate_segment(mapping()).has_value());
    EXPECT_EQ(error_of(validate_segment(mapping().first(100))), ErrorCode::Corrupted);
}

TEST_F(SegmentValidation, RejectsEachKindOfDamage) {
    const auto expect = [&](auto mutate, ErrorCode code) {
        SetUp();
        mutate();
        EXPECT_EQ(error_of(validate_segment(mapping())), code);
    };
    expect([&] { put<std::uint64_t>(offsetof(SegmentHeader, magic), 0); }, ErrorCode::Corrupted);
    expect([&] { put<std::uint16_t>(offsetof(SegmentHeader, layout_version), 9); },
           ErrorCode::VersionMismatch);
    expect([&] { put<std::uint16_t>(offsetof(SegmentHeader, header_size), 64); },
           ErrorCode::Corrupted);
    expect([&] { put<std::uint64_t>(offsetof(SegmentHeader, total_size), kSize + 1); },
           ErrorCode::Corrupted);  // claims more than is mapped
    expect([&] { put<std::uint64_t>(offsetof(SegmentHeader, payload_offset), 136); },
           ErrorCode::Corrupted);  // misaligned
    expect([&] { put<std::uint64_t>(offsetof(SegmentHeader, payload_offset), 0); },
           ErrorCode::Corrupted);  // overlaps the header
    expect([&] { put<std::uint64_t>(offsetof(SegmentHeader, payload_size), ~std::uint64_t{0}); },
           ErrorCode::Corrupted);  // offset + size would overflow
}

TEST_F(SegmentValidation, EverySingleBitFlipIsHandledWithoutCrashing) {
    // ASan/UBSan prove no out-of-bounds read happens on any of these.
    for (std::size_t bit = 0; bit < sizeof(SegmentHeader) * 8; ++bit) {
        bytes().at(bit / 8) ^= std::byte{static_cast<std::uint8_t>(1U << (bit % 8))};
        const auto result = validate_segment(mapping());
        static_cast<void>(result);
        bytes().at(bit / 8) ^= std::byte{static_cast<std::uint8_t>(1U << (bit % 8))};
    }
    EXPECT_TRUE(validate_segment(mapping()).has_value());
}

}  // namespace
