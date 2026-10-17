# PRD — Lock-free SPSC ring

> **Status: approved (2026-10-17).** Updated at the end of M1 to match what was built.

## 1. Purpose

A bounded, lock-free, wait-free FIFO queue between **exactly one producer thread and exactly one consumer thread**. It is the basic building block of fastbus:

- M1: in-process queue between two threads.
- M2: the same code placed in shared memory between two processes (the per-subscriber descriptor rings from ADR-003).
- M5: the recorder's callback → writer-thread buffer.

The ring is a **view over caller-provided memory**. It never allocates and holds no pointers inside that memory, so the bytes can live in shared memory unchanged.

## 2. Guarantees

| # | Guarantee |
|---|---|
| G1 | **FIFO:** items are popped in the order they were pushed. |
| G2 | **No loss, no duplication:** every successfully pushed item is popped exactly once. |
| G3 | **No torn reads:** a consumer never observes a partially written slot (the slot write happens-before the consumer's read; see `docs/memory-ordering.md`). |
| G4 | **Wait-free:** every operation finishes in a bounded number of steps. No locks, no CAS loops, no syscalls, no allocation, no exceptions. |
| G5 | **Non-blocking:** push on a full ring and pop on an empty ring return immediately with "no". |
| G6 | **Bounded state:** `0 ≤ tail − head ≤ capacity` at every moment, computed in modular `IndexT` arithmetic. |

## 3. Invariants and representation

- `capacity` is a power of two, `2 ≤ capacity ≤ 2^(bits(IndexT) − 1)`. The upper bound keeps `tail − head` unambiguous after the counters wrap.
- `head` (next slot to read) is written only by the consumer; `tail` (next slot to write) only by the producer. Both increase monotonically and wrap modulo `2^bits(IndexT)`.
- Slot index = `counter & (capacity − 1)`.
- Slot ownership: slots in `[head, tail)` belong to the consumer, all others to the producer. Ownership moves only by publishing `tail` (producer → consumer) or `head` (consumer → producer).
- Each side keeps a cached copy of the other side's index (`cached_head` on the producer side, `cached_tail` on the consumer side) and reloads it only when the cached value says full or empty. A stale cache can only make the ring look fuller or emptier than it is, never wrong (proof in `docs/memory-ordering.md`).
- `head` and `tail` sit in separate 128-byte blocks, as do the two cached copies (ADR-009), to avoid false sharing.

## 4. Element type requirements

`T` must satisfy `std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T>`, enforced by a concept (`RingElement`). This keeps the code valid when the ring lives in shared memory in M2.

## 5. API (sketch)

```cpp
namespace fastbus::core {

template <RingElement T, std::unsigned_integral IndexT = std::uint64_t>
class SpscRingView {
public:
    // Setup (not hot path): validates and initializes the layout inside `memory`.
    static Result<SpscRingView> create(std::span<std::byte> memory, std::size_t capacity) noexcept;
    static constexpr std::size_t required_bytes(std::size_t capacity) noexcept;
    static constexpr std::size_t required_alignment() noexcept;   // kCacheLineSize

    // Producer side — call from the producer thread only.
    [[nodiscard]] bool try_push(const T& value) noexcept;         // false = full
    [[nodiscard]] T*   try_claim() noexcept;                       // nullptr = full
    void               commit() noexcept;                          // publishes the claimed slot

    // Consumer side — call from the consumer thread only.
    [[nodiscard]] bool     try_pop(T& out) noexcept;               // false = empty
    [[nodiscard]] const T* try_peek() noexcept;                    // nullptr = empty
    void                   release() noexcept;                     // frees the peeked slot

    // Any thread; approximate under concurrency, exact when quiescent.
    [[nodiscard]] std::size_t size_approx() const noexcept;
    [[nodiscard]] bool        empty_approx() const noexcept;
    [[nodiscard]] std::size_t capacity() const noexcept;
};

// Owning convenience wrapper: one aligned allocation at construction, none afterwards.
template <RingElement T, std::unsigned_integral IndexT = std::uint64_t>
class SpscRing;   // static Result<SpscRing> create(std::size_t capacity)

}  // namespace fastbus::core
```

**Claim/commit and peek/release** are the zero-copy hooks for M2 loans. At most one claim and one peek can be outstanding. Calling `try_claim()` again before `commit()` returns the same slot. `commit()` without a claim, and `release()` without a peek, are precondition violations: they assert in debug builds and are undefined in release builds.

**Thread-safety contract:** one producer thread and one consumer thread, which may be the same thread. Debug builds record the thread id of the first producer and consumer calls and assert on misuse. Release builds have no checks (zero overhead).

**Errors:** only `create` can fail, through `Result` (ADR-001): `Error::InvalidCapacity` (zero, not a power of two, or too large for `IndexT`), `Error::BufferTooSmall`, `Error::Misaligned`. Hot-path operations report full/empty through `bool`/`nullptr`, never through errors.

## 6. Building-block contract

| | |
|---|---|
| **Input data** | Values of `T` (trivially copyable, standard layout), from the producer thread. |
| **Output data** | The same values, in FIFO order, to the consumer thread. |
| **Setup data** | A memory region (`size ≥ required_bytes(capacity)`, aligned to `required_alignment()`) and a capacity (power of two). Nothing global, no configuration files. |

## 7. Edge cases (each has a test)

Empty ring: pop and peek fail. Full ring: push and claim fail, and the ring is unchanged. Capacity 2 (the smallest). Capacity at the `IndexT` maximum. Counter wrap with `uint8_t` and `uint16_t` indices, including starting near the wrap point. Claim twice before commit. Peek twice before release. Interleaved push/claim and pop/peek. `size_approx` stays within `[0, capacity]` under concurrency. `create` rejects 0, 3, a non-power-of-two capacity, a too-small buffer and a misaligned buffer.

## 8. Performance hypotheses (measured in M1.9 / M9, not gated in M1)

| Metric | Hypothesis |
|---|---|
| Single-thread push + pop, 8-byte `T` | < 5 ns |
| Two threads on different cores, 8-byte `T` | > 50 M items/s |
| Versus a `std::mutex + std::deque` baseline | > 5× throughput |
| Heap allocations after `create` | exactly 0 (tested) |

## 9. Design choices (details in ADRs and `docs/memory-ordering.md`)

- **Monotonic counters with masking** rather than wrapped indices plus a "full" flag: one comparison for full and empty, and no ABA problem (a single writer per index).
- **acquire/release only**, no `seq_cst` and no standalone fences: the happens-before edges needed are exactly producer `tail.store(release)` → consumer `tail.load(acquire)`, and consumer `head.store(release)` → producer `head.load(acquire)`. Loads of a side's own index are `relaxed`. TSan does not model standalone fences reliably, which is another reason not to use them.
- **Cached indices in the shared layout**, on their own spacing blocks. The alternative, keeping them in per-side view objects, is revisited in M2 when each process has its own view.

## 10. Non-goals

Blocking or timed waits, multiple producers or consumers (MPSC is M2), dynamic resizing, non-trivially-copyable `T`, cross-process use (M2), wake-ups or notifications.

## 11. Test plan

Unit and edge-case tests (M1.6); stress tests with 1e7 items normally, reduced under sanitizers, using patterned 64-byte payloads and capacities {2, 64, 4096} with random `cpu_relax`/yield and slow-producer and slow-consumer variants (M1.7); ASan, UBSan and TSan in CI (M1.8); a counting-allocator binary asserting zero allocations (M1.9); coverage ≥ 85 % for `core` + `platform` (M1.8).
