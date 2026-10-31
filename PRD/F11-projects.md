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
- A1 Save/load round trip is lossless for every field. (`Project.SaveLoadRoundTripIsLossless`)
- A2 Simulated write failure leaves the previous project intact. (`Project.WriteFailureLeavesNothingAndReportsError`, `DataProtection.AnInterruptedSaveLeavesTheProjectIntact`)
- A3 Files of every earlier release open and keep their content (only schema `fk.project/1` exists, so there is no
  migration step yet). (`Compat.OpensProjectsFromEarlierReleases`, `Compat.ExportRecordsFromEarlierReleasesStillRelink`,
  `Compat.TemplatesFromEarlierReleasesOpen` — files written by 0.5.0 and 0.6.0-alpha.1, `tests/data/compat/`)
- A4 A newer schema opens read-only. (`Project.NewerSchemaOpensReadOnly`)
- A5 Project file for a 4-print scan stays < 200 KB. (Not tested yet — a size test is planned.)
- A6 (FK-014, 0.7.0-alpha.15) Keys a later version wrote survive opening and saving here, at every level; the app
  says when a later version last saved the project; a rollback to the earlier releases keeps what
  `docs/project-format.md` §Compatibility lists. (`Compat.KeysOfLaterVersionsSurviveOpeningAndSaving`,
  `Compat.WhatARollbackToTheLastReleaseKeeps`, `Compat.ProjectRecordsTheVersionThatSavedIt`; self-test
  "FK-014 a project last saved by a later version …")
