# ADR-009: Cache-line size constants

- **Status:** accepted (owner approval 2026-10-17)
- **Date:** 2026-10-16
- **Deciders:** Yanal Serhan

## Context

The SPSC ring (M1), MPSC queue and slab (M2) need to keep data written by different threads on different cache lines, to avoid **false sharing**. They also need a fixed alignment for structures placed in shared memory. That alignment is ABI: two processes built separately must agree on it.

C++17 offers `std::hardware_destructive_interference_size`, but:

- GCC warns (`-Winterference-size`) when it is used in a header, because its value can change with `-mtune`, which silently changes struct layout, i.e. ABI. Under `-Werror` that breaks the build.
- Its value differs between compilers and targets, so shared-memory layouts could disagree between an MSVC process and a GCC process.

Modern x86 CPUs have a 64-byte line, but Intel's **spatial prefetcher pulls adjacent 64-byte lines in pairs**. Two hot fields 64 bytes apart can therefore still interfere. Many ARM64 server cores and Apple M-series use 128-byte lines.

## Options

1. `std::hardware_destructive_interference_size`: rejected for the reasons above.
2. One hard-coded constant of **64**: matches the x86 line size, but leaves adjacent-line prefetch interference.
3. One hard-coded constant of **128**: safe everywhere, but doubles padding.
4. **Two constants**: 64 for alignment, 128 for separating fields written by different threads.

## Decision

Option 4, in `include/fastbus/core/cache_line.hpp`:

```cpp
inline constexpr std::size_t kCacheLineSize = 64;            // alignment of shared structs and slots
inline constexpr std::size_t kFalseSharingRange = 128;       // spacing between fields written by different threads
static_assert(kFalseSharingRange % kCacheLineSize == 0);
```

- Both are fixed on every compiler and target. Shared-memory structs `static_assert` their size, alignment and offsets against them (ADR-004), and the layout version in each segment header (M2) changes if these constants ever change.
- The SPSC ring places `tail`, `head`, the producer's cached head and the consumer's cached tail each at the start of its own `kFalseSharingRange` block.
- M9 includes an ablation benchmark (64 vs 128 spacing vs no padding), so the choice is backed by numbers, not folklore.

## Consequences

- ✅ Identical layout across MSVC, clang-cl, GCC and Clang; no `-Winterference-size` warning.
- ✅ Covers adjacent-line prefetch on x86 and 128-byte lines on ARM64.
- ❌ About 512 bytes of header padding per ring (four 128-byte blocks). Irrelevant next to the slot array, and measured in M9.
- ❌ Values are not tuned per CPU. Accepted: ABI stability matters more than a few bytes.
