// POSIX backend of fastbus/platform/shared_memory.hpp: shm_open + ftruncate + posix_fallocate +
// mmap. posix_fallocate reserves the pages up front: on a size-limited /dev/shm (containers,
// CI) a too-large segment then fails cleanly with OutOfMemory instead of SIGBUS on first touch.
#include "fastbus/platform/shared_memory.hpp"

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "fastbus/core/error.hpp"
#include "fastbus/core/result.hpp"

namespace fastbus::platform {
namespace {

constexpr std::string_view kPrefix = "/fastbus_";
using OsName = std::array<char, kMaxShmNameLength + 16>;

core::Unexpected<core::Error> from_errno(int err, const char* what) noexcept {
    switch (err) {
        case EEXIST:
            return core::fail(core::ErrorCode::AlreadyExists, what);
        case ENOENT:
            return core::fail(core::ErrorCode::NotFound, what);
        case EACCES:
        case EPERM:
            return core::fail(core::ErrorCode::PermissionDenied, what);
        case ENOMEM:
        case ENOSPC:
        case EFBIG:
            return core::fail(core::ErrorCode::OutOfMemory, what);
        case EINVAL:
        case ENAMETOOLONG:
            return core::fail(core::ErrorCode::InvalidArgument, what);
        default:
            return core::fail(core::ErrorCode::SystemError, what);
    }
}

}  // namespace

std::size_t page_size() noexcept {
    static const std::size_t kPageSize = [] {
        const long page = sysconf(_SC_PAGESIZE);
        return page > 0 ? static_cast<std::size_t>(page) : std::size_t{4096};
    }();
    return kPageSize;
}

core::Result<SharedMemory> SharedMemory::create(std::string_view name, std::size_t size) noexcept {
    OsName os_name{};
    if (detail::shm_os_name(kPrefix, name, os_name) == 0) {
        return core::fail(core::ErrorCode::InvalidArgument, "invalid shared-memory name");
    }
    const std::size_t page = page_size();
    if (size == 0 || size > std::numeric_limits<std::size_t>::max() - page ||
        size > static_cast<std::size_t>(std::numeric_limits<off_t>::max())) {
        return core::fail(core::ErrorCode::InvalidArgument, "shared-memory size out of range");
    }
    const std::size_t rounded = (size + page - 1) / page * page;

    const int fd = shm_open(os_name.data(), O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        return from_errno(errno, "shm_open(O_CREAT | O_EXCL) failed");
    }
    // From here on, any failure must undo the creation so no half-made segment is left behind.
    const auto undo = [&](int err, const char* what) {
        static_cast<void>(::close(fd));
        static_cast<void>(shm_unlink(os_name.data()));
        return from_errno(err, what);
    };
    if (ftruncate(fd, static_cast<off_t>(rounded)) != 0) {
        return undo(errno, "ftruncate on shared memory failed");
    }
    if (const int err = posix_fallocate(fd, 0, static_cast<off_t>(rounded)); err != 0) {
        return undo(err, "posix_fallocate on shared memory failed (is /dev/shm full?)");
    }
    void* mapped = mmap(nullptr, rounded, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) {
        return undo(errno, "mmap of shared memory failed");
    }
    static_cast<void>(::close(fd));  // the mapping keeps the object alive

    SharedMemory shm;
    shm.data_ = static_cast<std::byte*>(mapped);
    shm.size_ = rounded;
    shm.creator_ = true;
    shm.name_length_ = name.copy(shm.name_.data(), kMaxShmNameLength);
    return shm;
}

core::Result<SharedMemory> SharedMemory::open(std::string_view name) noexcept {
    OsName os_name{};
    if (detail::shm_os_name(kPrefix, name, os_name) == 0) {
        return core::fail(core::ErrorCode::InvalidArgument, "invalid shared-memory name");
    }
    const int fd = shm_open(os_name.data(), O_RDWR, 0);
    if (fd < 0) {
        return from_errno(errno, "shm_open of an existing segment failed");
    }
    struct stat info {};
    if (fstat(fd, &info) != 0) {
        const int err = errno;
        static_cast<void>(::close(fd));
        return from_errno(err, "fstat on shared memory failed");
    }
    if (info.st_size <= 0) {
        // A creator that crashed between shm_open and ftruncate leaves an empty object behind.
        static_cast<void>(::close(fd));
        return core::fail(core::ErrorCode::SystemError,
                          "shared-memory segment is empty (creator did not finish)");
    }
    const auto size = static_cast<std::size_t>(info.st_size);
    void* mapped = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    const int err = errno;
    static_cast<void>(::close(fd));
    if (mapped == MAP_FAILED) {
        return from_errno(err, "mmap of an existing segment failed");
    }

    SharedMemory shm;
    shm.data_ = static_cast<std::byte*>(mapped);
    shm.size_ = size;
    shm.creator_ = false;
    shm.name_length_ = name.copy(shm.name_.data(), kMaxShmNameLength);
    return shm;
}

core::Result<void> SharedMemory::remove(std::string_view name) noexcept {
    OsName os_name{};
    if (detail::shm_os_name(kPrefix, name, os_name) == 0) {
        return core::fail(core::ErrorCode::InvalidArgument, "invalid shared-memory name");
    }
    if (shm_unlink(os_name.data()) != 0) {
        return from_errno(errno, "shm_unlink failed");
    }
    return {};
}

void SharedMemory::close() noexcept {
    if (data_ != nullptr) {
        static_cast<void>(munmap(data_, size_));
        if (creator_) {
            OsName os_name{};
            if (detail::shm_os_name(kPrefix, name(), os_name) != 0) {
                // ENOENT is fine: the name may have been removed explicitly already.
                static_cast<void>(shm_unlink(os_name.data()));
            }
        }
    }
    data_ = nullptr;
    size_ = 0;
    creator_ = false;
}

}  // namespace fastbus::platform
