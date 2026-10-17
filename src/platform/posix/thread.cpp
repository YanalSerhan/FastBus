// POSIX (Linux) backend of fastbus/platform/thread.hpp. Needs _GNU_SOURCE for the *_np calls,
// gettid and sched_getcpu; libstdc++ defines it for every C++ translation unit.
#include "fastbus/platform/thread.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <thread>

#include <pthread.h>
#include <sched.h>
#include <unistd.h>

namespace fastbus::platform {

std::uint32_t hardware_concurrency() noexcept {
    return std::max(1U, std::thread::hardware_concurrency());
}

bool pin_current_thread(std::uint32_t cpu) noexcept {
    if (cpu >= CPU_SETSIZE) {
        return false;
    }
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    return pthread_setaffinity_np(pthread_self(), sizeof(set), &set) == 0;
}

bool unpin_current_thread() noexcept {
    // The process-wide mask (pid 0 = calling thread's process) is the widest we may use.
    cpu_set_t set;
    CPU_ZERO(&set);
    if (sched_getaffinity(0, sizeof(set), &set) != 0) {
        return false;
    }
    return pthread_setaffinity_np(pthread_self(), sizeof(set), &set) == 0;
}

bool set_current_thread_name(std::string_view name) noexcept {
    std::array<char, kMaxThreadNameLength + 1> buffer{};
    const std::size_t length = std::min(name.size(), kMaxThreadNameLength);
    for (std::size_t i = 0; i < length; ++i) {
        const auto c = static_cast<unsigned char>(name[i]);
        buffer.at(i) = c < 0x80U ? name[i] : '?';
    }
    return pthread_setname_np(pthread_self(), buffer.data()) == 0;
}

std::size_t current_thread_name(std::span<char> out) noexcept {
    std::array<char, kMaxThreadNameLength + 1> buffer{};
    if (out.empty() || pthread_getname_np(pthread_self(), buffer.data(), buffer.size()) != 0) {
        return 0;
    }
    const std::string_view name{buffer.data()};
    const std::size_t length = std::min(name.size(), out.size() - 1);
    std::copy_n(name.begin(), length, out.begin());
    out[length] = '\0';
    return length;
}

std::uint64_t current_thread_id() noexcept {
    return static_cast<std::uint64_t>(gettid());
}

std::uint32_t current_cpu() noexcept {
    const int cpu = sched_getcpu();
    return cpu < 0 ? 0U : static_cast<std::uint32_t>(cpu);
}

}  // namespace fastbus::platform
