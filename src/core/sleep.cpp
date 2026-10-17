#include "fastbus/core/sleep.hpp"

#include "fastbus/platform/cpu.hpp"
#include "fastbus/platform/time_source.hpp"

namespace fastbus::core {

void sleep_for(Duration d) noexcept {
    platform::sleep_for_ns(d.count_ns());
}

void precise_sleep_until(const Clock& clock, Timestamp deadline, Duration spin_margin) noexcept {
    const Duration remaining = deadline - clock.now();
    if (remaining > spin_margin) {
        sleep_for(remaining - spin_margin);
    }
    while (clock.now() < deadline) {
        platform::cpu_relax();
    }
}

}  // namespace fastbus::core
