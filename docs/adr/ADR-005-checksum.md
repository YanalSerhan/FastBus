# ADR-005: Checksum algorithm and policy

- **Status:** proposed (2026-10-18), awaiting owner approval before M2.4
- **Date:** 2026-10-18
- **Deciders:** Yanal Serhan

## Context

Messages need an integrity check where corruption is plausible: UDP (M6), log files (M5), and optionally shared memory (to catch buggy peers). The check must be fast enough for the hot path and identical on every platform.

## Options

(a) **CRC32C** (Castagnoli): hardware instructions on x86-64 (SSE4.2 `crc32`) and ARMv8 (`crc32c*`), a software table fallback, standard check value `0xE3069283` for `"123456789"`. (b) xxHash: very fast, but no hardware path and not an error-detecting code with known Hamming-distance properties. (c) No checksum.

## Decision

**(a) CRC32C** in `platform` (it is CPU-specific): a slice-by-8 software table plus a hardware path, selected once at runtime (CPUID on x86, `getauxval`/IsProcessorFeaturePresent on ARM) and exposed as one function `crc32c(std::span<const std::byte>, std::uint32_t seed)`.

Policy:
- **Mandatory** on UDP packets and log records.
- **Optional on shared memory, default off**, enabled per topic; the `checksummed` flag in `MessageHeader` says whether `checksum` is valid. The cost is measured in M9.
- Tested against the standard check value and against the software path for random inputs (hardware ≡ software).

## Consequences

- ✅ About 1 cycle per 8 bytes with hardware support; the same result everywhere.
- ✅ CRC32C detects all burst errors up to 32 bits; it is what iSCSI, ext4 and many RPC systems use.
- ❌ Not cryptographic: it detects accidents, not attacks (out of scope).
- ❌ Runtime dispatch adds one indirect call per checksum (negligible next to the data).
