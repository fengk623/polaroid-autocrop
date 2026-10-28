# ADR-0005 — Project file format

Status: Accepted · Date: 2026-10-28

## Decision
Single UTF-8 JSON file `*.fkproj`, schema id `fk.project/<n>` (n integer, starts at 1). Reasons: atomic
replace is trivial (temp + rename) on every OS including mobile sandboxes; diffable; no SQLite dependency;
a 100-scan project is < 2 MB. Thumbnails live in the cache, not in the project.
Migrations are pure functions `vN → vN+1`, each with a fixture test. Unknown newer versions open read-only.
Details: `docs/project-format.md`.

## Consequences
Whole-file rewrite on save (fast at this size). Concurrent editing of one project by two app instances is
detected (mtime + content hash check before save) and refused with a message.
