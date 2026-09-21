# Step 11 — Error Handling: Design & Progress

> **Durable design + tracking doc for a multi-session feature.** Not a dated
> snapshot. Update the **Progress checklist** and the **Open questions** as work
> lands; record *why* decisions were made in [`DECISIONS.md`](DECISIONS.md) and
> keep the one-line pointer in [`STATUS.md`](STATUS.md) → *Next steps* current.
>
> **Created:** 2026-09-21 · **Status:** design agreed, implementation not started

---

## 1. Why this, why now

Before doing a **C++-vs-Java developer-experience assessment** (a planned but not
yet scheduled activity), we need error handling sorted out, because the current
error surface *is* the biggest UX gap and would dominate any such report anyway.

**Current state (verified against code, 2026-09-21):**
- Every operation surfaces *all* failures as a bare `std::runtime_error` via
  `future.get()` — server ERROR, socket drop, auth failure, timeout, unexpected
  status are indistinguishable except by string-matching `what()`.
  (`include/hotrod/RemoteCache.h` `@throws` lines; throw sites in
  `src/operations/RemoteCache.cpp:84,237,291,411,449,500,...`.)
- **No operation-level retry exists.** The "automatic failover" in
  `selectServerForKey` (`src/operations/RemoteCache.cpp:746`) is
  *connection-establishment* failover only: its `try/catch` wraps
  `getConnectionForServer` ("can I get a live connection to an owner?"). Once a
  live connection is returned, the op is dispatched to that one connection and is
  **never retried** — a mid-flight drop or a server ERROR propagates straight up.
- Errors already surface **on the user's thread**: transport failures are carried
  as `resp.error` (an `exception_ptr`) and `std::rethrow_exception`'d inside the
  per-op continuation (`RemoteCache.cpp:216,263,321,409,462,514,...`); status
  failures `throw` there too. So *surfacing* a richer exception is a clean drop-in
  at those exact sites — the hard part is *replay*, not *surfacing*.
- `topology_` and `connectionPool_` have **no mutex** (`RemoteCache.h:352,357`) —
  the selection/pool path is currently single-threaded-by-convention.

Java reference for the exception model:
`org.infinispan.client.hotrod.exceptions.*`.

---

## 2. Decisions made (2026-09-21)

### D1 — Split Step 11 into 11a (error surfacing) and 11b (retry)
Error surfacing and retry are two different *kinds* of change:
- **11a** is a leaf-level, additive edit (replace `runtime_error` at known throw
  sites; no control-flow change; reversible; no distributed-systems hazard).
- **11b** is a behavioral, cross-cutting change with distributed-systems hazards.

Retry also **depends on** 11a: you can't "retry on transient" until you can
classify transient vs permanent, which is exactly what ERROR parsing + the
exception fields provide. So 11a is a prerequisite, not an alternative.

### D2 — Retry is **user-decided**, not automatic
The one judgment only the user can make safely is *"is my operation safe to
retry?"* — because the dangerous case is the **ambiguous** failure (request sent,
response lost). Retrying a plain PUT then double-applies (usually tolerable); but
retrying a **conditional** op (`putIfAbsent`, `replace`, `replaceWithVersion`,
`removeWithVersion` — all shipped in Step 10) can return a *wrong logical answer*
(e.g. a `putIfAbsent` that actually succeeded, retried after a dropped response,
comes back "already present" → caller thinks it lost a race it won). The library
must not guess this. It surfaces the facts; the user decides.

### D3 — Retry lives on the **cache** (fork 1.b), not on the error (fork 1.a)
Two shapes were considered for *how* the user drives a retry:
- **1.a — retry on the error:** `err.retry()`. Keeps op signatures clean.
- **1.b — retry on the cache:** the op takes an optional retry/exclusion context;
  the error is a pure data value.

**Chosen: 1.b.** Rationale and the rough impact comparison are in §4. In short:
1.a costs roughly **2×** the effort of 1.b *and* introduces a permanent
lifetime-hazard class (an error object that captures client internals and can
outlive them); 1.b keeps the error a trivially copyable value and pays only a
mechanical, defaultable extra parameter on each op.

---

## 3. The design (target)

### 3.1 The exception (shared foundation, 11a)
One **pure-data** exception type (start minimal; add subtypes only if users need
to `catch` different ones):

```cpp
enum class FailurePhase { BeforeSend, AfterSend, ServerError };

class HotRodClientException : public std::runtime_error {
public:
    bool                       retriable;      // is a retry permitted at all?
    FailurePhase               phase;          // WHEN it failed — see below
    std::vector<ServerAddress> triedNodes;     // nodes already attempted
    std::optional<uint8_t>     serverStatus;   // Hot Rod status / ERROR code, if any
    // message() via std::runtime_error::what()
};
```

**`phase` is the highest-value field** — more than `retriable` — because it is
exactly what lets the user make the idempotency call:
- **BeforeSend** — couldn't connect / `write()` never started → clean, always
  safe to retry, any op.
- **AfterSend** — sent, no response → **ambiguous**; the user's own op-idempotency
  knowledge is required.
- **ServerError** — server returned ERROR (0x50); safety depends on the code.

`phase` is cheaply derivable in the transport (we know whether `write()`
completed). `triedNodes` accumulates across successive retries (each produced
error carries the *union*).

