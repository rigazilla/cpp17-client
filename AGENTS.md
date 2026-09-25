# Working agreement for this repo

This project is worked on **intermittently, solo**. The whole process exists to
**minimize the cost of re-orienting after a long gap.** The full process is in
[`docs/WORKFLOW.md`](docs/WORKFLOW.md); read it first if you're picking this up
after a break. This file is the short version an agent must always honor.

## First thing in a fresh clone / machine

Activate the versioned git hooks (once):

```
git config core.hooksPath .githooks
```

The `pre-commit` hook enforces the rule below. Without this it silently won't.

## The one non-negotiable rule

**When you change project source, you update the living status before you
commit.** A `pre-commit` hook blocks any commit that touches `src/`, `include/`,
`tests/`, `CMakeLists.txt`, or `.github/workflows/` without also staging
`docs/STATUS.md`.

Split what you write by *kind*, so each doc keeps exactly one job:

- **`docs/STATUS.md` — CURRENT STATE ONLY.** Refresh `⏭ Next steps`, the
  `Last updated` date, and (if a feature shipped) `Working and shipped` + the
  test counts. Do **not** write narrative history here — no "what I did today",
  no rationale, no dated blow-by-blow. It answers only "where is the project
  right now."
- **`docs/DECISIONS.md` — APPEND-ONLY HISTORY + REASONING.** Anything
  history-worthy goes here: what changed and *why*, dated milestones, gotchas
  discovered, schema/protocol bugs, superseded decisions. Never rewrite or
  delete an old entry; supersede it with a new dated one.
- **`README.md`** — **check it after every commit.** If a *user-facing* fact
  changed (shipped feature, public API, test numbers, step/milestone status),
  update it in the same commit so it never drifts from STATUS.md.

If a commit is a legitimate pure docs catch-up (reconciling a prior
source-only commit), bypass with `git commit --no-verify` — but that should be
rare; prefer co-committing STATUS.md with the change.

## Authoritative protocol sources (when porting/debugging the wire)

In order — if any two disagree, stop and investigate before shipping:
1. **Java client** (behavior/semantics) — `/home/rigazilla/git/infinispan/client/hotrod-client/`
2. **Kaitai schema** `hotrod40.ksy` (byte layout) — `../hotrod-dissector/schemas/`
3. **Test vectors** (regression, baked into unit tests)

Prose protocol docs drift; they are a convenience, not authoritative.
