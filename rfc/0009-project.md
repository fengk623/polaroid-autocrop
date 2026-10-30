# RFC-0009 — Project and templates

Status: Accepted · Module: `core/project` · ADR-0005 · PRD F09, F11 · Tests: `tests/core/test_project.cpp`, `test_templates.cpp`

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
