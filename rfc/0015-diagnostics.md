# RFC-0015 — Diagnostics and memory accounting

Status: Accepted · Module: `core/diagnostics` · PRD F14 · Tests: `tests/core/test_diagnostics.cpp`

- Logger: JSON lines `{t, level, event, fields}`; sinks: rotating file (5 × 2 MB) and optional stderr.
  Helpers forbid pixel buffers (only scalars/strings). `anonymizePath()` keeps the extension and a stable hash.
- Events (minimum): `scan.open` (format, size, depth, ICC presence, DPI, compression), `detect.done` (count,
  confidences, quads, timings), `export.done`, `composite.done`, `cache.stats`, `mem.peak`, `warning`, `error`.
- Memory: `MemoryStats current()` → resident, peak resident, platform private/footprint
  (Linux `/proc/self/status`, macOS `task_info(TASK_VM_INFO).phys_footprint`, Windows `GetProcessMemoryInfo`
  PrivateUsage/PeakWorkingSetSize), plus our accounted buffer bytes (`BufferAccounting` counters by category).
- Diagnostic bundle: zip-less folder (logs, anonymised project summary, system info) written where the user chooses.
