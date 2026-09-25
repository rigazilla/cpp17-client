# Step 11 — Error Handling: Design & Progress

> **Durable design + tracking doc for a multi-session feature.** Not a dated
> snapshot. Update the **Progress checklist** and the **Open questions** as work
> lands; record *why* decisions were made in [`DECISIONS.md`](DECISIONS.md) and
> keep the one-line pointer in [`STATUS.md`](STATUS.md) → *Next steps* current.
>
> **Created:** 2026-09-21 · **Refined:** 2026-09-25 (D4 classification, D5
> proxy-to-non-owner, Java cross-check) · **Status:** design agreed,
> implementation not started

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

### D4 — Classify *futility* (transient vs permanent), not *safety* (2026-09-25)
Two orthogonal questions decide a retry: **(1) is it safe?** (won't corrupt the
logical answer) and **(2) is it worth it?** (could the failure mode plausibly
clear). Only (1) can be left to the user — it needs op-idempotency knowledge the
library lacks (D2). (2) needs the *error taxonomy*, which only the library has;
forcing users to reconstruct it from raw status codes recreates the
string-matching pain we're removing. So the library **classifies futility**, the
user **decides safety**, and a retry is warranted only when *both* hold.

To keep this honest we avoid the word "retriable" (it sounds like a safety
verdict). The library exposes raw facts (`phase`, `serverStatus`) and an
**advisory** classifier `isTransient()` (§3.3). Classification:
- **`phase == BeforeSend`** → transient (nothing sent; safe *and* not futile).
- **`phase == AfterSend`** → transient-but-**ambiguous** (futility says "not
  pointless", but this is the case where the user's idempotency judgment gates).
- **`phase == ServerError`** → classify by status code (§3.3 table). Node-level
  codes (`0x87 NODE_SUSPECTED`, `0x88 ILLEGAL_LIFECYCLE_STATE`) and `0x86
  COMMAND_TIMEOUT` are transient (`0x86` also flagged `outcomeUncertain`);
  request-level codes (`0x81–0x85`) are permanent.

### D5 — `proxyToNonOwner`, defaulted `true` (matches Java) (2026-09-25)
When a key's owner(s) can't be reached, do we auto-fall-back to a **non-owner**
(which proxies to the owner), or stop at "owners exhausted" and let the user opt
in? Made configurable via a flag **defaulted to `true`**, matching Java.

Java-confirmed (2026-09-25, `OperationDispatcher.addressForObject` /
`RoundRobinBalancingStrategy`): once an owner is in the op's `failedServers`,
routing falls through to `balancer.nextServer(...)` over the *whole* cluster; the
chosen non-owner proxies to the real owner. Default `max_retries = 3`
(`ConfigurationProperties.DEFAULT_MAX_RETRIES`).

**Scope caveat:** in *our* model the flag governs only the **BeforeSend**
connection-fallback inside `selectServerForKey`. Java also auto-re-dispatches
`AfterSend` transport failures (accepting double-apply risk); we deliberately do
**not** (D2). So `proxyToNonOwner=true` means "Java-like *routing* fallback", not
"become fully Java-automatic". `false` → after the owner loop fails, throw a
transient exception with `ownersExhausted = true` so the user drives the proxy
step.

---

## 3. The design (target)

### 3.1 The exception (shared foundation, 11a)
One **pure-data** exception type (start minimal; add subtypes only if users need
to `catch` different ones):

```cpp
enum class FailurePhase { BeforeSend, AfterSend, ServerError };

class HotRodClientException : public std::runtime_error {
public:
    FailurePhase               phase;          // WHEN it failed — see below (raw fact)
    std::vector<ServerAddress> triedNodes;     // nodes attempted (UNION across retries)
    std::optional<uint8_t>     serverStatus;   // Hot Rod status / ERROR code, if any (raw)
    bool                       ownersExhausted;// all owners of the key are in triedNodes
                                               // → further retry reaches only a proxy, or nothing
    // NB: no stored `retriable` — futility is derived via isTransient() (§3.3, D4).
    // message() via std::runtime_error::what()
};
```

The exception carries **raw facts** (`phase`, `serverStatus`, `triedNodes`) plus
one derived advisory (`ownersExhausted`). Futility (transient vs permanent) is a
free function, not a stored verdict (D4) — see §3.3. `ownersExhausted` is
computed for free from `consistentHash_.getOwners(key, topology_)`
(`RemoteCache.cpp:756`): `ownersExhausted = (owners ⊆ triedNodes)`.

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
struct RetryContext {                          // USER→library input only
    std::vector<ServerAddress> excludeNodes;   // don't dispatch here again
    // (room to grow: deadline, attempt count, ...)
    // NB: NOT proxyToNonOwner — that's routing policy, kept client-level
    //     (config), matching how Java models balancing/maxRetries (D5).
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

The existing selection (`RemoteCache.cpp:746`) already has the shape: its
owner loop (761) and fallback "any server" loop (780) skip already-tried nodes
(782–793). The only new code is (a) an `isExcluded(node, excludeNodes)` predicate
in both loops, and (b) reporting the nodes the internal sweep touched so they
land in `triedNodes`. `proxyToNonOwner` (client-level, D5) gates loop (b): when
`false`, the owner loop failing throws `ownersExhausted=true` instead of entering
the non-owner fallback.

### 3.3 Futility classification (11a, D4)
Raw facts on the exception; classification is a free function — advisory, never a
safety verdict. The user ANDs it with their own idempotency judgment.

```cpp
// Server ERROR (0x50) status → transient? Mirrors Java's retriable set.
bool isTransientStatus(uint8_t status) {
    switch (status) {
        case Status::NODE_SUSPECTED:          // 0x87  node-level: try another owner
        case Status::ILLEGAL_LIFECYCLE_STATE: // 0x88  node starting/stopping
        case Status::COMMAND_TIMEOUT:         // 0x86  busy/slow node → retry elsewhere
            return true;                      //       (ambiguous — see outcomeUncertain)
        case Status::INVALID_MAGIC_OR_MESSAGE_ID: // 0x81 ─┐ request-level: same
        case Status::UNKNOWN_COMMAND:             // 0x82  │ request → same answer
        case Status::UNKNOWN_VERSION:             // 0x83  │ on any node. Futile.
        case Status::REQUEST_PARSING_ERROR:       // 0x84  │
        case Status::SERVER_ERROR:                // 0x85 ─┘
            return false;
        default: return false;                    // unknown → treat as permanent
    }
}

bool isTransient(const HotRodClientException& e) {   // axis 2: worth retrying?
    switch (e.phase) {
        case FailurePhase::BeforeSend: return true;   // nothing sent
        case FailurePhase::AfterSend:  return true;   // not futile, but AMBIGUOUS
        case FailurePhase::ServerError:
            return e.serverStatus && isTransientStatus(*e.serverStatus);
    }
    return false;
}

// axis 1 helper: might the op have applied despite failing? ORTHOGONAL to transient.
// The user ANDs this with their own op-idempotency knowledge before retrying.
bool outcomeUncertain(const HotRodClientException& e) {
    return e.phase == FailurePhase::AfterSend                    // sent, reply lost
        || (e.phase == FailurePhase::ServerError
            && e.serverStatus == Status::COMMAND_TIMEOUT);       // 0x86 may have applied
}
```

**0x86 COMMAND_TIMEOUT is transient *and* uncertain** — same profile as
`AfterSend` (a client read-timeout and a server command-timeout are the same
situation from two sides). Retry is allowed (likely a busy node; exclusion routes
it elsewhere) but the user must gate it on op idempotency via `outcomeUncertain`.
**This intentionally diverges from Java**, which treats timeout as terminal
(`HotRodTimeoutException`); our two-axis model lets us *retry-but-flag* instead.

**Phase assignment lives in the transport** (network errors carry no status):
`getConnection`/`connect` failure → `BeforeSend`; any failure once `write()` has
*begun* (even a partial write) or while awaiting the response → `AfterSend`;
ERROR 0x50 → `ServerError`. Rule of thumb: **write started at all ⇒ AfterSend**.

**Terminal exhaustion vs permanent:** when selection runs out of nodes ("No
servers available", `RemoteCache.cpp:809`), throw a **non-transient** exception so
the user loop stops. That is *exhausted* ("no node could serve it now"), distinct
from *permanent* (`0x81–0x86`, "request itself rejected") — both stop the loop,
but the diagnosis differs.

**Missing constants:** `HeaderCodec.h::Status` currently lacks `0x84`
`REQUEST_PARSING_ERROR`, `0x87` `NODE_SUSPECTED`, `0x88`
`ILLEGAL_LIFECYCLE_STATE` — add them (cross-check `hotrod40.ksy` + Java
`HotRodConstants`) as part of 11a.

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

### 11a — Error surfacing — ✅ shipped 2026-09-25
- [x] Add ERROR response (0x50) + missing status constants to `HeaderCodec.h`:
      `0x84 REQUEST_PARSING_ERROR`, `0x87 NODE_SUSPECTED`,
      `0x88 ILLEGAL_LIFECYCLE_STATE` (§3.3).
- [x] Parse the ERROR response body (length-prefixed error message). Drained in
      `readLoop` *before* the pending lookup so an orphan ERROR can't desync the
      stream.
- [x] Define `FailurePhase` + `HotRodClientException` (pure data + `ownersExhausted`, §3.1).
- [x] Add `isTransientStatus()` / `isTransient()` / `outcomeUncertain()` free
      helpers (§3.3, D4). `0x86` is transient + uncertain.
- [x] Detect `phase` in the transport (BeforeSend vs AfterSend around `write()`;
      ServerError on 0x50) and populate `triedNodes`.
- [x] Map response statuses → `HotRodClientException` at the existing throw sites
      (`RemoteCache.cpp:84,237,291,411,449,500,...`), replacing `runtime_error`.
- [x] Make "No servers available" (`RemoteCache.cpp:809`) a non-transient exception.
- [x] Unit tests: ERROR-body parse; each phase; status→exception mapping; isTransient.
- [x] Integration test: trigger a server ERROR and assert the typed exception
      (+ `ConnectionUsableAfterServerError` proves no stream desync).

### 11b — User-decided retry
- [ ] `RetryContext` struct (§3.2), defaulted on every op signature.
- [ ] Exclusion-aware `selectServerForKey` variant (skip `excludeNodes`).
- [ ] Client-level `proxyToNonOwner` flag (default `true`, D5): gates the
      owner→non-owner fallback; when `false`, throw `ownersExhausted=true`.
- [ ] Populate `ownersExhausted` from `getOwners(key) ⊆ triedNodes`.
- [ ] Thread-safety: guard `topology_` + `connectionPool_` + selection path
      (retry may be driven concurrently with the read-loop's topology updates).
- [ ] `triedNodes` accumulation across successive retries (union); the internal
      BeforeSend sweep reports *all* nodes it touched.
- [ ] Unit tests: exclusion selection; context threading; union accumulation;
      proxyToNonOwner on/off; ownersExhausted.
- [ ] Integration test: kill a node mid-run, user-loop retry lands on another.
- [ ] Docs/example: the retry loop pattern (§3.2) in `examples/quickstart/`.

---

## 6. Open questions (resolve as we implement)
- **[RESOLVED, D4 / 2026-09-25]** Classification decided (§3.3): transient =
  node-level `0x87`/`0x88` **and `0x86 COMMAND_TIMEOUT`** (busy node → retry
  elsewhere, flagged `outcomeUncertain`); permanent = request-level `0x81–0x85`.
  `0x86` intentionally diverges from Java. Only residual doubt: `0x85
  SERVER_ERROR` stays permanent — revisit if a real case wants it retried.
- Minimal exception vs a small subtype family — start with one type; revisit only
  if a real `catch`-differentiation need appears.
- **[RESOLVED, D5]** Interaction with the connection-establishment failover in
  `selectServerForKey`: the internal sweep is **BeforeSend-only** (it wraps
  `getConnectionForServer`; once a live conn is returned, dispatch failures
  propagate as AfterSend/ServerError, never internally retried). The `excludeNodes`
  set composes with it; `proxyToNonOwner` (client-level, default `true`) gates
  whether the sweep continues past owners into non-owners. The sweep must report
  *every* node it touched into `triedNodes` (union) so a user retry doesn't
  re-hit a known-dead node.
- Do we need a `deadline`/max-attempts hint in `RetryContext`, or leave all
  pacing to the user? (Lean: leave to the user for now. Java's automatic
  `max_retries` default is 3 — a reference point, not a binding choice since our
  retry is user-driven.)

---

## 7. References
- Java behavior/semantics (authoritative): `org.infinispan.client.hotrod.exceptions.*`
  — local `/home/rigazilla/git/infinispan/client/hotrod-client/`.
- Java routing/retry cross-check (2026-09-25): non-owner fallback in
  `impl/transport/netty/OperationDispatcher.java` (`addressForObject` →
  `balancer.nextServer`); default balancer `RoundRobinBalancingStrategy`;
  `max_retries` default 3 in `impl/ConfigurationProperties.java`
  (`DEFAULT_MAX_RETRIES`). Retriable set: `RemoteNodeSuspectException` (0x87),
  `RemoteIllegalLifecycleStateException` (0x88), `TransportException` (no status);
  genuine server errors terminal. Note: Java retries **automatically**; we chose
  **user-decided** (D2) — the divergence is intentional.
- Wire layout: `hotrod40.ksy` error response — local
  `../hotrod-dissector/schemas/`.
- Source-hierarchy rule: [`DECISIONS.md`](DECISIONS.md) 2026-09-16 entry.
- Roadmap step: STATUS.md backlog → Step 11.
