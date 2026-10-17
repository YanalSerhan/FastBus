/// @file sleep.hpp
/// @brief Sleeping and precise waiting against an injected `Clock` (ADR-007).
#pragma once

#include "fastbus/core/clock.hpp"
#include "fastbus/core/time.hpp"

namespace fastbus::core {

/// Blocks for at least `d` of real time. Coarse: OS scheduling adds ~0.05-1 ms of overshoot.
void sleep_for(Duration d) noexcept;

/// Returns once `clock.now() >= deadline`, as close to the deadline as practical: it sleeps until
/// `spin_margin` before the deadline, then busy-waits with `cpu_relax()`. A larger margin trades
/// CPU time for precision (Windows needs ~1 ms, Linux ~100 us). With a `ManualClock`, another
/// thread must advance the clock past `deadline`, otherwise this spins forever.
/// Not for hot paths: it may block.
void precise_sleep_until(const Clock& clock, Timestamp deadline,
                         Duration spin_margin = Duration::microseconds(500)) noexcept;

}  // namespace fastbus::core
