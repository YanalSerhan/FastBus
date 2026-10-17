# Interview prep

Three Q&As appended at the end of every milestone.

## M0 — Repo scaffold

**Q1. Why CMake presets and per-target warning flags instead of global flags?**
Presets make every configuration (compiler, sanitizer, coverage) a named, reproducible one-liner shared by developers and CI. Warning flags live on an INTERFACE target linked PRIVATE to our own targets, so third-party code is not compiled with `-Werror` and a dependency update can't break the build, while every first-party target is guaranteed the same strict set.

**Q2. How do you keep platform code out of core logic?**
The build selects exactly one backend directory (`src/platform/windows` or `src/platform/posix`) at configure time; only the platform module includes OS headers, and `scripts/check_layering.py` fails CI if a higher layer reaches around it. Core code sees only portable interfaces, so there is no `#ifdef _WIN32` in the logic.

**Q3. How do you know your sanitizer job is really checking something?**
Each sanitizer preset builds a canary program with one deliberate bug (heap overflow, signed overflow, data race). The ctest entry passes only if the sanitizer's own report text appears in the output, so a misconfigured flag or preset turns the canary red instead of silently passing.

## M1 — Platform layer + lock-free SPSC ring

**Q1. Why acquire/release and not seq_cst? Which edges matter?**
Exactly two happens-before edges make the ring correct. Producer `tail.store(t + 1, release)` → consumer `tail.load(acquire)`: the slot write happens-before the consumer reads it (no torn reads). Consumer `head.store(h + 1, release)` → producer `head.load(acquire)`: the consumer's read happens-before the producer overwrites the slot. A thread's loads of its *own* index are `relaxed`, because it is the only writer and always sees its own last store. `seq_cst` adds a single total order, which only matters for store-then-load patterns across two different variables (Dekker); the ring never makes a decision based on two variables written by different threads. On x86 a `seq_cst` store becomes `xchg` (a full barrier), while a release store is a plain `mov`. One C++20 subtlety: a release sequence is no longer extended by later plain stores of the same thread, so *every* `tail` store must be `release`, not just the first. The full proof is in `docs/memory-ordering.md`, with one tag per atomic operation that the code repeats.

**Q2. What is false sharing, and how did you prevent and verify it?**
Two cores writing different variables that sit on the same cache line make the line bounce between their caches, even though they never share data. In the ring, the producer writes `tail` on every push and the consumer writes `head` on every pop, so they must not share a line. The header is four 128-byte blocks: `tail` | producer's cached head + mask | `head` | consumer's cached tail + mask. The blocks are 128 bytes, not 64, because Intel's spatial prefetcher pulls lines in adjacent pairs and some ARM cores use 128-byte lines (ADR-009). Each side's hot path touches only its own two blocks. The cached copy of the other side's index means the other side's line is read only when the ring *looks* full or empty, not on every operation. Verification today is structural: offsets, size and alignment are `static_assert`ed and also tested. The performance verification is an M9 ablation benchmark comparing 64-byte, 128-byte and no padding, so the choice is backed by numbers.

**Q3. Why monotonic indices with power-of-two masks, and what is the overflow constraint?**
The counters only ever increase, and the slot is `counter & (capacity − 1)`. Full is `tail − head == capacity` and empty is `tail == head`: one subtraction, no separate "full" flag, and no ambiguity at the boundaries. The counters wrap modulo 2^n, and every distance is computed as `static_cast<IndexT>(tail − head)`. That is exact as long as the real distance is in `[0, 2^n)`. Occupancy takes `capacity + 1` values (0 to capacity), so capacity must be < 2^n, and as a power of two that means capacity ≤ 2^(n−1), half the index range. The default `uint64_t` never wraps in practice. The tests use `uint8_t` and `uint16_t` counters that start just before the wrap point, to exercise the overflow in a few hundred operations.

**Bonus: why does SPSC not suffer from ABA?**
ABA hits algorithms that use compare-and-swap on a value that can change and then change back (A→B→A), so the CAS wrongly succeeds. The ring has no CAS. Each index has exactly one writer, it only moves forward, and a reader never acts on "the value is the same as before". It only compares distances that are bounded by the capacity rule. So "the same value again" can only mean the counter wrapped all the way around, which is impossible while at most `capacity ≤ 2^(n−1)` items are in flight.
