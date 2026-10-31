# RFC-0014 — Cache

Status: Accepted · Module: `core/cache` · ADR-0006 · Tests: `tests/core/test_memory.cpp` (`CacheLedger.*`),
self-test `runMemoryBudget` (FK-005)

## Disk cache (planned)
- `DiskCache(root, capBytes)`: `get(key) → optional<bytes>`, `put(key, bytes)` (temp → flush → rename →
  index update), `evictToCap()`, `clear()`, `sizeBytes()`, `cleanupOrphans()` at construction.
- Index: `index.json` rewritten atomically, entries `{key, size, lastAccess}`; a missing or corrupt index is rebuilt by scanning files.
- Keys: hex SHA-256 strings; files sharded `ab/abcdef….bin`.
- Not implemented yet: proxies and thumbnails are rebuilt from the scan when needed (ADR-0006 stays the plan;
  the library units UX-05 decide when it is built).

## Memory budget (implemented, U017 / FK-005)
The first draft named a `ByteBudgetLru<K,V>` container per cache. Implemented instead: **one ledger for all
in-memory caches** (`fk::CacheLedger`, `fk/cache.hpp`), because the caches hold different value types in
different owners (the app's proxies and previews are `QImage`, decoded strips live inside `TiffSource`) and the
budget has to be shared between them.

- **Entries** `key → bytes, last use`. Keys used by the app: `display:<scan>` (the scan's display proxy,
  ≈ 3000 px on the long side, RGB32), `source:<scan>` (decoded strips of the full-resolution view;
  `TiffSource::cachedBytes()`), `composite:<item>` (composite preview, ≤ 2400 px), `thumb:<scan>` (scan and
  print thumbnails), `canvas` (the canvas's pyramid levels above the proxy and its full-resolution patch).
- **Pinned** = on screen: the current scan's proxy and decoded strips, the current print's preview. Never
  dropped by the budget; they become the most recently used entries when shown.
- **Kept** = counted but never dropped by the budget: thumbnails (the scan list and filmstrip are their only
  source until the library has a disk cache, about 0.5–1 MB per scan) and the canvas images (one view).
- **Budget** `defaultCacheBudget(RAM)` = RAM / 16, clamped to 256 MB … 2 GB: 8 GB → 512 MB, 16 GB → 1 GB.
  Diagnostic overrides (environment, read at start): `FK_CACHE_BUDGET_MB` (0 = unlimited, for comparisons),
  `FK_ASSUME_RAM_GB` (size the budget for another device).
- **Rule** after every change: drop least recently used droppable entries until
  `total ≤ max(budget − reserved, pinned + kept)`. The owner frees what the ledger drops; the next time the
  entry is needed it is rebuilt from the source file (proxy: `loadPreview`; strips: decoded again; preview:
  computed again when the print is shown in composite mode). Dropping costs time, never output quality, and
  never changes a result: a relink check whose target proxy was dropped reads the same proxy again in its job.
- **Reservations**: every job states its expected working memory (`JobRunner::start(…, estimateBytes)`):
  detection/opening = 128 MB decoded strips + proxy × (2 × bytes per pixel + 4); exports and previews =
  128 MB + the print's region × 2 (× 3 with a graded file). Per lane, the largest estimates of its queued and
  running jobs count, as many as the lane runs in parallel (detect 1, export 1, io 2). The reservation exists
  from the moment the job is queued, so the caches make room before its work starts; it is released when the
  job's result has been applied. Jobs are never blocked by the budget (the lanes already bound concurrency).
- **Decoded strips of the view**: one scan's `TiffSource` holds at most min(96 MB, budget / 4).
- **Memory pressure** (`app/src/MemoryPressure.cpp`): macOS `DISPATCH_SOURCE_TYPE_MEMORYPRESSURE` (warn /
  critical), Windows `CreateMemoryResourceNotification(LowMemoryResourceNotification)` polled every 2 s, Linux
  `MemAvailable` < max(5 % of RAM, 256 MB) polled every 2 s. Handler: drop every droppable entry and the
  current scan's decoded strips; the proxy and preview on screen stay. Repeated every 30 s while low
  (Windows, Linux).
- **Report** `AppController::memoryReport()`: RSS, peak RSS, private bytes (Windows PrivateUsage, macOS
  phys_footprint, Linux RssAnon), accounted buffers, budget, cached / fixed / reserved bytes, bytes and counts
  per kind, images actually held (must equal the ledger's counts), evictions, pressure events.

## Not covered
- Disk cache (above). Library grid virtualisation: UX-05 (U019/U020).
- 8 GB / 16 GB devices: the budget is computed for them (`FK_ASSUME_RAM_GB`) and measured on the Linux VM;
  real devices are acceptance items (PERFORMANCE-BUDGET.md §3).
