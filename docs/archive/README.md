# Archived documentation (historical snapshots)

These files are **point-in-time snapshots**, kept for history only. They were
accurate when written but are **not maintained** and several contradict the
current code.

**For current project state, read [`../STATUS.md`](../STATUS.md).**
**For the reasoning behind design decisions, read [`../DECISIONS.md`](../DECISIONS.md).**

## Known-stale content in here

As of 2026-09-16, these describe the async/multiplexing work as unfinished
("70% complete", "temporary blocking implementation", "not truly async",
integration tests "need updating"). **All of that is done** — `MultiplexedConnection`
is fully implemented, every operation uses `execute()`, and the tests were
migrated. Do not trust these files for status:

- `IMPLEMENTATION_STATUS.md` — "70% Complete", PUT/GET/PING "still blocking"
- `MULTIPLEXING_IMPLEMENTATION_GUIDE.md` — "70% Complete", how-to for work now done
- `ASYNC_MIGRATION_COMPLETE.md` — titled "Complete" but describes only Phase 1 / blocking shim
- `ASYNC_MIGRATION_STATUS.md` — tests marked "❌ TODO" (they're migrated)
- `QUICK_REFERENCE.md` — "❌ REMOVE" checklist for code already removed

Still useful as *reference/design* background (but check against STATUS/DECISIONS):
- `MULTIPLEXING_DESIGN.md`, `MULTIPLEXING_DECISIONS.md`, `ASYNC_API_FINAL.md`
- `PHASE2_COMPLETE.md`, `TOPOLOGY_TESTING_SETUP.md`, `topology-test-architecture.md`
- `ASYNC_API_MIGRATION.md`, `TEST_UPDATE_PROGRESS.md`
