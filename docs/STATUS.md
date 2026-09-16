# Project Status — Hot Rod C++17 Client

> **This is the single source of truth for "where the project is."**
> If any other doc disagrees with this file, this file wins. Point-in-time
> snapshots live in [`archive/`](archive/) and are historical only.
>
> **Last updated:** 2026-09-16

---

## ▶ Coming back after a break? Do this first

1. `git pull` (if working across machines)
2. `git log --oneline -5` — see what the last session actually did
3. Build: `cmake --build build`  (configure first if needed: `cmake -S . -B build`)
4. Test: `ctest --test-dir build --output-on-failure`
5. Read **⏭ Next steps** below, pick one, go.

Then **before you stop**: update *⏭ Next steps* and the *Last updated* date
while the context is still fresh in your head. This 60-second habit is what
keeps this file trustworthy across long gaps.

---

## ⏭ Next steps (start here)

_The 1–3 concrete things to do next. Keep this short and current._

1. **Confirm the suite is green on this machine** — run the build + `ctest`
   above; the numbers below come from the README, not a run this session.
2. **Step 10 — Metadata operations** (version-based ops): the only remaining
   feature gap toward core parity with the Java client. `EntryMetadata` /
   `EntryWithMetadata` types exist; the operations do not.
3. **Benchmark the multiplexing path** — the async rewrite targets 5–10×
   concurrent throughput; this has not been measured yet.

---

## Current state (verified against code, 2026-09-16)

**Working and shipped:**
- Wire primitives (vInt, vLong, strings, byte arrays)
- Protocol 4.0 headers (all conditional fields)
- SCRAM-SHA-256 authentication (RFC 5802, OpenSSL)
- Cluster topology awareness (failover, load balancing)
- Consistent hashing (MurmurHash3 x64_32, seed 9001)
- Hash-aware routing → primary owner, with automatic failover
- Connection pooling (one connection per server)
- **Full CRUD:** PING / GET / PUT / REMOVE
- **Async API:** all operations return `std::future` / `std::optional`
- **MultiplexedConnection:** true async — dedicated read-loop thread,
  `messageId → promise` pending map, `execute()` used by all four operations.
  _(The July "70% / temporary blocking" docs are obsolete — see archive note.)_
- Multi-server topology test fixtures + Docker-based integration tests

**Test status (verified by full run, 2026-09-16):**
- Unit: **145/145** passing (`./build/unit_tests`, <1s)
- Integration: **46/46** passing across 8 suites (`ctest`, ~287s, spins up
  Docker Infinispan single-server + multi-node clusters)
- Full run: `ctest --test-dir build --output-on-failure` → 100% pass (9/9 ctest targets)

**Not started / open:**
- Step 10 — Metadata / version-based operations
- Performance benchmarks for the multiplexing path

---

## Where things live

| What | Where |
|------|-------|
| Public API | `include/hotrod/RemoteCache.h` |
| Async transport core | `src/transport/MultiplexedConnection.cpp` |
| Operations (PING/GET/PUT/REMOVE) | `src/operations/RemoteCache.cpp` |
| Codecs | `src/codec/`, `src/hash/`, `src/auth/`, `src/topology/` |
| Unit tests | `tests/unit/` |
| Integration tests | `tests/integration/` (see `README_TOPOLOGY.md` there) |
| Why decisions were made | [`DECISIONS.md`](DECISIONS.md) |
| Historical milestone log | [`../PROGRESS.md`](../PROGRESS.md) |
| Superseded snapshots | [`archive/`](archive/) |

---

## How this project's docs work (the process)

Kept deliberately small so intermittent work stays cheap to resume:

- **`docs/STATUS.md`** (this file) — the *only* living status doc. Update the
  *Next steps* + date at the end of every session.
- **`docs/DECISIONS.md`** — append-only. When you make a design decision you'd
  otherwise forget the reasoning for, add an entry. Never rewrite old ones.
- **`docs/archive/`** — frozen point-in-time snapshots. Read for history, never
  trust for current state.
- **`README.md`** — user-facing overview and quick start.
- **`PROGRESS.md`** — historical step-by-step milestone log (accurate through
  the CRUD steps; predates the async rewrite).

Rule of thumb: if a fact about the project changes, it changes *here* — not in
a new dated doc.
