# ADR-003: Fan-out / zero-copy design

- **Status:** accepted (2026-10-18; owner delegated the decision to the proposed option); matches the G0 answer to Q2
- **Date:** 2026-10-18
- **Deciders:** Yanal Serhan

## Context

One publisher, N subscribers in other processes. Publishing must be zero-copy (the subscriber reads the bytes the publisher wrote), allocation-free after setup, and must never block the publisher on a slow or dead subscriber. Processes can crash at any instruction.

## Options

(a) **Descriptor rings + shared slab + refcounts:** the payload is written once into a block of a publisher-owned slab; each subscriber gets a small descriptor (block index, generation, sequence) through its own SPSC ring; a per-block refcount says when the block can be reused.
(b) **Full-message rings:** each subscriber ring carries complete messages; the publisher writes every message N times.
(c) **One shared broadcast ring:** a single ring that all subscribers read with private cursors; overwrites when the slowest is too slow (seqlock-style).

## Decision

**(a).** Details:
- Slab: fixed-size blocks (`MessageHeader` + payload), sized at segment creation. The publisher is the **only allocator**.
- Per subscriber: one SPSC descriptor ring (publisher → subscriber) and one SPSC **return ring** (subscriber → publisher). These are the M1 ring, unchanged.
- Publish: `refcount = live subscribers`, then push a descriptor into each ring. If a ring is full, that subscriber's reference is released immediately and a drop counter is incremented. The publisher never waits.
- Release: subscriber does `refcount.fetch_sub(1, acq_rel)`. Whoever reaches 0 pushes the block index into its return ring. The publisher drains the return rings at `loan()`. There is no shared free list and no CAS loop, so there is no ABA.
- Generation per block, bumped on reuse: a stale or corrupt descriptor is detected and ignored.
- Dead subscriber (pid + start time): the publisher releases the references still in that subscriber's ring, plus its outstanding sample, then frees the subscriber slot.

## Consequences

- ✅ True zero-copy for any number of subscribers; publish cost grows with N only by one small descriptor push per subscriber.
- ✅ All cross-process queues are the proven SPSC ring; the only new atomic is the refcount.
- ✅ A slow subscriber only loses its own messages (counted); others are unaffected.
- ❌ Fixed block size per segment: payloads larger than a block need a larger segment (configured per topic in M3).
- ❌ Refcount and dead-subscriber reclamation are the subtle parts; they get their own section in `memory-ordering.md` (M2.5) and crash tests (M2.8).
- ❌ Option (b) would be simpler for one subscriber and is kept in mind as a benchmark baseline (M9).
