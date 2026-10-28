# F14 — Cache, memory, diagnostics, settings

RFC: `rfc/0014-cache.md`, `rfc/0015-diagnostics.md`, ADR-0006 · Tests: `tests/core/test_cache.cpp`, `tests/core/test_diagnostics.cpp`

## Scope
- Disk cache for proxies and thumbnails only: content-addressed (scan fingerprint + params), LRU, size cap
  (default 2 GB or 5 % of free space, whichever is smaller; user adjustable), atomic writes, orphan temp cleanup at start,
  "Clear cache" in Settings; deleting the cache only costs time.
- Temp files in a private subfolder of the system temp dir; total cap; cleaned at start and after each job.
- Memory: per-job estimate before start; memory-pressure notifications (macOS dispatch source, Windows
  memory resource notification) drop preview caches, never output quality.
- Logs: structured, size-capped, rotated; no pixel data; paths anonymised on request (always in diagnostic bundles).
- Diagnostic bundle: only on user request, saved where the user chooses; never sent anywhere.
- Fully offline: no network code in v1.

## Acceptance
- A1 Cache never exceeds its cap; LRU eviction order verified. (`test_cache`)
- A2 Interrupted cache writes leave no registered entry; orphans removed at start. (`test_cache.Crash`)
- A3 Logs contain no pixel buffers and anonymised bundles contain no user paths. (`test_diagnostics`)
- A4 Binary contains no network symbols from Qt Network or sockets in core (CI check `check-no-network`).
