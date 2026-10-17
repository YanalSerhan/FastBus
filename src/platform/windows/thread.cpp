// Windows backend of fastbus/platform/thread.hpp. WIN32_LEAN_AND_MEAN/NOMINMAX and
// _WIN32_WINNT=0x0A00 (Windows 10, needed for SetThreadDescription) come from platform_libs.
#include "fastbus/platform/thread.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <thread>

#include <windows.h>

namespace fastbus::platform {
namespace {

constexpr std::uint32_t kCpusPerGroup = 64;  // one processor group = one 64-bit affinity mask

}  // namespace

std::uint32_t hardware_concurrency() noexcept {
    return std::max(1U, std::thread::hardware_concurrency());
}

bool pin_current_thread(std::uint32_t cpu) noexcept {
    if (cpu >= kCpusPerGroup || cpu >= hardware_concurrency()) {
        return false;
    }
    const DWORD_PTR mask = DWORD_PTR{1} << cpu;
    return SetThreadAffinityMask(GetCurrentThread(), mask) != 0;
}

bool unpin_current_thread() noexcept {
    DWORD_PTR process_mask = 0;
    DWORD_PTR system_mask = 0;
    if (GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask) == 0) {
        return false;
    }
    return SetThreadAffinityMask(GetCurrentThread(), process_mask) != 0;
}

bool set_current_thread_name(std::string_view name) noexcept {
    std::array<wchar_t, kMaxThreadNameLength + 1> wide{};
    const std::size_t length = std::min(name.size(), kMaxThreadNameLength);
    for (std::size_t i = 0; i < length; ++i) {
        const auto c = static_cast<unsigned char>(name[i]);
        wide.at(i) = c < 0x80U ? static_cast<wchar_t>(c) : L'?';
    }
    return SetThreadDescription(GetCurrentThread(), wide.data()) >= 0;  // HRESULT success
}

std::size_t current_thread_name(std::span<char> out) noexcept {
    if (out.empty()) {
        return 0;
    }
    PWSTR description = nullptr;
    if (GetThreadDescription(GetCurrentThread(), &description) < 0 || description == nullptr) {
        return 0;
    }
    const std::wstring_view wide{description};
    const std::size_t length = std::min({wide.size(), out.size() - 1, kMaxThreadNameLength});
    for (std::size_t i = 0; i < length; ++i) {
        out[i] = wide[i] < 0x80 ? static_cast<char>(wide[i]) : '?';
    }
    out[length] = '\0';
    static_cast<void>(LocalFree(description));  // GetThreadDescription uses LocalAlloc
    return length;
}

std::uint64_t current_thread_id() noexcept {
    return GetCurrentThreadId();
}

std::uint32_t current_cpu() noexcept {
    return GetCurrentProcessorNumber();
}

}  // namespace fastbus::platform
