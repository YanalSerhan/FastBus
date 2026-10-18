/// @file shared_memory.hpp
/// @brief Named shared memory, RAII (M2.1, PRD_shm_transport.md section 3).
///
/// | Operation | Windows                                 | POSIX (Linux)                       |
/// |-----------|-----------------------------------------|-------------------------------------|
/// | create    | CreateFileMappingW(Local\fastbus_<name>) | shm_open(O_EXCL) + posix_fallocate  |
/// | open      | OpenFileMappingW                        | shm_open(O_RDWR) + fstat            |
/// | map       | MapViewOfFile (64 KiB aligned)          | mmap(MAP_SHARED) (page aligned)     |
/// | remove    | NotSupported (dies with its last handle) | shm_unlink                          |
///
/// Lifetime ("creator owns lifetime", PRD section 3.1): the creator's destructor unmaps and, on
/// POSIX, unlinks the name. Openers keep a valid mapping until they close it. Stale segments
/// left by a crashed creator are removed explicitly with `remove()` (or fbclean, M2.9).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "fastbus/core/result.hpp"

namespace fastbus::platform {

/// Longest name accepted (POSIX NAME_MAX is 255 including the "/fastbus_" prefix).
inline constexpr std::size_t kMaxShmNameLength = 200;

/// True if `name` is 1..kMaxShmNameLength characters from [A-Za-z0-9._-]. The same rule on every
/// OS, so a name that works on one works on all.
[[nodiscard]] bool is_valid_shm_name(std::string_view name) noexcept;

/// OS page size (4 KiB on x86-64). Shared-memory sizes are rounded up to it.
[[nodiscard]] std::size_t page_size() noexcept;

class SharedMemory {
public:
    /// Creates a new segment of at least `size` bytes (rounded up to page_size()), zero-filled.
    /// Errors: InvalidArgument (bad name, size 0), AlreadyExists, PermissionDenied, OutOfMemory,
    /// SystemError.
    [[nodiscard]] static core::Result<SharedMemory> create(std::string_view name,
                                                           std::size_t size) noexcept;

    /// Maps an existing segment. Errors: InvalidArgument, NotFound, PermissionDenied, SystemError.
    [[nodiscard]] static core::Result<SharedMemory> open(std::string_view name) noexcept;

    /// Deletes a (stale) name so it can be created again. Existing mappings stay valid.
    /// Errors: InvalidArgument, NotFound, PermissionDenied; NotSupported on Windows.
    [[nodiscard]] static core::Result<void> remove(std::string_view name) noexcept;

    SharedMemory(SharedMemory&& other) noexcept;
    SharedMemory& operator=(SharedMemory&& other) noexcept;
    SharedMemory(const SharedMemory&) = delete;
    SharedMemory& operator=(const SharedMemory&) = delete;
    ~SharedMemory();

    [[nodiscard]] std::byte* data() const noexcept { return data_; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::span<std::byte> bytes() const noexcept { return {data_, size_}; }
    [[nodiscard]] bool is_creator() const noexcept { return creator_; }
    [[nodiscard]] std::string_view name() const noexcept { return {name_.data(), name_length_}; }

private:
    SharedMemory() noexcept = default;
    void close() noexcept;  // unmap, close the handle, unlink if creator (POSIX)

    std::byte* data_ = nullptr;
    std::size_t size_ = 0;
    std::intptr_t handle_ = -1;  // Windows HANDLE or -1; POSIX: unused (fd closed after mmap)
    bool creator_ = false;
    std::size_t name_length_ = 0;
    std::array<char, kMaxShmNameLength + 1> name_{};
};

namespace detail {
/// Writes `prefix + name` (NUL-terminated) into `out` and returns its length, or 0 if `name` is
/// invalid or `out` is too small. Shared by both backends
/// (src/platform/common/shared_memory_common.cpp); each backend passes its prefix ("/fastbus_" on
/// POSIX, "Local\\fastbus_" on Windows).
[[nodiscard]] std::size_t shm_os_name(std::string_view prefix, std::string_view name,
                                      std::span<char> out) noexcept;
}  // namespace detail

}  // namespace fastbus::platform
