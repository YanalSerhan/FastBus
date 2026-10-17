#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <type_traits>
#include <unordered_set>

#include "fastbus/core/ids.hpp"
#include "fastbus/core/strong_type.hpp"

namespace {

using fastbus::core::NodeId;
using fastbus::core::SchemaVersion;
using fastbus::core::SequenceNumber;
using fastbus::core::TopicId;
using fastbus::core::TypeId;

// --- Compile-time guarantees: no implicit conversions, distinct tags are distinct types -----
static_assert(!std::is_convertible_v<std::uint32_t, TopicId>, "construction must be explicit");
static_assert(!std::is_convertible_v<TopicId, std::uint32_t>, "no implicit unwrap");
static_assert(!std::is_convertible_v<TopicId, NodeId>, "different tags must not convert");
static_assert(!std::is_same_v<TopicId, NodeId>);
static_assert(std::is_constructible_v<TopicId, std::uint32_t>);

template <typename A, typename B>
concept EqualityComparable = requires(A a, B b) { a == b; };
static_assert(EqualityComparable<TopicId, TopicId>);
static_assert(!EqualityComparable<TopicId, NodeId>, "IDs of different kinds must not compare");

// --- Layout: safe for shared memory / wire --------------------------------------------------
static_assert(std::is_trivially_copyable_v<TopicId>);
static_assert(std::is_standard_layout_v<TopicId>);
static_assert(sizeof(TopicId) == sizeof(std::uint32_t));
static_assert(sizeof(SequenceNumber) == sizeof(std::uint64_t));
static_assert(sizeof(TypeId) == sizeof(std::uint64_t));
static_assert(sizeof(SchemaVersion) == sizeof(std::uint16_t));

// --- constexpr usability ---------------------------------------------------------------------
static_assert(TopicId{7}.value() == 7U);
static_assert(TopicId{1} < TopicId{2});
static_assert(TopicId{} == TopicId{0}, "default construction is value-initialized");

TEST(StrongType, ValueRoundTrips) {
    constexpr std::uint32_t kRaw = 0xDEADBEEFU;
    EXPECT_EQ(TopicId{kRaw}.value(), kRaw);
}

TEST(StrongType, ComparisonsFollowTheUnderlyingValue) {
    const NodeId a{3};
    const NodeId b{5};
    EXPECT_TRUE(a == NodeId{3});
    EXPECT_TRUE(a != b);
    EXPECT_TRUE(a < b);
    EXPECT_TRUE(b >= a);
}

TEST(StrongType, UsableAsUnorderedSetKey) {
    std::unordered_set<TopicId> topics;
    topics.insert(TopicId{1});
    topics.insert(TopicId{2});
    topics.insert(TopicId{1});
    EXPECT_EQ(topics.size(), 2U);
    EXPECT_TRUE(topics.contains(TopicId{2}));
    EXPECT_EQ(std::hash<TopicId>{}(TopicId{42}), std::hash<std::uint32_t>{}(42U));
}

TEST(SequenceNumber, NextIncrementsAndWrapsWithoutUndefinedBehaviour) {
    EXPECT_EQ(next(SequenceNumber{0}), SequenceNumber{1});
    constexpr auto kMax = std::numeric_limits<std::uint64_t>::max();
    EXPECT_EQ(next(SequenceNumber{kMax}), SequenceNumber{0});
}

}  // namespace
