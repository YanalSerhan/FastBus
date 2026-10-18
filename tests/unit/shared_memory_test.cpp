// platform::SharedMemory tests (M2.1). Behaviour that differs between Windows and POSIX is
// asserted per OS on purpose: the difference is documented (PRD_shm_transport.md 3.1), and the
// tests pin it down so it cannot change silently.
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "fastbus/core/error.hpp"
#include "fastbus/platform/process.hpp"
#include "fastbus/platform/shared_memory.hpp"

#include "ipc_test_support.hpp"

namespace {

using fastbus::core::ErrorCode;
using fastbus::platform::is_valid_shm_name;
using fastbus::platform::kMaxShmNameLength;
using fastbus::platform::page_size;
using fastbus::platform::Process;
using fastbus::platform::SharedMemory;
using fastbus::test::error_of;
using fastbus::test::kTestChild;
using fastbus::test::unique_shm_name;

constexpr bool kWindows =
#if defined(_WIN32)
    true;
#else
    false;
#endif

SharedMemory create_or_die(const std::string& name, std::size_t size) {
    auto shm = SharedMemory::create(name, size);
    EXPECT_TRUE(shm.has_value()) << "create(" << name << ") failed";
    return std::move(shm).value();
}

TEST(ShmName, AcceptsOnlyThePortableCharacterSet) {
    EXPECT_TRUE(is_valid_shm_name("fastbus.demo.topic-1_A"));
    EXPECT_TRUE(is_valid_shm_name(std::string(kMaxShmNameLength, 'x')));
    EXPECT_FALSE(is_valid_shm_name(""));
    EXPECT_FALSE(is_valid_shm_name(std::string(kMaxShmNameLength + 1, 'x')));
    for (const std::string_view bad : {"a/b", "a\\b", "with space", "caf\xC3\xA9", "a:b", "/x"}) {
        EXPECT_FALSE(is_valid_shm_name(bad)) << bad;
    }
}

TEST(SharedMemory, RejectsInvalidNamesAndZeroSize) {
    EXPECT_EQ(error_of(SharedMemory::create("a/b", 64)), ErrorCode::InvalidArgument);
    EXPECT_EQ(error_of(SharedMemory::create(unique_shm_name("zero"), 0)),
              ErrorCode::InvalidArgument);
    EXPECT_EQ(error_of(SharedMemory::open("bad name")), ErrorCode::InvalidArgument);
    EXPECT_EQ(error_of(SharedMemory::remove("")), ErrorCode::InvalidArgument);
}

TEST(SharedMemory, CreateRoundsUpToPagesZeroFillsAndIsAligned) {
    const auto shm = create_or_die(unique_shm_name("round"), 100);
    EXPECT_TRUE(shm.is_creator());
    EXPECT_EQ(shm.size(), page_size());
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(shm.data()) % page_size(), 0U);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(shm.data()) % 64U, 0U);  // ADR-009 alignment
    for (const std::byte b : shm.bytes()) {
        ASSERT_EQ(b, std::byte{0});
    }
}

TEST(SharedMemory, TwoMappingsInOneProcessSeeEachOthersWrites) {
    const std::string name = unique_shm_name("two");
    const auto creator = create_or_die(name, 3 * page_size());
    auto opened = SharedMemory::open(name);
    ASSERT_TRUE(opened.has_value());
    EXPECT_FALSE(opened->is_creator());
    EXPECT_EQ(opened->size(), creator.size());
    EXPECT_NE(opened->data(), creator.data());  // two views of the same memory

    creator.bytes()[10] = std::byte{0x5A};
    EXPECT_EQ(opened->bytes()[10], std::byte{0x5A});
    opened->bytes()[creator.size() - 1] = std::byte{0xC3};
    EXPECT_EQ(creator.bytes()[creator.size() - 1], std::byte{0xC3});
}

TEST(SharedMemory, CreateTwiceFailsWithAlreadyExists) {
    const std::string name = unique_shm_name("twice");
    const auto first = create_or_die(name, 64);
    EXPECT_EQ(error_of(SharedMemory::create(name, 64)), ErrorCode::AlreadyExists);
    EXPECT_EQ(first.bytes()[0], std::byte{0});  // the first segment is untouched
}

TEST(SharedMemory, OpenMissingFailsWithNotFound) {
    EXPECT_EQ(error_of(SharedMemory::open(unique_shm_name("missing"))), ErrorCode::NotFound);
}

