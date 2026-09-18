# Project Status — Hot Rod C++17 Client

> **This is the single source of truth for "where the project is."**
> If any other doc disagrees with this file, this file wins. Point-in-time
> snapshots live in [`archive/`](archive/) and are historical only.
>
> **Last updated:** 2026-09-19

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

_(Full per-session workflow: [`WORKFLOW.md`](WORKFLOW.md).)_

---

## ⏭ Next steps (start here)

_The 1–3 concrete things to do next. Keep this short and current._

1. **Step 10 — Metadata operations, continued.** `getWithMetadata` (0x1B/0x1C)
   and `removeWithVersion` (0x0D/0x0E) are now **shipped** (see "Working and
   shipped"). Next in the recommended order: **`replaceWithVersion`**
   (0x09/0x0A), then the conditionals `putIfAbsent`/`replace`/`containsKey`.
   Version comes from `getWithMetadata().get()->metadata.version`;
   `replaceWithVersion` also carries lifespan/maxIdle + value in the request.
2. **Benchmark the multiplexing path** — the async rewrite targets 5–10×
   concurrent throughput; this has not been measured yet.
3. **Small cleanup:** read header "other params" when `paramCount > 0`
   (two `TODO`s in `src/operations/RemoteCache.cpp:140,182`).

---

## 📋 Backlog (the whole list)

_Everything known-to-do, in one place. `⏭ Next steps` above is just the 1–3 you
pull from here next. Step numbers follow
[`../hotrod-foundry/ROADMAP.md`](../../hotrod-foundry/ROADMAP.md); `PROGRESS.md`
is authoritative for what's done._

**Remaining roadmap steps:**
- **Step 10 — Metadata operations** (version-based CAS). Deliverables:
  - [x] `getWithMetadata` (0x1B/0x1C) — **shipped 2026-09-17** (8 unit + 7 integration tests)
  - [x] `removeWithVersion` (0x0D/0x0E) — **shipped 2026-09-19** (7 unit + 5 integration tests). Returns `future<bool>` (matches Java `boolean removeWithVersion(K, long)`); added `Codec::writeLong`/`readLong` (fixed 8-byte BE). Parser drains the `*_WITH_PREVIOUS` (0x03/0x04) body defensively though FORCE_RETURN_VALUE isn't wired yet. Byte layout cross-checked against all three legs: this client, Java `RemoveIfUnmodifiedOperation`, and `hotrod40.ksy` (`remove_if_unmodified_request` = `key: lp_bytes` + `entry_version: s8`, big-endian per `meta.endian: be`).
  - [ ] `replaceWithVersion`/REPLACE_IF_UNMODIFIED (0x09/0x0A)
  - [ ] `putIfAbsent` (0x05/0x06), `replace` (0x07/0x08), `containsKey` (0x0F/0x10)

  Java ref: `GetWithMetadataOperation`, `ReplaceIfUnmodifiedOperation`. See the
  [step-by-step plan](#plan-metadata-operations-step-10) below.
- [ ] **Step 11 — Error handling.** ERROR response parsing (opcode 0x50),
  length-prefixed error message extraction, an exception hierarchy, and retry
  logic for transient errors. Java ref:
  `org.infinispan.client.hotrod.exceptions.*`.
- [ ] **Step 12 — Bulk operations.** `GET_ALL` (0x2F), `PUT_ALL` (0x2D),
  `BULK_GET` (0x1F, iterator-style).

**Other known work (not roadmap steps):**
- [ ] **Benchmark the multiplexing path** — the async rewrite targets 5–10×
  concurrent throughput; unmeasured so far.
- [ ] **Code cleanup:** read header "other params" when `paramCount > 0`
  (`src/operations/RemoteCache.cpp:140,182`).
- [ ] **TLS/SSL support** (transport encryption).

**Longer horizon (Step 14+ advanced features):** transactions (XA), client
listeners/events, counters, Ickle queries, streaming ops (4.1+), multimap,
near caching. Not scheduled.

_Done and not repeated here: foundation/primitives, Protocol 4.0 headers,
SCRAM auth (Step 3), topology (Step 4), consistent hashing + hash-aware
routing, connection pooling (Step 13), full CRUD (Steps 6–9), async
multiplexing. See "Working and shipped" below and `PROGRESS.md`._

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
- **getWithMetadata** (0x1B/0x1C) — value + entry version/expiration metadata,
  the entry point for version-based CAS (Step 10)
- **removeWithVersion** (0x0D/0x0E) — version-based conditional remove (CAS);
  returns `future<bool>` (removed?), version from `getWithMetadata`
- **Async API:** all operations return `std::future` / `std::optional`
- **MultiplexedConnection:** true async — dedicated read-loop thread,
  `messageId → promise` pending map, `execute()` used by all four operations.
  _(The July "70% / temporary blocking" docs are obsolete — see archive note.)_
- Multi-server topology test fixtures + Docker-based integration tests
- **CI runs integration tests on Linux** (Ubuntu job) as of 2026-09-18 — the
  `ubuntu-latest` runner's host Docker daemon starts the Infinispan containers.
  Windows and Fedora stay unit-only (Fedora runs inside a container with no
  Docker daemon; Windows doesn't build the integration tests). Windows/MSVC
  portability and `-Werror` build parity also landed (Sept 2026).

**Test status (verified 2026-09-19):**
- Unit: **160/160** passing (`./build/unit_tests`, <1s)
- Integration: **58/58** passing across 10 suites (`ctest`, spins up Docker
  Infinispan single-server + multi-node clusters), now also green on Linux CI.
  The interlaced distributed tests (`ConcurrentMultiServerTest`) were fixed to
  tolerate a GET racing ahead of its PUT — a `nullopt` is expected, only a
  present-but-wrong value is an error.
- Full run: `ctest --test-dir build --output-on-failure` → 100% pass (10 ctest targets)
- **Local caveat:** the multi-node cluster suites bind fixed host ports
  `11222/11322/11422/11522`; free `11222` (e.g. stop the `memory-service`
  Infinispan) before running them locally, or they fail with "port is already
  allocated". CI runners have these ports free.

**Not started / open:** see [📋 Backlog](#-backlog-the-whole-list) above for the
full list (Steps 10–12, benchmarks, TLS, code TODOs).

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
| **Wire-format spec (protocol 4.0/4.1)** | Kaitai schema — local `../hotrod-dissector/schemas/hotrod40.ksy`, public [github.com/rigazilla/hotrod-dissector](https://github.com/rigazilla/hotrod-dissector/tree/main/schemas) |
| **Java reference client** (authoritative for behavior/semantics) | local `/home/rigazilla/git/infinispan/client/hotrod-client/`, public [github.com/infinispan/infinispan](https://github.com/infinispan/infinispan/tree/main/client/hotrod-client) |

---

## Plan: metadata operations (Step 10)

Opcodes verified against the official Hot Rod protocol reference
(infinispan.org/docs → hotrod_protocol) **and** the authoritative Kaitai
schema at [`../hotrod-dissector/schemas/hotrod40.ksy`](../../hotrod-dissector/schemas/hotrod40.ksy)
on 2026-09-16. That `.ksy` is the wire-format source of truth for protocol
4.0/4.1 — consult it (not prose docs) when in doubt about a byte layout.

> **Why the Kaitai schema is trustworthy:** it's an *independent* encoding of
> the same protocol the Java client implements, so the two act as a mutual
> cross-check. If this client, the schema, and the Java client ever disagree on
> a byte layout, one of them has a bug — investigate before shipping. Public
> copy: <https://github.com/rigazilla/hotrod-dissector/tree/main/schemas>.

### Opcode table (request / response)

| Operation | Req | Resp | In code? |
|-----------|-----|------|----------|
| put | 0x01 | 0x02 | ✅ |
| get | 0x03 | 0x04 | ✅ |
| putIfAbsent | 0x05 | 0x06 | ❌ |
| replace (only if present) | 0x07 | 0x08 | ❌ |
| **replaceWithVersion** (replaceIfUnmodified) | **0x09** | **0x0A** | ❌ |
| remove | 0x0B | 0x0C | ✅ |
| **removeWithVersion** (removeIfUnmodified) | **0x0D** | **0x0E** | ✅ |
| containsKey | 0x0F | 0x10 | ❌ |
| getWithVersion | 0x11 | 0x12 | ❌ |
| **getWithMetadata** | **0x1B** | **0x1C** | ✅ |

Opcode constants are defined in `include/hotrod/HeaderCodec.h` (`Opcodes`
namespace) — currently only PUT/GET/REMOVE/PING. Add the new ones there.

### Recommended order
`getWithMetadata` first (it yields the entry version), then the two
version-based writes that depend on it, then the simpler conditionals:
1. ~~**getWithMetadata** (0x1B/0x1C)~~ ✅ shipped 2026-09-17
2. ~~**removeWithVersion** (0x0D/0x0E)~~ ✅ shipped 2026-09-19
3. replaceWithVersion (0x09/0x0A) ← next session
4. putIfAbsent (0x05/0x06), replace (0x07/0x08), containsKey (0x0F/0x10)

### Step-by-step: getWithMetadata (the entry point)

Mirrors the existing `get()` at `src/operations/RemoteCache.cpp:221`.

1. **Opcodes** — add to `HeaderCodec.h`:
   `GET_WITH_METADATA_REQUEST = 0x1B`, `GET_WITH_METADATA_RESPONSE = 0x1C`.
2. **Request encoding** — identical to GET: Protocol 4.0 header + key as
   `lp_bytes` (vInt length + bytes). Reuse the get() request builder.
3. **Response body parser** — this is the new bit. `execute()` extracts the
   header status and hands it to the parser as the `status` arg (do **not**
   read a status byte from the body — the `.ksy` puts status in the header):
   - `status == 0x01 || 0x02` → key not found → return `{}` (→ `nullopt`),
     exactly as `get()` does at `RemoteCache.cpp:221`
   - on `status == 0x00`: **reuse `Connection::receiveMetadata()`**
     (`Connection.cpp:292`) — it reads flag + optional created(s8)/lifespan(vInt)
     + optional lastUsed(s8)/maxIdle(vInt) + 8-byte version, byte-for-byte
     matching `entry_metadata` in the `.ksy` (lines 498–516)
   - read value as `lp_bytes` (`conn->receiveByteArray()`)
   - return `EntryWithMetadata{ metadata, value }`
4. **Public method** — add
   `std::future<std::optional<EntryWithMetadata>> getWithMetadata(const ByteArray& key);`
   to `RemoteCache.h`, implement via `connection_->execute(...)` with the
   parser lambda (same shape as get()).
5. **Unit test** — new `tests/unit/GetWithMetadataTest.cpp`: encode request,
   decode a crafted response with and without infinite-lifespan/maxidle flags,
   assert version + timestamps + value. Add target to `CMakeLists.txt` and to
   the `unit_tests` sources.
6. **Integration test** — PUT a key, then `getWithMetadata`, assert a non-zero
   version and the value round-trips (pattern: `GetIntegrationTest.cpp`).

**Wire format cheat-sheet (response 0x1C):**
`status(1)` · `flag(1)` [`0x01`=INFINITE_LIFESPAN, `0x02`=INFINITE_MAXIDLE] ·
`created(int64)`+`lifespan(vInt)` *(only if lifespan not infinite)* ·
`lastUsed(int64)`+`maxIdle(vInt)` *(only if maxIdle not infinite)* ·
`version(int64, 8 bytes, always)` · `valueLen(vInt)`+`value` *(on success)*.

**Then removeWithVersion / replaceWithVersion** carry the 8-byte version *in the
request*: removeWithVersion = key + version; replaceWithVersion = key + lifespan
(vInt) + maxIdle (vInt) + version + value. Responses use status `0x01` =
"not done, key was modified", `0x02` = "key does not exist".

## How this project's docs work (the process)

The full per-session workflow — start-of-session re-orientation, what to update
before you stop, the authoritative protocol sources, and how each doc fits
together — lives in **[`WORKFLOW.md`](WORKFLOW.md)**. Read that first if you're
coming back to the project.

The one habit that keeps this file trustworthy: **update `⏭ Next steps` + the
`Last updated` date when you *stop*, not when you start** — finished or not.

Rule of thumb: if a fact about the project changes, it changes *here* (or in
`DECISIONS.md` for *why*) — not in a new dated doc.
