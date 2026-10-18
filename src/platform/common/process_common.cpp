// Backend-independent parts of platform::Process (each backend defines spawn, wait, kill,
// running and reset).
#include <utility>

#include "fastbus/platform/process.hpp"

namespace fastbus::platform {

Process::Process(Process&& other) noexcept
    : pid_(std::exchange(other.pid_, 0)),
      handle_(std::exchange(other.handle_, -1)),
      exited_(std::exchange(other.exited_, false)),
      exit_code_(std::exchange(other.exit_code_, 0)) {}

Process& Process::operator=(Process&& other) noexcept {
    if (this != &other) {
        reset();
        pid_ = std::exchange(other.pid_, 0);
        handle_ = std::exchange(other.handle_, -1);
        exited_ = std::exchange(other.exited_, false);
        exit_code_ = std::exchange(other.exit_code_, 0);
    }
    return *this;
}

Process::~Process() {
    reset();
}

}  // namespace fastbus::platform
