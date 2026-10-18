# PRD — MPSC queue

> **Status: proposed (2026-10-18), awaiting owner approval.** Implemented in M2.6. The design decision is recorded as a new ADR at M2.6; this PRD states the options and the proposed choice so it can be reviewed now.

## 1. Purpose

A bounded queue from **many producers to one consumer**, valid in-process and in shared memory. Users: the slab's block-return path (subscribers → publisher, M2.5), multi-publisher topics, and control messages to a single owner (M3). Requirement from the TODO acceptance criteria: passes multi-producer stress under TSan (in-process threads) and cross-process with 3 producer processes (M2.8).

## 2. Options

| | (A) Vyukov bounded MPMC/MPSC array | (B) Per-producer SPSC rings, consumer merges |
|---|---|---|
| Mechanism | one array; each slot has a sequence number; producers CAS the tail | N independent `SpscRingView`s (M1); the consumer polls them round-robin |
| Producer cost | CAS loop on a contended cache line | no CAS; same cost as SPSC push |
| Ordering | global order = order of successful claims | FIFO **per producer**; no global order between producers |
| **Producer crashes after claiming a slot** | **head-of-line blocking: the consumer waits forever** on that slot (needs timeout + skip marker, which is hard to get right) | only that producer's ring stops; the others are unaffected |
| Memory | one array of `capacity` | `max_producers × capacity_per_producer` |
| Producers | unbounded number | fixed maximum, set at creation; producers register for a ring (pid + start time, so a dead producer's ring can be reclaimed) |
| Reuses proven code | no (new ordering proof) | yes: M1 ring + its ordering proof; only registration is new |

## 3. Proposed decision: (B) per-producer SPSC rings

In shared memory, processes crash. Option A turns a crashed producer into a stall for everyone, and its mitigation (timeouts + skip markers on a CAS protocol) is the riskiest code in the project. Option B isolates failures, adds no new lock-free algorithm to prove, and matches the slab design: each subscriber already needs its own return path, which is exactly one SPSC ring. The costs are a fixed producer limit and no cross-producer ordering. No user in M2–M8 needs global ordering; topics guarantee per-publisher order (PRD_shm_transport T3).

## 4. Guarantees (for option B)

| # | Guarantee |
|---|---|
| M1 | FIFO per producer; no loss or duplication. |
| M2 | Producers never block each other; a producer push is wait-free. |
| M3 | A crashed producer affects only its own ring; the consumer can detect it (registration identity) and reclaim the ring. |
| M4 | Fair consumption: round-robin across non-empty rings, bounded batch per ring, so one busy producer cannot starve the others. |
| M5 | No allocation after setup; view over caller memory; no pointers in the memory. |

## 5. API sketch

```cpp
template <RingElement T, RingIndex IndexT = std::uint64_t>
class MpscQueueView {
public:
    static Result<MpscQueueView> create(std::span<std::byte> memory, std::size_t max_producers,
                                        std::size_t capacity_per_producer) noexcept;
    Result<ProducerHandle> register_producer(platform::ProcessIdentity who) noexcept;  // setup
    [[nodiscard]] bool try_push(ProducerHandle, const T&) noexcept;                     // producer
    [[nodiscard]] bool try_pop(T& out) noexcept;                                        // consumer
    std::size_t reclaim_dead_producers() noexcept;                                      // consumer
};
```

## 6. Edge cases

Capacity 2 per producer with 8 producers (high contention on the consumer side), all producers full, a producer registering after others have pushed, a producer slot reused after a crash (stale items in its ring are discarded or drained, as decided in the ADR), `max_producers` = 1 (degenerates to SPSC).

## 7. Test plan

Unit tests (capacity, wrap, full, registration limits), multi-producer stress with 4–8 producer threads checking per-producer sequences (no loss or duplication), TSan run, small-capacity contention test, cross-process test with 3 producer processes (M2.8), producer crash test.

## 8. Non-goals

Global ordering across producers, unbounded producer count, multiple consumers, blocking waits.

## 9. Open question for the owner

Approve option (B)? If global ordering is wanted after all, option A comes back with an explicit crash policy and its own memory-ordering section.
