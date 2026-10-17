# ADR-007: Injectable time

- **Status:** accepted (owner approval 2026-10-17)
- **Date:** 2026-10-16
- **Deciders:** Yanal Serhan

## Context

Several later features need control over time:

- Deterministic tests: deadline QoS (M3), reassembly timeouts (M6).
- Simulation and replay: the player drives "message time" (M5).
- EKF runs live and under replay with identical results (M7).

If modules call `std::chrono::steady_clock::now()` directly, none of this is testable or reproducible. Cross-process latency measurement (M3, M9) also needs a **system-wide** monotonic clock, so that two processes read the same timeline.

## Options

1. **Virtual interface** `Clock` with `now()`, passed by reference (`Clock&`).
2. **Template parameter / concept**: zero call overhead, but every class that uses time becomes a template.
3. **Global time hook** (a settable function pointer): easy to adopt, but hidden global state that makes parallel tests interfere.

## Decision

Option 1, with a hot-path escape hatch:

```cpp
namespace fastbus::platform {
class Clock {
public:
    virtual ~Clock() = default;
    [[nodiscard]] virtual core::Timestamp now() const noexcept = 0;
};
class SystemMonotonicClock final : public Clock;  // Windows: QueryPerformanceCounter; POSIX: clock_gettime(CLOCK_MONOTONIC)
class WallClock            final : public Clock;  // Windows: GetSystemTimePreciseAsFileTime; POSIX: CLOCK_REALTIME
class ManualClock          final : public Clock;  // tests/simulation: set()/advance(); atomic int64, safe to read from any thread
}
```

- `Timestamp` is a strong type holding nanoseconds since the clock's epoch, as `int64_t` (ADR-001 / M1.1).
- Modules **take `Clock&` in their constructor** and never call `std::chrono::*_clock::now()`. A CI grep (added to `scripts/check_layering.py` in M1.2) fails if `steady_clock`, `system_clock` or `high_resolution_clock` appears outside `src/platform/`.
- Hot loops that need a timestamp per message can use a concept-constrained template. That is decided per case and recorded in `design-decisions.md`; no such case exists in M1.
- `SystemMonotonicClock` is documented as system-wide on both OSes (QPC and `CLOCK_MONOTONIC` are), which cross-process latency relies on.

## Consequences

- ✅ Tests and simulation control time exactly; no hidden global state.
- ✅ One virtual call per `now()`, ~1–2 ns, negligible next to the clock read itself (QPC is ~10–20 ns). Measured in M9.
- ❌ Every time-aware module has a constructor dependency. This is the intended explicit wiring.
- ❌ The grep rule needs an allow-list for `src/platform/` and benchmarks.
