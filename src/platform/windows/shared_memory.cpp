// Windows backend of fastbus/platform/shared_memory.hpp: pagefile-backed file mappings in the
// per-session Local\ namespace (Global\ needs SeCreateGlobalPrivilege). WIN32_LEAN_AND_MEAN,
// NOMINMAX and _WIN32_WINNT come from fastbus::platform_libs.
#include "fastbus/platform/shared_memory.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

#include <windows.h>

#include "fastbus/core/error.hpp"
#include "fastbus/core/result.hpp"

namespace fastbus::platform {
namespace {

constexpr std::string_view kPrefix = "Local\\fastbus_";
constexpr std::size_t kOsNameCapacity = kMaxShmNameLength + 32;
using WideName = std::array<wchar_t, kOsNameCapacity>;

/// Builds the wide OS name; names are ASCII by construction (is_valid_shm_name), so widening is
/// a plain character copy. Returns false for an invalid name.
bool make_os_name(std::string_view name, WideName& out) noexcept {
    std::array<char, kOsNameCapacity> narrow{};
    const std::size_t length = detail::shm_os_name(kPrefix, name, narrow);
    if (length == 0) {
        return false;
    }
    for (std::size_t i = 0; i <= length; ++i) {  // includes the terminating NUL
        out.at(i) = static_cast<wchar_t>(narrow.at(i));
    }
    return true;
}

core::Unexpected<core::Error> from_last_error(DWORD err, const char* what) noexcept {
    switch (err) {
        case ERROR_ALREADY_EXISTS:
            return core::fail(core::ErrorCode::AlreadyExists, what);
        case ERROR_FILE_NOT_FOUND:
            return core::fail(core::ErrorCode::NotFound, what);
        case ERROR_ACCESS_DENIED:
            return core::fail(core::ErrorCode::PermissionDenied, what);
        case ERROR_NOT_ENOUGH_MEMORY:
        case ERROR_OUTOFMEMORY:
        case ERROR_COMMITMENT_LIMIT:
        case ERROR_DISK_FULL:
            return core::fail(core::ErrorCode::OutOfMemory, what);
        case ERROR_INVALID_PARAMETER:
        case ERROR_INVALID_NAME:
            return core::fail(core::ErrorCode::InvalidArgument, what);
        default:
            return core::fail(core::ErrorCode::SystemError, what);
    }
}

HANDLE to_handle(std::intptr_t value) noexcept {
    return reinterpret_cast<HANDLE>(value);  // NOLINT(performance-no-int-to-ptr): stored handle
}

}  // namespace

std::size_t page_size() noexcept {
    static const std::size_t kPageSize = [] {
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        return static_cast<std::size_t>(info.dwPageSize);
    }();
    return kPageSize;
}

core::Result<SharedMemory> SharedMemory::create(std::string_view name, std::size_t size) noexcept {
    WideName os_name{};
    if (!make_os_name(name, os_name)) {
        return core::fail(core::ErrorCode::InvalidArgument, "invalid shared-memory name");
    }
    const std::size_t page = page_size();
    if (size == 0 || size > std::numeric_limits<std::size_t>::max() - page) {
        return core::fail(core::ErrorCode::InvalidArgument, "shared-memory size out of range");
    }
    const std::size_t rounded = (size + page - 1) / page * page;
    const auto high = static_cast<DWORD>(static_cast<std::uint64_t>(rounded) >> 32U);
    const auto low = static_cast<DWORD>(static_cast<std::uint64_t>(rounded) & 0xFFFF'FFFFU);

    // ERROR_ALREADY_EXISTS is only *set* when the mapping existed; clear any stale value first.
    SetLastError(ERROR_SUCCESS);
    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE | SEC_COMMIT,
                                        high, low, os_name.data());
    const DWORD create_error = GetLastError();
    if (mapping == nullptr) {
        return from_last_error(create_error, "CreateFileMappingW failed");
    }
    if (create_error == ERROR_ALREADY_EXISTS) {
        // We got a handle to someone else's mapping: drop it, creation must be exclusive.
        static_cast<void>(CloseHandle(mapping));
        return core::fail(core::ErrorCode::AlreadyExists, "shared-memory name already exists");
    }
    void* view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, rounded);
    if (view == nullptr) {
        const DWORD err = GetLastError();
        static_cast<void>(CloseHandle(mapping));
        return from_last_error(err, "MapViewOfFile failed");
    }

    SharedMemory shm;
    shm.data_ = static_cast<std::byte*>(view);
    shm.size_ = rounded;
    shm.handle_ = reinterpret_cast<std::intptr_t>(mapping);
    shm.creator_ = true;
    shm.name_length_ = name.copy(shm.name_.data(), kMaxShmNameLength);
    return shm;
}

core::Result<SharedMemory> SharedMemory::open(std::string_view name) noexcept {
    WideName os_name{};
    if (!make_os_name(name, os_name)) {
        return core::fail(core::ErrorCode::InvalidArgument, "invalid shared-memory name");
    }
    HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, os_name.data());
    if (mapping == nullptr) {
        return from_last_error(GetLastError(), "OpenFileMappingW failed");
    }
    void* view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);  // whole mapping
    if (view == nullptr) {
        const DWORD err = GetLastError();
        static_cast<void>(CloseHandle(mapping));
        return from_last_error(err, "MapViewOfFile failed");
    }
    // Windows cannot report a mapping's exact size; the view's region size is the mapping size
    // rounded to pages, which equals what create() reported because create() rounds too.
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(view, &info, sizeof(info)) == 0) {
        const DWORD err = GetLastError();
        static_cast<void>(UnmapViewOfFile(view));
        static_cast<void>(CloseHandle(mapping));
        return from_last_error(err, "VirtualQuery failed");
    }

    SharedMemory shm;
    shm.data_ = static_cast<std::byte*>(view);
    shm.size_ = info.RegionSize;
    shm.handle_ = reinterpret_cast<std::intptr_t>(mapping);
    shm.creator_ = false;
    shm.name_length_ = name.copy(shm.name_.data(), kMaxShmNameLength);
    return shm;
}

core::Result<void> SharedMemory::remove(std::string_view name) noexcept {
    if (!is_valid_shm_name(name)) {
        return core::fail(core::ErrorCode::InvalidArgument, "invalid shared-memory name");
    }
    return core::fail(core::ErrorCode::NotSupported,
                      "Windows mappings have no name to remove; they die with their last handle");
}

void SharedMemory::close() noexcept {
    if (data_ != nullptr) {
        static_cast<void>(UnmapViewOfFile(data_));
    }
    if (handle_ != -1) {
        static_cast<void>(CloseHandle(to_handle(handle_)));
    }
    data_ = nullptr;
    size_ = 0;
    handle_ = -1;
    creator_ = false;
}

}  // namespace fastbus::platform
