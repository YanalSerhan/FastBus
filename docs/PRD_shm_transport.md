# PRD — Shared-memory transport

> **Status: approved (2026-10-18; the owner delegated the open questions to the proposed answers).** Sections 3–5: M2.1/M2.2. Section 6: M2.3. Section 6a: M2.4. Section 7: M2.5. Sections 8–9: M2.7/M2.8. Design choices are in [ADR-002](adr/ADR-002-platform-abstraction.md), [ADR-003](adr/ADR-003-fanout-zero-copy.md), [ADR-004](adr/ADR-004-wire-shm-layout.md), [ADR-005](adr/ADR-005-checksum.md).

## 1. Purpose

Move messages between **processes on one machine** without copying the payload and without allocating on the hot path. A publisher process writes a message once into shared memory; every subscriber process reads it in place. This is the transport behind the M3 pub/sub API, the recorder (M5) and the benchmarks (M9).

## 2. Guarantees

| # | Guarantee |
|---|---|
| T1 | **Zero-copy:** a subscriber's `Sample<T>` points into its own mapping of the publisher's segment; the payload is never copied by fastbus (a convenience `publish(const T&)` copies once, by request). |
| T2 | **No torn messages:** a subscriber only ever sees messages whose write completed (publication is the SPSC ring's release store, `memory-ordering.md`). A publisher crash in the middle of a write exposes nothing. |
| T3 | **Per-publisher FIFO** per subscriber; sequence numbers have no gaps unless messages were dropped, and drops are counted. |
| T4 | **Publisher never blocks** on a slow or dead subscriber (it drops for that subscriber and counts; QoS refinements in M3). |
| T5 | **No allocation after setup** on publish, loan, take or release. |
| T6 | **Untrusted memory:** every value read from shared memory is validated before use; a corrupted segment yields an error, never a crash. |
| T7 | **Crash recovery:** a segment whose creator died is detected (pid + process start time) and reclaimed explicitly; a dead subscriber's blocks are reclaimed by the publisher. |

## 3. Platform shared memory (M2.1): `platform::SharedMemory`

```cpp
namespace fastbus::platform {
class SharedMemory {                       // move-only RAII
public:
    static core::Result<SharedMemory> create(std::string_view name, std::size_t size) noexcept;
    static core::Result<SharedMemory> open(std::string_view name) noexcept;
    static core::Result<void> remove(std::string_view name) noexcept;   // stale cleanup (fbclean)

    std::byte* data() const noexcept;      // page-aligned (>= 4 KiB; 64 KiB on Windows)
    std::size_t size() const noexcept;     // requested size rounded up to the page size
    bool is_creator() const noexcept;
    std::string_view name() const noexcept;
};
[[nodiscard]] bool is_valid_shm_name(std::string_view name) noexcept;
[[nodiscard]] std::size_t page_size() noexcept;
}
```

- **Names:** 1–200 characters from `[A-Za-z0-9._-]`, chosen by fastbus (e.g. `fastbus.demo.topic.1234`). One shared function validates and maps them: POSIX `"/fastbus_" + name`, Windows `L"Local\\fastbus_" + name` (the `Global\` namespace needs a privilege).
- **Memory is committed at creation** (`posix_fallocate` / `SEC_COMMIT`): on a size-limited `/dev/shm` (containers, CI) a too-large segment fails immediately with `OutOfMemory` instead of crashing later with SIGBUS on first touch (KI-011).
- **Size:** `create` rounds up to the page size, so creator and opener report the same `size()` on both OSes (Windows cannot report the exact size of an existing mapping, only whole pages). The exact byte count lives in the segment header (M2.3).
- **Errors:** `InvalidArgument` (bad name, size 0), `AlreadyExists` (create on an existing name), `NotFound` (open/remove of a missing name), `PermissionDenied`, `OutOfMemory`, `SystemError`.
- **Creation is exclusive:** POSIX `shm_open(O_CREAT | O_EXCL, 0600)`; Windows `CreateFileMappingW` + `ERROR_ALREADY_EXISTS` → the extra handle is closed and `AlreadyExists` returned. Two creators can never share a segment by accident.

### 3.1 Lifetime: the platform gap and fastbus semantics

| | POSIX (`shm_open`) | Windows (`CreateFileMapping`) |
|---|---|---|
| Object lives until | `shm_unlink` **and** last unmap | last handle closes |
| Creator crashes | object **persists** in `/dev/shm` (stale) | object dies with the last process holding it |
| Name after creator exits normally | gone once unlinked | still openable while any opener holds a handle |

**fastbus semantics: the creator owns the lifetime.**
- The creator's destructor unlinks the name (POSIX) and unmaps. Openers keep a valid mapping until they close it, but new `open` calls fail on POSIX.
- On Windows a new `open` can still succeed while another process holds a handle. The segment protocol (M2.3) therefore marks the header `closing` before the creator unmaps, and openers treat a `closing` segment as gone. Behaviour is identical above the platform layer.
- Stale cleanup is **explicit**. A creator crash on POSIX leaves the name behind. The next creator gets `AlreadyExists`, checks the stale header's creator identity (§4), and removes it with `SharedMemory::remove`, or `fbclean` does it (M2.9). Nothing is deleted silently.
- `remove` on Windows returns `NotSupported`: there is nothing to delete, because the object disappears with its last handle.

## 4. Platform processes (M2.2): `platform::Process` and `ProcessIdentity`

```cpp
struct ProcessIdentity { std::uint64_t pid; std::uint64_t start_time; };  // trivially copyable, stored in shm
ProcessIdentity current_process() noexcept;
core::Result<ProcessIdentity> identity_of(std::uint64_t pid) noexcept;
bool is_alive(const ProcessIdentity& who) noexcept;     // same pid AND same start time, not a zombie

class Process {                                        // move-only RAII child process
public:
    static core::Result<Process> spawn(std::string_view path, std::span<const std::string_view> args) noexcept;
    core::Result<int> wait(std::chrono::milliseconds timeout) noexcept;  // exit code; Timeout if still running
    core::Result<void> kill() noexcept;                // SIGKILL / TerminateProcess; exit code 137
    bool running() noexcept;
    std::uint64_t pid() const noexcept;
    ~Process();                                        // kills and reaps a still-running child (no orphans)
};
```

- **PID reuse:** an OS can hand a dead process's pid to a new process. Liveness therefore compares the **start time** too: Linux `/proc/<pid>/stat` field 22 (clock ticks since boot), Windows `GetProcessTimes` creation time. A zombie (exited, not yet reaped) counts as dead.
- **Exit codes:** normal exit → its code; killed by signal *n* (POSIX) → `128 + n`; `kill()` → 137 on both OSes.
- **Spawn:** POSIX `posix_spawn` (safe in multi-threaded processes, unlike `fork`); Windows `CreateProcessW` with arguments quoted by the `CommandLineToArgvW` rules, UTF-8 converted to UTF-16.
- **Test helper:** `tests/helpers/fb_test_child` runs roles chosen by argv (`exit`, `sleep`, `hang`, `shm-write`, `shm-check`, `print-identity`). It has a built-in watchdog (`--watchdog-ms`, default 30 s) that exits with code 124, so a hung child can never hang CI.

## 5. Building-block contract (platform pieces)

| | SharedMemory | Process |
|---|---|---|
| Input data | name, size | executable path, argv |
| Output data | a mapped, page-aligned byte range | exit code, liveness |
| Setup data | none (no files, no environment) | none |

## 6. Segment (M2.3): `transport::Segment`

A segment is a `SharedMemory` whose first 128 bytes are a `SegmentHeader` (ADR-004 rules: fixed-width fields, zeroed reserved bytes, `static_assert`ed offsets):

| Offset | Field | Meaning |
|---|---|---|
| 0 | `u64 magic` | `"FBSEGMNT"`; anything else → `Corrupted` |
| 8 | `u16 layout_version`, `u16 header_size`, `u32 kind` | version 1; 128; topic = 1, test = 0xFFFF |
| 16 | `u64 total_size` | exact bytes in use (≤ mapping size) |
| 24 | `u64 payload_offset`, `u64 payload_size` | where the kind-specific content lives |
| 40 | `ProcessIdentity creator` | pid + start time (stale detection) |
| 56 | `u64 epoch` | wall-clock ns at creation; distinguishes two lives of one name |
| 64 | `atomic<u32> state` | `initializing (0) → ready (1) → closing (2)` |
| 68–127 | reserved | zero |

- **Create:** map, fill every field while `state == initializing` (memory starts zeroed), then `state.store(ready, release)`. A reader that `acquire`-loads `ready` sees every field (and every byte of payload set up before it).
- **Open:** map, then validate everything a reader will rely on *before* using it: magic, version, header size, `total_size ≤ mapping size`, `payload_offset + payload_size ≤ total_size`, alignment. Then wait (polling, with a timeout) for `ready`. `closing` → `NotFound`. Shared memory is untrusted: a failed check is an error, never a crash (T6).
- **Stale creator:** `create` on an existing name opens it and checks `is_alive(header.creator)`. Dead → the segment is stale: removed and created again (POSIX). Alive → `AlreadyExists`. On Windows a stale name can only exist while another process still holds a handle; it cannot be removed, so `create` reports `AlreadyExists` with a "stale, still mapped elsewhere" message. `open` refuses a segment whose creator is dead (`NotFound`).
- **Destroy (creator):** `state.store(closing, release)` first, then unmap/unlink. Openers that see `closing` treat the segment as gone (this is what makes Windows behave like POSIX, §3.1).
- `OffsetPtr<T>` (core): an offset from the segment base, resolved only through a bounds- and alignment-checked function that returns `nullptr` on any violation.

## 6a. Message header (M2.4): `core::MessageHeader`

64 bytes, `alignas(64)`, first in every slab block's payload, on the wire (M6) and in logs (M5):

| Offset | Field | Notes |
|---|---|---|
| 0 | `u32 magic` `"FBMS"` · `u16 header_version` (1) · `u16 schema_version` | |
| 8 | `u64 type_id` | FNV-1a 64 of the fully qualified type name (`constexpr`) |
| 16 | `u64 sequence` · 24 `i64 timestamp_ns` | assigned at publish |
| 32 | `u32 payload_size` · 36 `u32 checksum` | CRC32C, valid only with the `checksummed` flag |
| 40 | `u16 flags` (`replayed` = 1, `checksummed` = 2) | other bits must be zero |
| 42–63 | reserved | always zeroed by `make_header` |

Checksum (ADR-005): CRC32C over the 64 header bytes **with `checksum` set to 0**, continued over the payload. Hardware CRC32C (SSE4.2 / ARMv8 CRC) when available, software slice-by-8 otherwise; both give identical results (tested). Validation (`validate(header, bytes_available)`): magic, version, unknown flag bits, `payload_size ≤ bytes_available − 64`.

## 7. Slab (M2.5): `core::SlabView` — descriptor rings + refcounts (ADR-003)

A view over caller memory (the segment payload in M2.7) with no pointers inside:

```
[ SlabHeader 64 B | free stack u32[block_count] (publisher only) | block 0 | block 1 | ... ]
block = [ BlockHeader 64 B: atomic<u64> state = (generation << 32) | refcount | payload ]
```

- **The publisher is the only allocator.** `allocate()` pops the free stack (no CAS, no ABA) and returns a `BlockHandle {index, generation}`; empty → `PoolExhausted` (never blocks).
- `share(handle, n)` sets `refcount = n` (n = live subscribers) before the descriptors are pushed; `free(handle)` returns an unpublished block (a loan dropped without publish).
- **Release (any process):** a CAS loop on the packed 64-bit state that succeeds only if the generation matches *and* refcount > 0. Generation and refcount are checked in the **same** atomic operation, so a stale or duplicated descriptor can never decrement a block that has since been reused (the race a separate generation check would have). Results: `Stale`, `Remaining`, or `Last`.
- **Return path:** the subscriber that gets `Last` pushes the block index into its own SPSC return ring (MPSC option B); the publisher drains return rings and calls `reclaim(index)`, which bumps the generation and pushes the index back on the free stack.
- Memory ordering (full argument in `memory-ordering.md` §13): each subscriber's reads of the payload happen-before its release CAS (`acq_rel`); the CASes on one block form a release sequence, so the `Last` releaser's acquire synchronizes with all of them; its return-ring push (`release`) → publisher pop (`acquire`) → reuse. The publisher's payload writes reach subscribers through the descriptor ring's release/acquire (M1 proof).
- **Block states** (low 32 bits of `state`; the generation is bumped on every return to `FREE`):

  ```
  FREE --allocate--> LOANED --share(n)--> SHARED(n..1) --last release--> RETURNED --reclaim--> FREE
  LOANED --free (loan dropped without publish)--> FREE
  ```
  Every transition checks the state it starts from. So a corrupted return ring or a buggy peer cannot double-free (`reclaim` of a `FREE` block is rejected) or steal a loan (`reclaim` of a `LOANED` block is rejected). Either case would otherwise hand one block to two owners.
- Block size: `BlockHeader` + payload capacity rounded up to 64 bytes (ADR-009), so refcounts of neighbouring blocks never share a cache line with each other's payload start.

## 8. Loans (M2.7)

`Loan<T> loan()` → fill in place → `publish(std::move(loan))`. Destroying a loan without publishing returns the block (no message). `Sample<T> take()` gives read-only access and releases on destruction. Sequence numbers are assigned at publish; timestamps come from the injected `Clock&` (ADR-007). Payloads are zero-filled by default (determinism for M5); this can be switched off and is measured in M9 (known-issues entry when M2.7 lands).

## 9. Edge cases (each gets a test)

Name: empty, too long, invalid characters, `/` inside. Size 0, not a multiple of the page size, very large (`OutOfMemory`). Create twice, open missing, open after the creator is destroyed, remove missing. Two mappings of one segment in one process see each other's writes; a child process sees the parent's writes and vice versa. Spawn a missing executable; wait with timeout on a running child; kill; a zombie counts as dead; a pid with the wrong start time counts as dead; destroying a running `Process` kills it. Later: subscriber ring full, all subscribers dead, crash mid-write, stale segment, corrupted header (fuzz), late joiner, 1 MB payload.

## 10. Performance hypotheses (measured in M9)

| Metric | Hypothesis |
|---|---|
| Cross-process latency, 64 B, p50 / p99 | < 1 µs / < 5 µs |
| Throughput, 64 B, 1 → 1 | > 10 M msgs/s |
| Allocations after setup | 0 |

## 11. Non-goals

Network transport (M6), cross-machine use, encryption, persistence beyond the creator's lifetime, blocking waits and wake-ups (polling only in M2; wait strategies in M3), macOS.

## 12. Test plan

M2.1/M2.2 unit tests (`tests/unit/shared_memory_test.cpp`, `process_test.cpp`) including real cross-process checks through `fb_test_child`; M2.8 cross-process integration suite (ping-pong, 1e6 messages, zero-copy proof, crash and stale tests, corrupted-segment fuzz, late joiner, MPSC with 3 producer processes) on Windows and Linux CI.

## 13. Decisions (were open questions)

All four were decided on 2026-10-18 by delegation to the proposed answers: creator owns lifetime; the `vocabulary` layer; ADR-003/004/005 accepted; MPSC option (B).

<details><summary>Original questions</summary>


1. Approve "creator owns lifetime" with explicit stale cleanup (§3.1)? [proposed: yes]
2. Approve the `vocabulary` exception to the layering (platform may use `Result`/`Error`/`fail_fast`, ADR-002)? [proposed: yes]
3. Approve ADR-003 (descriptor rings + slab + refcount, per Q2), ADR-004 (layout rules) and ADR-005 (CRC32C) before M2.3? [proposed: yes]

</details>
