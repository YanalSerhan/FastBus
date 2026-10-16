#include "fastbus/core/build_info.hpp"

namespace fastbus::core {

// Captured when this translation unit is compiled, i.e. when the library is built.
VersionInfo library_version() noexcept {
    return kVersion;
}

bool headers_match_library() noexcept {
    return library_version() == version();
}

}  // namespace fastbus::core
