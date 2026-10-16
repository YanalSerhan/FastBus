#include <gtest/gtest.h>

#include <string>

#include "fastbus/core/build_info.hpp"
#include "fastbus/version.hpp"

namespace {

// version() must be usable at compile time.
static_assert(fastbus::version() == fastbus::kVersion);
static_assert(!fastbus::kVersion.text.empty());

TEST(Version, TextMatchesNumericComponents) {
    constexpr auto kV = fastbus::version();
    const std::string expected =
        std::to_string(kV.major) + "." + std::to_string(kV.minor) + "." + std::to_string(kV.patch);
    EXPECT_EQ(kV.text, expected);
}

TEST(Version, InitialReleaseIsZeroOneZero) {
    // Guards against an accidental bump; update together with project(VERSION) + CHANGELOG.
    EXPECT_EQ(fastbus::kVersion.text, "0.1.0");
}

TEST(Version, HeadersMatchLinkedLibrary) {
    EXPECT_TRUE(fastbus::core::headers_match_library());
    EXPECT_EQ(fastbus::core::library_version(), fastbus::version());
}

}  // namespace
