# Working on this project — the per-session workflow

This project is worked on **intermittently, solo**. The whole process is
designed around one goal: **minimize the cost of re-orienting after a long
gap.** If you follow the loop below, you can disappear for weeks and pick up in
minutes.

The single rule that makes it work:

> **Write down where you are and what's next when you *stop* — not when you
> start.** Finished or interrupted, spend 60 seconds updating `STATUS.md` while
> the context is still in your head. That note is what future-you reads first.

**This rule is enforced, not just advised.** A versioned `pre-commit` hook
(`.githooks/pre-commit`) blocks any commit that changes project source
(`src/`, `include/`, `tests/`, `CMakeLists.txt`, `.github/workflows/`) without
also staging `docs/STATUS.md`. Activate it once per clone/machine:

```
git config core.hooksPath .githooks
```

(Also in the start-of-session checklist below. Legitimate pure-docs catch-up
commits can bypass with `git commit --no-verify`.)

---

## The per-session loop

### 1. Start of session — re-orient (≈2 min)

Run the checklist at the top of [`STATUS.md`](STATUS.md) (“▶ Coming back after a
break?”):

1. `git config core.hooksPath .githooks` (once per clone/machine — activates the
   STATUS.md pre-commit guard; harmless to re-run)
2. `git pull` (if working across machines)
3. `git log --oneline -5` — see what the last session actually did
4. Build: `cmake --build build` (configure first if needed: `cmake -S . -B build`)
5. Test: `ctest --test-dir build --output-on-failure`
6. Read [`STATUS.md → ⏭ Next steps`](STATUS.md#-next-steps-start-here), pick one, go.

Need more than the next 1–3 items? The full list is
[`STATUS.md → 📋 Backlog`](STATUS.md#-backlog-the-whole-list).

### 2. During — do the work

- Consult the **authoritative sources in order** (see below) — Java client for
  behavior, Kaitai schema for byte layout, test vectors for regression.
- Follow the porting discipline: test vectors → unit tests → implementation,
  one step at a time.

### 3. Before you stop — leave a breadcrumb (≈2 min, non-negotiable)

Do this **whether or not the step is finished** — mid-step interruption is the
normal case. Split what you write by *kind*, so each doc keeps one job:

1. **`STATUS.md` — current state ONLY.** Update
   [`⏭ Next steps`](STATUS.md#-next-steps-start-here) and the `Last updated`
   date. If a step is half-done, say exactly where you are (“parser done, unit
   test half-written, integration test not started”). If a feature shipped,
   move it to *Working and shipped*, tick it off the *Backlog*, and refresh the
   test counts. **Do not write narrative history here** — no dated blow-by-blow,
   no rationale, no "what I did today". STATUS.md answers only *"where is the
   project right now."* Anything that reads like history belongs in step 2.
2. **`DECISIONS.md` — history + reasoning (append-only).** This is where
   history-worthy detail lives: what changed and *why*, dated milestones,
   gotchas discovered, schema/protocol bugs, and design decisions. If future-you
   would want to know why or how something happened, it goes here — not in
   STATUS.md. **Never rewrite old entries**; supersede with a new dated one.
3. **`README.md`** — update **only if a user-facing fact changed** (a feature
   shipped, test numbers, public API). Most sessions won't touch it.
4. **Commit.** The `pre-commit` hook will block you if source changed and
   STATUS.md wasn't staged (see the rule box at the top of this file).

---

## Authoritative sources (in order)

When porting an operation or debugging the wire, consult these — and **if any
two disagree, stop and investigate before shipping**:

1. **Java client** — authoritative for *behavior and semantics* (what each op
   does, statuses, edge cases). Study it first (MANDATORY). Local
   `/home/rigazilla/git/infinispan/client/hotrod-client/`, public
   <https://github.com/infinispan/infinispan/tree/main/client/hotrod-client>.
2. **Kaitai schema** (`hotrod40.ksy`, protocol 4.0/4.1) — authoritative for
   *byte layout*; an independent, machine-readable cross-check of the Java
   client. Local `../hotrod-dissector/schemas/`, public
   <https://github.com/rigazilla/hotrod-dissector/tree/main/schemas>.
3. **Test vectors** — the byte-for-byte regression check baked into unit tests.

Prose protocol docs (infinispan.org) are a convenience, not authoritative —
they drift.

---

## How the docs fit together

Kept deliberately small so intermittent work stays cheap to resume. Each doc has
exactly one job:

| Doc | Job | Trust for… |
|-----|-----|-----------|
| [`STATUS.md`](STATUS.md) | The **living current state** — next steps, backlog, what's shipped, test numbers. **No narrative history.** | *"Where is the project right now."* This wins over any other doc for current state. |
| [`DECISIONS.md`](DECISIONS.md) | **Append-only** history + reasoning: what changed and *why*, dated milestones, gotchas, schema bugs, design decisions | *Why/how* something happened, and *when* |
| [`WORKFLOW.md`](WORKFLOW.md) | This file — the process (rarely changes) | *How* to work on the project |
| [`archive/PROGRESS.md`](archive/PROGRESS.md) | Per-step milestone history, frozen at Step 9 | History only — **never** current state |
| [`../README.md`](../README.md) | User-facing overview + quick start | Consuming the library |
| [`../../hotrod-foundry/ROADMAP.md`](../../hotrod-foundry/ROADMAP.md) | The master step definitions | *What each step means* |
| [`archive/`](archive/) | Frozen point-in-time snapshots | History only — **never** current state |

**Rule of thumb:** if the *current state* changes, it changes in `STATUS.md`
(not in a new dated doc, not in the README). If there's a *story* behind the
change — why, when, what broke — that goes in `DECISIONS.md`. Keep the two
apart: STATUS.md is a snapshot, DECISIONS.md is the ledger.
