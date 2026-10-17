#include "fastbus/core/assert.hpp"

#include <cstdio>
#include <cstdlib>

namespace fastbus::core {

// GCOVR_EXCL_START: runs only in death-test child processes, which abort() before gcov writes
// its counters. Covered behaviourally by the death tests (result_test, spsc_ring_test).
void fail_fast(const char* what, std::source_location where) noexcept {
    // fprintf to stderr is unbuffered and does not allocate on the supported C runtimes. Its
    // result is ignored on purpose: we are about to abort and have nowhere to report a failure.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): the one printf-style call in the library.
    static_cast<void>(std::fprintf(stderr, "fastbus: fatal: %s (%s:%u)\n",
                                   what != nullptr ? what : "(null)", where.file_name(),
                                   static_cast<unsigned>(where.line())));
    std::abort();
}
// GCOVR_EXCL_STOP

}  // namespace fastbus::core
