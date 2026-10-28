# RFC-0014 — Cache

Status: Accepted · Module: `core/cache` · ADR-0006 · Tests: `tests/core/test_cache.cpp`

- `DiskCache(root, capBytes)`: `get(key) → optional<bytes>`, `put(key, bytes)` (temp → flush → rename →
  index update), `evictToCap()`, `clear()`, `sizeBytes()`, `cleanupOrphans()` at construction.
- Index: `index.json` rewritten atomically, entries `{key, size, lastAccess}`; a missing or corrupt index is rebuilt by scanning files.
- Keys: hex SHA-256 strings; files sharded `ab/abcdef….bin`.
- `ByteBudgetLru<K,V>` for in-memory caches with a `shrinkTo(bytes)` hook for memory pressure.