TEST(SharedMemory, DestroyingTheOnlyCreatorRemovesTheName) {
    const std::string name = unique_shm_name("gone");
    { const auto shm = create_or_die(name, 64); }
    EXPECT_EQ(error_of(SharedMemory::open(name)), ErrorCode::NotFound);
    // ...and the name can be created again.
    EXPECT_TRUE(SharedMemory::create(name, 64).has_value());
}

TEST(SharedMemory, OpenerKeepsItsMappingAfterTheCreatorIsDestroyed) {
    const std::string name = unique_shm_name("keep");
    auto creator = create_or_die(name, 64);
    auto opened = SharedMemory::open(name);
    ASSERT_TRUE(opened.has_value());
    creator.bytes()[1] = std::byte{0x77};
    { const SharedMemory gone = std::move(creator); }
    EXPECT_EQ(opened->bytes()[1], std::byte{0x77});  // still mapped, still the same bytes

    // The platform gap (PRD 3.1): POSIX unlinked the name; Windows keeps it while a handle lives.
    const auto again = SharedMemory::open(name);
    if (kWindows) {
        EXPECT_TRUE(again.has_value());
    } else {
        EXPECT_EQ(error_of(again), ErrorCode::NotFound);
    }
}

TEST(SharedMemory, RemoveDeletesAStaleName) {
    const std::string name = unique_shm_name("stale");
    const auto shm = create_or_die(name, 64);
    const auto removed = SharedMemory::remove(name);
    if (kWindows) {
        EXPECT_EQ(error_of(removed), ErrorCode::NotSupported);
        return;
    }
    ASSERT_TRUE(removed.has_value());
    EXPECT_EQ(error_of(SharedMemory::open(name)), ErrorCode::NotFound);
    EXPECT_TRUE(SharedMemory::create(name, 64).has_value());  // reusable at once
    EXPECT_EQ(error_of(SharedMemory::remove(unique_shm_name("never"))), ErrorCode::NotFound);
}

TEST(SharedMemory, MoveTransfersOwnership) {
    const std::string name = unique_shm_name("move");
    auto a = create_or_die(name, 64);
    std::byte* data = a.data();
    const SharedMemory b = std::move(a);
    EXPECT_EQ(a.data(), nullptr);  // NOLINT(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    EXPECT_EQ(b.data(), data);
    EXPECT_TRUE(b.is_creator());
    EXPECT_EQ(b.name(), name);
    EXPECT_TRUE(SharedMemory::open(name).has_value());  // moved-from did not remove the name
}

TEST(SharedMemory, HugeSizeFailsCleanlyAndLeavesNothingBehind) {
    const std::string name = unique_shm_name("huge");
    const auto shm = SharedMemory::create(name, std::size_t{1} << 46U);  // 64 TiB
    ASSERT_FALSE(shm.has_value());
    EXPECT_TRUE(shm.error().code == ErrorCode::OutOfMemory ||
                shm.error().code == ErrorCode::InvalidArgument)
        << fastbus::core::to_string(shm.error().code);
    EXPECT_EQ(error_of(SharedMemory::open(name)), ErrorCode::NotFound);
}

TEST(SharedMemory, MoveAssignmentReleasesTheOldSegment) {
    const std::string old_name = unique_shm_name("old");
    const std::string new_name = unique_shm_name("new");
    auto target = create_or_die(old_name, 64);
    target = create_or_die(new_name, 64);
    EXPECT_EQ(target.name(), new_name);
    EXPECT_EQ(error_of(SharedMemory::open(old_name)), ErrorCode::NotFound);  // old one released
    EXPECT_TRUE(SharedMemory::open(new_name).has_value());
}

// ---- Across processes ---------------------------------------------------------------------
TEST(SharedMemory, ChildProcessSeesAndModifiesTheSameBytes) {
    const std::string name = unique_shm_name("child");
    const auto shm = create_or_die(name, 4096);
    shm.bytes()[7] = std::byte{0x42};

    const std::array check{std::string_view{"shm-check"}, std::string_view{name},
                           std::string_view{"7"}, std::string_view{"66"}};
    auto checker = Process::spawn(kTestChild, check);
    ASSERT_TRUE(checker.has_value());
    EXPECT_EQ(checker->wait(std::chrono::seconds(20)).value(), 0) << "child saw a different byte";

    const std::array write{std::string_view{"shm-write"}, std::string_view{name},
                           std::string_view{"4000"}, std::string_view{"171"}};
    auto writer = Process::spawn(kTestChild, write);
    ASSERT_TRUE(writer.has_value());
    ASSERT_EQ(writer->wait(std::chrono::seconds(20)).value(), 0);
    EXPECT_EQ(shm.bytes()[4000], std::byte{171});
}

}  // namespace