### 3.2 Retry via the cache (11b)
Ops gain an optional retry/exclusion context, defaulted so existing call sites are
untouched:

```cpp
struct RetryContext {
    std::vector<ServerAddress> excludeNodes;   // don't dispatch here again
    // (room to grow: deadline, attempt count, ...)
};

// existing signature keeps working:
std::future<std::optional<ByteArray>> get(const ByteArray& key);
// retrying caller passes context built from the caught exception:
std::future<std::optional<ByteArray>> get(const ByteArray& key,
                                          const RetryContext& ctx);
```

Typical user loop:
```cpp
RetryContext ctx;
for (;;) {
    try { return cache.get(key, ctx).get(); }
    catch (const HotRodClientException& e) {
        if (!e.retriable) throw;
        // user decides based on e.phase whether THIS op is safe to retry
        ctx.excludeNodes = e.triedNodes;
    }
}
```

`selectServerForKey` gains an **exclusion-aware** variant so a retry avoids the
dead node(s). Retry is then just the normal op path with an exclusion set — **no
closure capture, no shared dispatcher, no templated exception.**

---

## 4. Fork 1.a vs 1.b — impact comparison (for the record)

Both forks share a **foundation B**: ERROR 0x50 parsing · the exception with the
fields above · failure-**phase** detection in the transport · a node-exclusion
variant of `selectServerForKey`.

What each fork adds **on top of B**:

| Dimension | 1.a (retry on error) | 1.b (retry on cache) — **chosen** |
|---|---|---|
| Exception shape | Templated `RetriableError<T>` + non-template base so `retry()` can return the op's value (M) | Pure data, one type (0) |
| Re-dispatch machinery | Replayable closure captured into the error; reify each op into a re-invocable form (M) | Normal op path + exclusion arg — no capture (0) |
| Lifetime | Extract a shared dispatcher/session so the error can re-enter after crossing the `future` boundary; permanent "stashed error outlives client → UB" hazard (L) | Cache owns everything and is alive when called — no coupling (0) |
| API surface | Op signatures stay clean | ~9 ops gain a defaulted `RetryContext` param; common call site unchanged (M, mechanical) |
| Thread-safety of pool/topology | Required, and worse: retry fires from an arbitrary thread possibly long after the call (M–L) | Required, but it's ordinary concurrent method use (M) |

**Rough estimate:** 1.b ≈ **40–55%** of 1.a's total effort. Savings concentrate in
1.a's two heaviest items (shared-dispatcher/lifetime extraction, and the templated
exception family) — neither exists in 1.b. Beyond effort, 1.b avoids a lasting
lifetime-footgun and keeps the error trivially copyable/loggable/storable.

---

## 5. Progress checklist

### 11a — Error surfacing (do first)
- [ ] Add opcode/status constants for ERROR response (0x50) in `HeaderCodec.h`.
- [ ] Parse the ERROR response body (length-prefixed error message).
- [ ] Define `FailurePhase` + `HotRodClientException` (pure data, §3.1).
- [ ] Detect `phase` in the transport (BeforeSend vs AfterSend around `write()`;
      ServerError on 0x50) and populate `triedNodes`.
- [ ] Map response statuses → `HotRodClientException` at the existing throw sites
      (`RemoteCache.cpp:84,237,291,411,449,500,...`), replacing `runtime_error`.
- [ ] Unit tests: ERROR-body parse; each phase; status→exception mapping.
- [ ] Integration test: trigger a server ERROR and assert the typed exception.

### 11b — User-decided retry
- [ ] `RetryContext` struct (§3.2), defaulted on every op signature.
- [ ] Exclusion-aware `selectServerForKey` variant (skip `excludeNodes`).
- [ ] Thread-safety: guard `topology_` + `connectionPool_` + selection path
      (retry may be driven concurrently with the read-loop's topology updates).
- [ ] `triedNodes` accumulation across successive retries (union).
- [ ] Unit tests: exclusion selection; context threading; union accumulation.
- [ ] Integration test: kill a node mid-run, user-loop retry lands on another.
- [ ] Docs/example: the retry loop pattern (§3.2) in `examples/quickstart/`.

---

## 6. Open questions (resolve as we implement)
- Which Hot Rod statuses / ERROR codes are classified `retriable` and with which
  `phase`? (Cross-check Java `exceptions.*` + `hotrod40.ksy` error response.)
- Minimal exception vs a small subtype family — start with one type; revisit only
  if a real `catch`-differentiation need appears.
- Interaction with the existing connection-establishment failover in
  `selectServerForKey` — the exclusion set should compose with it, not fight it.
- Do we need a `deadline`/max-attempts hint in `RetryContext`, or leave all
  pacing to the user? (Lean: leave to the user for now.)

---

## 7. References
- Java behavior/semantics (authoritative): `org.infinispan.client.hotrod.exceptions.*`
  — local `/home/rigazilla/git/infinispan/client/hotrod-client/`.
- Wire layout: `hotrod40.ksy` error response — local
  `../hotrod-dissector/schemas/`.
- Source-hierarchy rule: [`DECISIONS.md`](DECISIONS.md) 2026-09-16 entry.
- Roadmap step: STATUS.md backlog → Step 11.
