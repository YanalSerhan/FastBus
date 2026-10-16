# fastbus — Design plan

> Status: **draft (G0.3)** — owner review pending. Detailed per-mechanism designs live in the `PRD_<mechanism>.md` files, written just before the milestone that builds them.

## 1. C4 — Context

```mermaid
graph LR
  dev[Developer / application code] -->|publishes / subscribes| fb[fastbus]
  fb -->|shared memory| peer[Other processes on the same host]
  fb -->|UDP| remote[Processes on other hosts]
  fb -->|.fblog files| disk[(Disk)]
  ops[Operator] -->|fbtop, fbrecord, fbplay, fbclean| fb
```

## 2. C4 — Containers (deployment)

```mermaid
graph TB
  subgraph Host A
    P1[Process: publisher node] -- shm segment per topic --> S1[Process: subscriber node]
    P1 -- registry segment --- R[(fastbus.&lt;domain&gt;.registry)]
    S1 --- R
    REC[fbrecord] --- R
    REC --> LOG[(.fblog)]
  end
  subgraph Host B
    S2[Process: subscriber node]
  end
  P1 -- UDP (wire v1) --> S2
```

## 3. C4 — Components (module layering, G0.2)

```mermaid
graph TD
  platform --> core
  core --> transport
  transport --> faults
  transport --> record
  core --> idl_runtime[idl runtime/traits]
  transport --> api
  faults --> api
  idl_runtime --> api
  record --> tools
  api --> tools
  api --> ekf[modules/ekf]
  fbgen[tools/fbgen] -.generates.-> idl_runtime
```

**Layering rules** (enforced by `scripts/check_layering.py`):
- A module includes only lower layers (arrows point from dependency to dependent).
- `tools/` use `api` + `record` (+ generated messages); `examples/` and `modules/ekf` use **only** `api` + generated messages.
- Only `platform` includes OS headers; backend chosen at build time (ADR-002).

## 4. Module responsibilities

| Module | Responsibility | Milestone |
|---|---|---|
| platform | Clocks, threads, shared memory, processes, sockets, CRC32C dispatch | M1, M2, M6 |
| core | Strong types, `Result`, SPSC/MPSC rings, slab, message header | M1, M2 |
| transport | Shm segments + loans, UDP + reliability, `Transport` concept | M2, M6 |
| faults | Seeded fault-injection decorator, `SimNetwork` | M6 |
| record | `.fblog` recorder/player | M5 |
| idl (runtime) | `MessageTraits`, descriptors, schema adaptation | M4 |
| api | `Node`, `Publisher<T>`, `Subscriber<T>`, QoS, discovery, executor | M3 |
| tools | fbgen, fbtop, fbrecord, fbplay, fbbench, fbinfo, fbclean | M2–M9 |
| modules/ekf | EKF sensor fusion, separate version | M7 |

## 5. Data flow — publish path (target design, M2/M3)

```mermaid
sequenceDiagram
  participant App as Publisher app
  participant Pub as Publisher<T>
  participant Slab as Shm slab
  participant Ring as Descriptor ring (per subscriber)
  participant Sub as Subscriber<T>
  App->>Pub: loan()
  Pub->>Slab: claim block (drain return queue first)
  App->>App: fill message in place
  App->>Pub: publish(std::move(loan))
  Pub->>Ring: push descriptor (refcount = N) — release
  Sub->>Ring: pop descriptor — acquire
  Sub->>Sub: Sample<T> points into shared mapping (zero-copy)
  Sub->>Slab: on Sample destruction: refcount--, last one returns block via MPSC queue
```

## 6. Threading model

- `Publisher` objects are single-threaded; multiple publishers per topic use the MPSC side.
- Each `Node` owns one discovery thread (off the hot path) and one executor thread for callbacks.
- Polling consumers run on the caller's thread.
- No locks on the hot path; cross-process coordination uses atomics in shared memory only.

## 7. Deployment

| Resource | Naming | Lifetime |
|---|---|---|
| Registry segment | `fastbus.<domain>.registry` | creator-owned, stale reaping via pid + start time |
| Topic segments | per topic/publisher | creator-owned; `fbclean` removes stale ones |
| UDP peers | static JSON peer list | per process |
| Logs | `*.fblog` | user-managed files |
