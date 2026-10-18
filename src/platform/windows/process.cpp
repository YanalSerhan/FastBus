// Windows backend of fastbus/platform/process.hpp. Arguments are quoted with the rules
// CommandLineToArgvW / the MSVC CRT use to split a command line, so every argument arrives in the
// child exactly as given (spaces, quotes and backslashes included).
#include "fastbus/platform/process.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include <windows.h>

#include "fastbus/core/error.hpp"
#include "fastbus/core/result.hpp"

namespace fastbus::platform {
namespace {

constexpr DWORD kMaxFiniteWaitMs = INFINITE - 1;

HANDLE to_handle(std::intptr_t value) noexcept {
    return reinterpret_cast<HANDLE>(value);  // NOLINT(performance-no-int-to-ptr): stored handle
}

std::uint64_t creation_time(HANDLE process) noexcept {
    FILETIME created{};
    FILETIME exited{};
    FILETIME kernel{};
    FILETIME user{};
    if (GetProcessTimes(process, &created, &exited, &kernel, &user) == 0) {
        return 0;
    }
    return (static_cast<std::uint64_t>(created.dwHighDateTime) << 32U) | created.dwLowDateTime;
}

/// UTF-8 -> UTF-16. Returns false on invalid UTF-8.
bool widen(std::string_view in, std::wstring& out) {
    out.clear();
    if (in.empty()) {
        return true;
    }
    const int in_len = static_cast<int>(in.size());
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, in.data(), in_len, nullptr, 0);
    if (n <= 0) {
        return false;
    }
    out.resize(static_cast<std::size_t>(n));
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, in.data(), in_len, out.data(), n) ==
           n;
}

/// Appends `arg` to `cmd`, quoted so that CommandLineToArgvW returns it unchanged.
void append_quoted(const std::wstring& arg, std::wstring& cmd) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        cmd += arg;
        return;
    }
    cmd += L'"';
    std::size_t backslashes = 0;
    for (const wchar_t c : arg) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        if (c == L'"') {
            cmd.append((backslashes * 2) + 1, L'\\');  // escape the backslashes and the quote
        } else {
            cmd.append(backslashes, L'\\');  // backslashes not before a quote are literal
        }
        backslashes = 0;
        cmd += c;
    }
    cmd.append(backslashes * 2, L'\\');  // they precede the closing quote: double them
    cmd += L'"';
}

}  // namespace

ProcessIdentity current_process() noexcept {
    return ProcessIdentity{.pid = GetCurrentProcessId(),
                           .start_time = creation_time(GetCurrentProcess())};
}

core::Result<ProcessIdentity> identity_of(std::uint64_t pid) noexcept {
    if (pid == 0 || pid > MAXDWORD) {
        return core::fail(core::ErrorCode::NotFound, "no running process with this pid");
    }
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE,
                                 static_cast<DWORD>(pid));
    if (process == nullptr) {
        if (GetLastError() == ERROR_ACCESS_DENIED) {
            return core::fail(core::ErrorCode::PermissionDenied, "OpenProcess: access denied");
        }
        return core::fail(core::ErrorCode::NotFound, "no running process with this pid");
    }
    // A process object outlives the process while handles exist: check it is still running.
    const bool running = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    const std::uint64_t started = creation_time(process);
    static_cast<void>(CloseHandle(process));
    if (!running) {
        return core::fail(core::ErrorCode::NotFound, "process has exited");
    }
    return ProcessIdentity{.pid = pid, .start_time = started};
}

bool is_alive(const ProcessIdentity& who) noexcept {
    const auto now = identity_of(who.pid);
    return now.has_value() && now->start_time == who.start_time;
}

core::Result<Process> Process::spawn(std::string_view path,
                                     std::span<const std::string_view> args) noexcept {
    if (path.empty()) {
        return core::fail(core::ErrorCode::InvalidArgument, "empty executable path");
    }
    // Setup path: string building allocates, which is fine outside the hot path.
    std::wstring wide_path;
    std::wstring wide_arg;
    std::wstring command_line;
    if (!widen(path, wide_path)) {
        return core::fail(core::ErrorCode::InvalidArgument, "executable path is not valid UTF-8");
    }
    append_quoted(wide_path, command_line);
    for (const std::string_view arg : args) {
        if (!widen(arg, wide_arg)) {
            return core::fail(core::ErrorCode::InvalidArgument, "argument is not valid UTF-8");
        }
        command_line += L' ';
        append_quoted(wide_arg, command_line);
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    if (CreateProcessW(wide_path.c_str(), command_line.data(), nullptr, nullptr, FALSE, 0, nullptr,
                       nullptr, &startup, &info) == 0) {
        switch (GetLastError()) {
            case ERROR_FILE_NOT_FOUND:
            case ERROR_PATH_NOT_FOUND:
                return core::fail(core::ErrorCode::NotFound, "CreateProcessW: file not found");
            case ERROR_ACCESS_DENIED:
                return core::fail(core::ErrorCode::PermissionDenied,
                                  "CreateProcessW: access denied");
            default:
                return core::fail(core::ErrorCode::SystemError, "CreateProcessW failed");
        }
    }
    static_cast<void>(CloseHandle(info.hThread));
    Process process;
    process.pid_ = info.dwProcessId;
    process.handle_ = reinterpret_cast<std::intptr_t>(info.hProcess);
    return process;
}

core::Result<int> Process::wait(std::chrono::milliseconds timeout) noexcept {
    if (exited_) {
        return exit_code_;
    }
    const auto ms = std::clamp<std::int64_t>(timeout.count(), 0, kMaxFiniteWaitMs);
    const DWORD r = WaitForSingleObject(to_handle(handle_), static_cast<DWORD>(ms));
    if (r == WAIT_TIMEOUT) {
        return core::fail(core::ErrorCode::Timeout, "child process still running");
    }
    DWORD code = 0;
    if (r != WAIT_OBJECT_0 || GetExitCodeProcess(to_handle(handle_), &code) == 0) {
        return core::fail(core::ErrorCode::SystemError, "waiting for the child process failed");
    }
    exited_ = true;
    exit_code_ = static_cast<int>(code);
    return exit_code_;
}

core::Result<void> Process::kill() noexcept {
    if (exited_) {
        return {};
    }
    // Fails with ACCESS_DENIED if the process already exited; the wait below covers both cases.
    static_cast<void>(TerminateProcess(to_handle(handle_), static_cast<UINT>(kKilledExitCode)));
    if (WaitForSingleObject(to_handle(handle_), INFINITE) != WAIT_OBJECT_0) {
        return core::fail(core::ErrorCode::SystemError, "waiting for the killed child failed");
    }
    DWORD code = 0;
    static_cast<void>(GetExitCodeProcess(to_handle(handle_), &code));
    exited_ = true;
    exit_code_ = static_cast<int>(code);
    return {};
}

bool Process::running() noexcept {
    if (exited_ || handle_ == -1) {
        return false;
    }
    if (WaitForSingleObject(to_handle(handle_), 0) == WAIT_TIMEOUT) {
        return true;
    }
    DWORD code = 0;
    static_cast<void>(GetExitCodeProcess(to_handle(handle_), &code));
    exited_ = true;
    exit_code_ = static_cast<int>(code);
    return false;
}

void Process::reset() noexcept {
    if (handle_ != -1) {
        if (!exited_) {
            static_cast<void>(kill());
        }
        static_cast<void>(CloseHandle(to_handle(handle_)));
    }
    pid_ = 0;
    handle_ = -1;
    exited_ = false;
    exit_code_ = 0;
}

}  // namespace fastbus::platform
