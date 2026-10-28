# F10 — Batch processing and grid review

RFC: `rfc/0010-jobs.md` · Tests: `tests/core/test_jobs.cpp`, `tests/core/test_batch.cpp`

## Scope
- Grid view: every print rendered with its own settings and status badges; exclude/include; per-item override; double-click opens the item.
- Batch operations: detect all, export image areas, auto-relink a folder of graded files, composite + final export.
- One large export at a time (global limit 1); detection/thumbnail pools bounded (default 2 workers, ≤ cores-1).
- Cancel any job; cancelled jobs release buffers immediately; partial outputs removed.
- Error policy: continue with the next item, collect errors in a report (default); "stop on first error" option.

## Acceptance
- A1 Cancelling a running export removes the temp file and returns memory to within 10 % of pre-job level. (`test_jobs.Cancel`)
- A2 Batch of 100 synthetic scans: peak memory independent of count (10/50/100 within 10 %). (`PERFORMANCE-BUDGET.md`)
- A3 A damaged file in a batch is reported, others complete. (`test_batch.ContinueOnError`)
- A4 Stale results (older generation) never overwrite newer state. (`test_jobs.Generation`)
