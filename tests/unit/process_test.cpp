// platform::Process and ProcessIdentity tests (M2.2). Every child is the fb_test_child helper
// with a watchdog, and every Process kills its child on destruction, so no test can leave a
// process behind or hang CI.
#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fastbus/core/error.hpp"
#include "fastbus/platform/process.hpp"
#include "fastbus/platform/shared_memory.hpp"

#include "ipc_test_support.hpp"

namespace {

using namespace std::chrono_literals;
using fastbus::core::ErrorCode;
using fastbus::platform::current_process;
using fastbus::platform::identity_of;
using fastbus::platform::is_alive;
using fastbus::platform::kKilledExitCode;
using fastbus::platform::Process;
using fastbus::platform::ProcessIdentity;
using fastbus::platform::SharedMemory;
using fastbus::test::error_of;
using fastbus::test::eventually;
using fastbus::test::kTestChild;
using fastbus::test::unique_shm_name;

Process spawn_or_die(std::initializer_list<std::string_view> args) {
    const std::vector<std::string_view> list{args};
    auto child = Process::spawn(kTestChild, list);
    EXPECT_TRUE(child.has_value()) << "spawning fb_test_child failed";
    return std::move(child).value();
}

TEST(Process, ReportsTheChildsExitCode) {
    auto child = spawn_or_die({"exit", "7"});
    EXPECT_EQ(child.wait(20s).value(), 7);
    EXPECT_FALSE(child.running());
    EXPECT_EQ(child.wait(0ms).value(), 7);  // reaped: the same code again
}

TEST(Process, WaitTimesOutWhileTheChildRunsThenKillEndsIt) {
    auto child = spawn_or_die({"sleep", "20000"});
    EXPECT_TRUE(child.running());
    const auto waited = child.wait(20ms);
    ASSERT_FALSE(waited.has_value());
    EXPECT_EQ(error_of(waited), ErrorCode::Timeout);
    ASSERT_TRUE(child.kill().has_value());
    EXPECT_FALSE(child.running());
    EXPECT_EQ(child.wait(0ms).value(), kKilledExitCode);
}

TEST(Process, SpawningAMissingExecutableFails) {
    const std::array<std::string_view, 0> none{};
    const auto child = Process::spawn("/definitely/not/here/fb_missing_binary", none);
    ASSERT_FALSE(child.has_value());
    EXPECT_EQ(error_of(child), ErrorCode::NotFound);
    EXPECT_EQ(error_of(Process::spawn("", none)), ErrorCode::InvalidArgument);
}

TEST(Process, WatchdogEndsAHungChild) {
    auto child = spawn_or_die({"--watchdog-ms", "200", "hang"});
    EXPECT_EQ(child.wait(20s).value(), 124);
}

TEST(Process, DestroyingARunningChildKillsIt) {
    ProcessIdentity who{};
    {
        auto child = spawn_or_die({"sleep", "30000"});
        ASSERT_TRUE(eventually([&] { return identity_of(child.pid()).has_value(); }));
        who = identity_of(child.pid()).value();
        EXPECT_TRUE(is_alive(who));
    }
    EXPECT_FALSE(is_alive(who));
}

TEST(Process, MoveTransfersTheChild) {
    auto a = spawn_or_die({"exit", "3"});
    const std::uint64_t pid = a.pid();
    Process b = std::move(a);
    EXPECT_EQ(a.pid(), 0U);  // NOLINT(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    EXPECT_EQ(b.pid(), pid);
    EXPECT_EQ(b.wait(20s).value(), 3);
}

TEST(Process, ArgumentsArriveUnchanged) {
    // The cases Windows command-line quoting gets wrong if done naively.
    const std::vector<std::string_view> tricky{
        "plain", "with space", R"(quote"inside)", R"(trailing\)", R"(back\\slash "mix")", "", "\t"};
    const std::string name = unique_shm_name("args");
    const auto shm = SharedMemory::create(name, 4096);
    ASSERT_TRUE(shm.has_value());

    std::vector<std::string_view> args{"shm-args", name};
    args.insert(args.end(), tricky.begin(), tricky.end());
    auto child = Process::spawn(kTestChild, args);
    ASSERT_TRUE(child.has_value());
    ASSERT_EQ(child->wait(20s).value(), 0);

    const std::byte* p = shm->data();
    std::uint32_t count = 0;
    std::memcpy(&count, p, sizeof(count));
    p += sizeof(count);
    ASSERT_EQ(count, tricky.size());
    for (const std::string_view expected : tricky) {
        std::uint32_t length = 0;
        std::memcpy(&length, p, sizeof(length));
        p += sizeof(length);
        const std::string got(reinterpret_cast<const char*>(p), length);
        p += length;
        EXPECT_EQ(got, expected);
    }
}

TEST(Process, MoveAssigningOverARunningChildKillsIt) {
    auto running = spawn_or_die({"sleep", "30000"});
    ASSERT_TRUE(eventually([&] { return identity_of(running.pid()).has_value(); }));
    const ProcessIdentity old_child = identity_of(running.pid()).value();
    auto replacement = spawn_or_die({"exit", "5"});
    running = std::move(replacement);
    EXPECT_FALSE(is_alive(old_child));
    EXPECT_EQ(running.wait(20s).value(), 5);
}

// ---- Identity and liveness -----------------------------------------------------------------
TEST(ProcessIdentity, CurrentProcessIsAliveAndMatchesIdentityOf) {
    const ProcessIdentity me = current_process();
    EXPECT_NE(me.pid, 0U);
    EXPECT_NE(me.start_time, 0U);
    EXPECT_TRUE(is_alive(me));
    EXPECT_EQ(identity_of(me.pid).value(), me);
}

TEST(ProcessIdentity, SamePidWithADifferentStartTimeIsNotAlive) {
    // What a reused pid looks like: right pid, wrong start time.
    ProcessIdentity impostor = current_process();
    impostor.start_time += 1;
    EXPECT_FALSE(is_alive(impostor));
}

TEST(ProcessIdentity, UnknownPidIsNotFound) {
    EXPECT_EQ(error_of(identity_of(0)), ErrorCode::NotFound);
    EXPECT_FALSE(is_alive(ProcessIdentity{.pid = 0, .start_time = 0}));
}

TEST(ProcessIdentity, ChildReportsTheSameIdentityAsTheParentSees) {
    const std::string name = unique_shm_name("identity");
    const auto shm = SharedMemory::create(name, 4096);
    ASSERT_TRUE(shm.has_value());
    auto child = spawn_or_die({"shm-identity", name, "30000"});

    ProcessIdentity reported{};
    ASSERT_TRUE(eventually([&] {
        std::memcpy(&reported, shm->data(), sizeof(reported));
        return reported.pid != 0;
    }));
    EXPECT_EQ(reported.pid, child.pid());
    EXPECT_EQ(identity_of(child.pid()).value(), reported);
    EXPECT_TRUE(is_alive(reported));

    ASSERT_TRUE(child.kill().has_value());
    EXPECT_FALSE(is_alive(reported));
}

TEST(ProcessIdentity, ExitedButUnreapedChildIsNotAlive) {
    // POSIX keeps an exited child as a zombie until waitpid; it must already count as dead.
    auto child = spawn_or_die({"exit", "0"});
    const std::uint64_t pid = child.pid();
    EXPECT_TRUE(eventually([&] { return !identity_of(pid).has_value(); }));
    EXPECT_EQ(child.wait(20s).value(), 0);
}

}  // namespace
