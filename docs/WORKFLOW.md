# Working on this project — the per-session workflow

This project is worked on **intermittently, solo**. The whole process is
designed around one goal: **minimize the cost of re-orienting after a long
gap.** If you follow the loop below, you can disappear for weeks and pick up in
minutes.

The single rule that makes it work:

> **Write down where you are and what's next when you *stop* — not when you
> start.** Finished or interrupted, spend 60 seconds updating `STATUS.md` while
> the context is still in your head. That note is what future-you reads first.

---

## The per-session loop

### 1. Start of session — re-orient (≈2 min)

Run the checklist at the top of [`STATUS.md`](STATUS.md) (“▶ Coming back after a
break?”):

1. `git pull` (if working across machines)
2. `git log --oneline -5` — see what the last session actually did
3. Build: `cmake --build build` (configure first if needed: `cmake -S . -B build`)
4. Test: `ctest --test-dir build --output-on-failure`
5. Read [`STATUS.md → ⏭ Next steps`](STATUS.md#-next-steps-start-here), pick one, go.

Need more than the next 1–3 items? The full list is
[`STATUS.md → 📋 Backlog`](STATUS.md#-backlog-the-whole-list).

### 2. During — do the work

- Consult the **authoritative sources in order** (see below) — Java client for
  behavior, Kaitai schema for byte layout, test vectors for regression.
- Follow the porting discipline: test vectors → unit tests → implementation,
  one step at a time.

### 3. Before you stop — leave a breadcrumb (≈2 min, non-negotiable)

Do this **whether or not the step is finished** — mid-step interruption is the
normal case:

1. **`STATUS.md`** — update [`⏭ Next steps`](STATUS.md#-next-steps-start-here)
   and the `Last updated` date. If a step is half-done, say exactly where you
   are (“parser done, unit test half-written, integration test not started”).
   If a feature shipped, move it to *Working and shipped*, tick it off the
   *Backlog*, and refresh the test counts.
2. **`DECISIONS.md`** — if you made a design decision whose reasoning you'd
   forget, append an entry. **Never rewrite old ones**; supersede instead.
3. **`README.md`** — update **only if a user-facing fact changed** (a feature
   shipped, test numbers, public API). Most sessions won't touch it.
4. **Commit.**

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
| [`STATUS.md`](STATUS.md) | The **living** status — next steps, backlog, current state, test numbers | *Everything about "where the project is."* This wins over any other doc. |
| [`DECISIONS.md`](DECISIONS.md) | **Append-only** log of design decisions + reasoning | *Why* something was done a certain way |
| [`WORKFLOW.md`](WORKFLOW.md) | This file — the process (rarely changes) | *How* to work on the project |
| [`archive/PROGRESS.md`](archive/PROGRESS.md) | Per-step milestone history, frozen at Step 9 | History only — **never** current state |
| [`../README.md`](../README.md) | User-facing overview + quick start | Consuming the library |
| [`../../hotrod-foundry/ROADMAP.md`](../../hotrod-foundry/ROADMAP.md) | The master step definitions | *What each step means* |
| [`archive/`](archive/) | Frozen point-in-time snapshots | History only — **never** current state |

**Rule of thumb:** if a fact about the project changes, it changes in
`STATUS.md` — not in a new dated doc, and not in the README.
