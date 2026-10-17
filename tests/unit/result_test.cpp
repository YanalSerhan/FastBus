#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>

#include "fastbus/core/error.hpp"
#include "fastbus/core/result.hpp"

namespace {

using fastbus::core::Error;
using fastbus::core::ErrorCode;
using fastbus::core::fail;
using fastbus::core::Result;
using fastbus::core::Unexpected;

Result<int> parse_positive(int x) {
    if (x <= 0) {
        return fail(ErrorCode::InvalidArgument, "x must be positive");
    }
    return x;
}

Result<void> validate(bool ok) {
    if (!ok) {
        return fail(ErrorCode::NotSupported, "not ok");
    }
    return {};
}

static_assert(std::is_trivially_copyable_v<Error>, "errors must be copyable without allocation");
static_assert(!std::is_convertible_v<Result<int>, bool>, "operator bool must be explicit");

TEST(Result, HoldsValue) {
    const auto r = parse_positive(5);
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(static_cast<bool>(r));
    EXPECT_EQ(r.value(), 5);
    EXPECT_EQ(*r, 5);
    EXPECT_EQ(r.value_or(-1), 5);
}

TEST(Result, HoldsError) {
    const auto r = parse_positive(0);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, ErrorCode::InvalidArgument);
    EXPECT_EQ(std::string_view{r.error().message}, "x must be positive");
    EXPECT_EQ(r.value_or(-1), -1);
}

TEST(Result, SupportsMoveOnlyValues) {
    Result<std::unique_ptr<int>> r = std::make_unique<int>(7);
    ASSERT_TRUE(r);
    EXPECT_EQ(**r, 7);
    const std::unique_ptr<int> owned = std::move(r).value();
    EXPECT_EQ(*owned, 7);
}

TEST(Result, ArrowAccessesMembers) {
    struct Point {
        int x;
        int y;
    };
    Result<Point> r = Point{.x = 1, .y = 2};
    EXPECT_EQ(r->x + r->y, 3);
    r->x = 10;
    EXPECT_EQ(r.value().x, 10);
}

TEST(Result, CustomErrorType) {
    enum class ParseError : std::uint8_t { Empty, Overflow };
    const Result<int, ParseError> r = Unexpected{ParseError::Overflow};
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error(), ParseError::Overflow);
}

TEST(ResultVoid, SuccessAndFailure) {
    EXPECT_TRUE(validate(true).has_value());
    validate(true).value();  // does not terminate on success
    const auto bad = validate(false);
    ASSERT_FALSE(bad);
    EXPECT_EQ(bad.error().code, ErrorCode::NotSupported);
}

TEST(Error, EqualityUsesOnlyTheCode) {
    EXPECT_EQ((Error{ErrorCode::Misaligned, "a"}), (Error{ErrorCode::Misaligned, "b"}));
    EXPECT_NE((Error{ErrorCode::Misaligned, "a"}), (Error{ErrorCode::BufferTooSmall, "a"}));
}

TEST(Error, EveryCodeHasAName) {
    for (const auto code :
         {ErrorCode::InvalidArgument, ErrorCode::InvalidCapacity, ErrorCode::BufferTooSmall,
          ErrorCode::Misaligned, ErrorCode::OutOfMemory, ErrorCode::NotSupported,
          ErrorCode::SystemError}) {
        EXPECT_FALSE(to_string(code).empty());
        EXPECT_NE(to_string(code), "unknown error");
    }
    EXPECT_EQ(to_string(static_cast<ErrorCode>(0xFFFF)), "unknown error");
}

// Contract violations terminate (they never throw). "threadsafe" re-executes the binary, which
// is reliable under sanitizers and on Windows.
class ResultDeathTest : public ::testing::Test {
protected:
    void SetUp() override { GTEST_FLAG_SET(death_test_style, "threadsafe"); }
};

TEST_F(ResultDeathTest, ValueOnErrorTerminates) {
    EXPECT_DEATH(static_cast<void>(parse_positive(-1).value()), "called on an error");
}

TEST_F(ResultDeathTest, ErrorOnValueTerminates) {
    EXPECT_DEATH(static_cast<void>(parse_positive(1).error()), "called on a value");
}

TEST_F(ResultDeathTest, VoidValueOnErrorTerminates) {
    EXPECT_DEATH(validate(false).value(), "called on an error");
}

}  // namespace
