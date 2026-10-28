# F11 — Non-destructive projects

RFC: `rfc/0009-project.md`, ADR-0005, `docs/project-format.md` · Tests: `tests/core/test_project.cpp`

## Scope
- File `Name.fkproj` (JSON, `fk.project/1`). Stores references, fingerprints, boundaries, matrices,
  masks, settings, sidecar references, edit history. Thumbnails (≤ 256 px) live only in the disk cache (ADR-0006), never in the project file.
- Never stores copies of scans, full-resolution previews or graded files (unless the user runs "Package project").
- Atomic save (temp → flush → rename), previous version kept as `.fkproj.bak`; autosave every 2 minutes and on quit.
- Corruption: falls back to `.bak` with a message.
- Missing sources: relocate by folder (name + fingerprint).
- Newer unknown schema: open read-only with an explicit message.
- External graded versions list; revert item to original; conflict detection when a graded file changed since relink (SHA-256).
- Project panel shows: project file size, cache size, size of referenced external files, estimated packaged size.

## Acceptance
- A1 Save/load round trip is lossless for every field. (`test_project.RoundTrip`)
- A2 Simulated write failure leaves the previous project intact. (`test_project.WriteFailure`)
- A3 Migration tests for every schema version. (`test_project.Migration`)
- A4 Future version opens read-only. (`test_project.FutureVersion`)
- A5 Project file for a 4-print scan stays < 200 KB. (`test_project.Size`)
