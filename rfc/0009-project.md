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
