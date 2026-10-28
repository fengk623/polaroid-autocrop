# ADR-0006 — Cache strategy

Status: Accepted · Date: 2026-10-28

## Decision
On-disk cache holds only re-computable, small artefacts: scan proxies (≈3000 px long side, 16-bit RGB,
zlib-compressed raw) and print thumbnails (256 px JPEG). Keys are SHA-256 of
(scan fingerprint, artefact kind, parameters, format version). Entries are written to a temp name, flushed,
renamed, then registered in an index; eviction is LRU by last access with a byte cap (default
min(2 GB, 5 % of free disk)). Orphan temp files are removed at start. Full-resolution data and originals
are never cached. In-memory caches (decoded tiles, display tiles) are byte-bounded and shrink under
memory pressure.

## Consequences
Clearing the cache only costs recomputation time. Proxy rebuild of a 1600 DPI scan is a full decode pass (measured in PERFORMANCE-BUDGET.md).
