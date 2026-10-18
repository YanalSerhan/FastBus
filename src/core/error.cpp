#include "fastbus/core/error.hpp"

namespace fastbus::core {

std::string_view to_string(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::InvalidArgument:
            return "invalid argument";
        case ErrorCode::InvalidCapacity:
            return "invalid capacity";
        case ErrorCode::BufferTooSmall:
            return "buffer too small";
        case ErrorCode::Misaligned:
            return "misaligned buffer";
        case ErrorCode::OutOfMemory:
            return "out of memory";
        case ErrorCode::NotSupported:
            return "not supported";
        case ErrorCode::SystemError:
            return "system error";
        case ErrorCode::AlreadyExists:
            return "already exists";
        case ErrorCode::NotFound:
            return "not found";
        case ErrorCode::PermissionDenied:
            return "permission denied";
        case ErrorCode::Timeout:
            return "timeout";
    }
    return "unknown error";  // a value outside the enum (e.g. read from corrupted memory)
}

}  // namespace fastbus::core
