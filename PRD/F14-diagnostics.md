# F14 — Cache, memory, diagnostics, settings

RFC: `rfc/0014-cache.md`, `rfc/0015-diagnostics.md`, ADR-0006 · Tests: `tests/core/test_memory.cpp` (memory budget), self-test `runMemoryBudget`; planned: `tests/core/test_cache.cpp`, `tests/core/test_diagnostics.cpp`

## Scope
- Disk cache for proxies and thumbnails only: content-addressed (scan fingerprint + params), LRU, size cap
  (default 2 GB or 5 % of free space, whichever is smaller; user adjustable), atomic writes, orphan temp cleanup at start,
  "Clear cache" in Settings; deleting the cache only costs time.
- Temp files in a private subfolder of the system temp dir; total cap; cleaned at start and after each job.
- Memory (FK-005, U017, RFC-0014 §Memory budget): one byte budget for every in-memory cache (scan proxies,
  decoded strips of the full-resolution view, composite previews; thumbnails and the canvas counted), sized
  by the device's RAM (8 GB → 512 MB, 16 GB → 1 GB); least recently used first, never what is on screen;
  per-job estimate reserved before the job starts; memory-pressure notifications (macOS dispatch source,
  Windows memory resource notification, Linux available memory) drop rebuildable caches, never output
  quality.
- Logs: structured, size-capped, rotated; no pixel data; paths anonymised on request (always in diagnostic bundles).
- Diagnostic bundle: only on user request, saved where the user chooses; never sent anywhere.
- Fully offline: no network code in v1.

## Acceptance
- A1 Cache never exceeds its cap; LRU eviction order verified. (`CacheLedger.DropsLeastRecentlyUsedFirst` for the in-memory cache; a disk cache is not built, ADR-0006)
- A2 Interrupted cache writes leave no registered entry; orphans removed at start. (not built: there is no disk cache yet)
- A3 Logs contain no pixel buffers and anonymised bundles contain no user paths. (not built: diagnostic bundles are not implemented; `tools/check-no-network.py` covers "no upload")
- A4 Binary contains no network symbols from Qt Network or sockets in core (CI check `check-no-network`).
- A5 (FK-005) The caches never exceed the budget except by what is on screen or kept; dropped entries are
  rebuilt identically when needed; every image the app holds is counted. (`CacheLedger.*`, self-test
  "FK-005 batch detection…", "switching…", "full-resolution…", "composite previews…")
- A6 (FK-005) A job's working memory is reserved before it starts and released when it ends, also when it is
  cancelled. (self-test "FK-005 export…", "cancelled detection…")
- A7 (FK-005) Memory pressure drops every rebuildable cache, keeps the scan on screen, and outputs made
  afterwards are byte-identical; a relink check gives the same result whether its target's proxy is cached or
  not. (self-test "FK-005 memory pressure…", "relink check…")
- A8 (FK-005) Batches of 1/10/50/100 scans: after detection, switching, full-resolution view, composites and a
  cancelled detection, the steady state does not grow with the number of scans (RSS, private bytes,
  accounted buffers and cache recorded; `--scenario memory`, PERFORMANCE-BUDGET.md §3). 8 GB / 16 GB devices:
  budget computed and measured on the VM; real devices pending (HV).
