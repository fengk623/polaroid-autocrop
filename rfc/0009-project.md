# RFC-0009 — Project and templates

Status: Accepted · Module: `core/src/project.cpp` · ADR-0005 · PRD F09, F11 · Tests: `tests/core/test_project.cpp`

- In-memory model: `Project { id, name, schema, settings, scans[] }`, `Scan { id, path, relPath,
  fingerprint, info, detection params, items[] }`, `Item { id, index, label, filmType, outer, inner,
  upright, safetyMarginPx, reviewed, excluded, confidence, reasons, edits[], composite, history }`.
- `Result<Project> loadProject(path)`; `Result<void> saveProject(const Project&, path)` (atomic, `.bak`);
  `migrate(json)` chain; `ProjectOpenMode::ReadOnly` for newer schemas.
- Fingerprint: size, mtime, SHA-256 of first 1 MiB + last 1 MiB + size ("quick"), optional full SHA-256.
- Relocation: `relocate(project, folder)` matches by file name then quick fingerprint.
- Edit history: bounded undo stack per item (100 entries) persisted as compact diffs.
- Templates: `Template { schema, id, name, createdDpi, scanSizeMm, slots[] }` load/save JSON, `applyTemplate()`.
- Package project: copy referenced scans + graded files into a folder with rewritten relative paths.

## Export records (v0.6.0-alpha.3, U003, UX-07)

Status: Accepted (2026-10-30). The geometry, colour and identity of every export are stored in the project
(`Project::exportRecords`, one `Sidecar` per `editVersionId`, top-level JSON key `exportRecords`), so relinking and
compositing do not depend on a `.fk.json` beside the graded file. API: `findExportRecord(project, editVersionId)`,
`exportRecordFor(project, editVersion)` (record, else the portable sidecar file), `importSidecarRecords(project)`
(called by `loadProject`). The portable sidecar is an option (setting `writeSidecar`, default off).

Why no schema bump: the plan for U003 said `fk.project/2` + migration copy. A top-level key in `fk.project/1` gives
the same result without an incompatible change — older versions already preserve unknown top-level keys
(`Project.UnknownKeysArePreserved`), so a project saved by 0.6.0-alpha.3 can still be opened, edited and saved by
0.5.0 without losing the records, and no migration copy is needed. Records the reader cannot parse (a future record
schema) are kept verbatim. Tests: `ExportRecords.*` in tests/core/test_project.cpp; self-test "export records in
the project", "reopened project relinks without a sidecar", "portable sidecar written when enabled".

Not solved here: a project that was never saved loses its records when the app quits (crash-recovery snapshots,
U008), and graded files with no usable evidence still cannot be relinked (target-first relink, U004).

## Source identity (v0.6.0-alpha.7, U007, FK-007)

Status: Accepted (2026-10-30), implemented in U007. Before: a scan replaced at its recorded path (same name, same
size, other content) was trusted as the original (known issue `FK007_ReplacedScanAtSamePathIsNotTrusted`); a graded
file overwritten after relinking was composited without any check.

- **Import** hashes the whole scan once, streamed, in the detection job (`fingerprintFile(path, full, cancel)`):
  `fingerprint.fullSha256`. Projects from earlier versions get it the next time the scan is opened.
- **Verification** (`verifyFileIdentity`): size → first/last MiB → whole-file SHA-256 when known; result
  same/missing/changed with a reason code (size, quick, full). Never on the UI thread: it runs in the jobs that open a
  scan (preview, detection) and in every output job (image areas, full prints, final) before the scan is read.
  `resolveScanPaths` (on load) compares only the size (no reading); `verifyScans` streams all.
- **Changed scans** (`ScanRecord::changed`, runtime) are not loaded or detected again (detection of an already
  detected scan verifies first: its exports refer to the old file); the scan list shows "changed" with the reason;
  Relocate looks for the original by fingerprint like for a missing scan.
- **Time of check vs. time of use**: each output job takes the scan's `FileStamp` (size + mtime in ns) when it
  verifies and compares it after reading; a change in between discards the output ("changed while it was read").
  The same applies to the graded file between hashing and reading.
- **Graded files** (final output, composite preview): the file's SHA-256 is compared with the relinked hash; when it
  differs, `recheckGraded` compares with what was confirmed: same size, the picture still matches at the confirmed
  turn (≥ 0.80), the same embedded profile (recorded since this version). Same picture → a normal re-grade: used, the
  new hash recorded, the user told. Otherwise refused with the reason (size, picture, profile) — relink again.
  Relinks of versions before 0.6.0-alpha.5 (no confirmed transform) are refused when their file changed.
- Renamed or moved scans are found only by content (`relocateScans`, fingerprint), never by name alone.

Limits: Relocate still fingerprints candidate files on the UI thread (first/last MiB each, as before).
Tests: core `SourceIdentity.*` ×5 (replaced at same path — the former known issue; middle-of-file edit seen only by
the full hash; renamed/moved found by content with a same-name decoy; stamp; graded re-check), self-test FK-007 ×6
on a copy of the scan (whole-file hash at import; re-graded file used and hash updated; replaced picture refused with
the reason; middle-byte change refused with the reason; changed scan shown as changed after reopening).
