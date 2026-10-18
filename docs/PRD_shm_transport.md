# PRD — Shared-memory transport

> **Status: proposed (2026-10-18), awaiting owner approval.** Sections 3–5 (platform shared memory and processes) are what M2.1/M2.2 implement; sections 6–9 (segment, slab, loans) are implemented from M2.3 and must be approved before then. Design choices are in [ADR-002](adr/ADR-002-platform-abstraction.md), [ADR-003](adr/ADR-003-fanout-zero-copy.md), [ADR-004](adr/ADR-004-wire-shm-layout.md), [ADR-005](adr/ADR-005-checksum.md).

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

## 6. Segment (M2.3)

One segment per publisher and topic: `SegmentHeader` {magic, layout_version, total_size, creator `ProcessIdentity`, state `initializing → ready → closing`, epoch}, then the subscriber table, then per-subscriber rings, then the slab (§7). All in-segment references are `OffsetPtr<T>` (offsets from the segment base, ADR-004). The creator fills everything, then `state.store(ready, release)`. Openers `acquire`-load the state and wait with a timeout.

## 7. Fan-out (ADR-003): descriptor rings + shared slab + refcounts

- **Slab:** fixed-size blocks, each a 64-byte `MessageHeader` (M2.4) plus payload. The publisher is the only allocator.
- **Per subscriber:** an SPSC *descriptor ring* from publisher to subscriber (block index + generation + sequence), and an SPSC *return ring* from subscriber to publisher.
- **Publish:** set `refcount = number of live subscribers`, then push a descriptor into each subscriber ring. If a ring is full, that subscriber's reference is dropped immediately and counted (T4).
- **Release:** a subscriber decrements the refcount. The one that brings it to 0 pushes the block index into its return ring. The publisher drains the return rings on the next `loan()`. There is no shared free list and no CAS loop, so there is no ABA.
- **Generation:** bumped on every reuse, so a stale descriptor is detected and ignored.

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

## 13. Open questions for the owner

1. Approve "creator owns lifetime" with explicit stale cleanup (§3.1)? [proposed: yes]
2. Approve the `vocabulary` exception to the layering (platform may use `Result`/`Error`/`fail_fast`, ADR-002)? [proposed: yes]
3. Approve ADR-003 (descriptor rings + slab + refcount, per Q2), ADR-004 (layout rules) and ADR-005 (CRC32C) before M2.3? [proposed: yes]
