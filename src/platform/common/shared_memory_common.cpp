// Shared-memory name rules, shared by both backends (ADR-002: code common to the backends lives
// in src/platform/common/). The rule is deliberately the intersection of what POSIX and Windows
// accept, so a name valid on one OS is valid on the other.
#include <algorithm>
#include <cstddef>
#include <span>
#include <string_view>
#include <utility>

#include "fastbus/platform/shared_memory.hpp"

namespace fastbus::platform {
namespace {

constexpr bool is_name_char(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
           c == '_' || c == '-';
}

}  // namespace

bool is_valid_shm_name(std::string_view name) noexcept {
    return !name.empty() && name.size() <= kMaxShmNameLength &&
           std::all_of(name.begin(), name.end(), is_name_char);
}

namespace detail {

std::size_t shm_os_name(std::string_view prefix, std::string_view name,
                        std::span<char> out) noexcept {
    const std::size_t length = prefix.size() + name.size();
    if (!is_valid_shm_name(name) || out.size() < length + 1) {
        return 0;
    }
    auto it = std::copy(prefix.begin(), prefix.end(), out.begin());
    it = std::copy(name.begin(), name.end(), it);
    *it = '\0';
    return length;
}

}  // namespace detail
}  // namespace fastbus::platform

// ---- Backend-independent parts of SharedMemory (each backend defines close() and the factories).
namespace fastbus::platform {

SharedMemory::SharedMemory(SharedMemory&& other) noexcept
    : data_(other.data_),
      size_(other.size_),
      handle_(other.handle_),
      creator_(other.creator_),
      name_length_(other.name_length_),
      name_(other.name_) {
    other.data_ = nullptr;
    other.size_ = 0;
    other.handle_ = -1;
    other.creator_ = false;
}

SharedMemory& SharedMemory::operator=(SharedMemory&& other) noexcept {
    if (this != &other) {
        close();
        data_ = std::exchange(other.data_, nullptr);
        size_ = std::exchange(other.size_, 0);
        handle_ = std::exchange(other.handle_, -1);
        creator_ = std::exchange(other.creator_, false);
        name_length_ = other.name_length_;
        name_ = other.name_;
    }
    return *this;
}

SharedMemory::~SharedMemory() {
    close();
}

}  // namespace fastbus::platform
