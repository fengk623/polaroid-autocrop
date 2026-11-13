# F16 — Libraries (UX-05)

Status: **Accepted** (owner, 2026-11-12, with RFC-0018) · RFC: `rfc/0018-library.md` ·
Units U018 (storage, built in 0.8.0-alpha.1), U019 (home page), U020 (grid / single view, metadata, sort, ratings
and labels) · Tests: `test_library` (`Library.*`), self-test "U018 …"; U019/U020 add theirs.

## Scope
- Several libraries, each isolated (its own folder, catalog, backups, cache); a home page lists them with name,
  cover, scan and print counts, last opened / modified; New, Open, Rename, Recent; double-click enters.
- Inside a library: folders imported from (references, never copies), import / export at the bottom, grid or
  single view (virtualised: only visible cells load thumbnails), information panel (file name, library, copyright,
  author, capture time, rating, pixel size, file size, DPI — unknown shown as unknown; the scan date is never shown
  as the capture date).
- Sort by capture time, added order, edit time / count, rating, pick, text / colour label, file name, pixel size,
  extension, file type, aspect ratio, manual order; ascending / descending; stable, values missing last; ratings,
  labels and manual order persist.
- Scans and their prints as parent and children: a view or filter for each, counts of scans and prints.
- Old `.fkproj` projects: imported as a copy into a new library; the original stays usable.
- Out of scope (UX-05): Capture One catalogs, RAW decoding, cloud libraries, a new grading engine.

## Acceptance
- A1 A new user can: create a library → import → switch grid / single → filter and sort → edit a print (Edit
  workspace) → return to the same selection; reopening keeps ratings, labels and order. (self-test)
- A2 Two libraries are isolated (no scan, rating or cache shared). (`Library.TwoLibrariesShareNothing`)
- A3 100 scans in the grid never decode all originals at once (virtualised; memory measured as in FK-005).
  U020: virtualised grid, thumbnails from the library's cache — self-test "U020 a library opens without decoding
  every scan …" (12 scans: 1 decoded with the cache, 9 without, 25 of 36 cells exist); the 100-scan memory
  measurement on a real device is HV-05.
- A9 Library workspace (U020): prints and scans lists with counts; grid and single view; fourteen sort keys both
  ways, missing values last, stable; filters by rating, pick, colour, text; keys 0–5, 6–9, P / X / U; information
  with unknown shown as unknown and the capture time never the scan date; manual order; everything kept after
  reopening; scan → its prints; folders; double-click edits and G returns to the same print.
  (`LibraryView.*`; self-test "U020 …")
- A4 Old `.fkproj`: migration copy, failure leaves nothing behind, original unchanged, backup kept.
  (`Library.OldProjectsAreImportedAsACopyAndTheProjectIsNeverTouched`, `Library.AFailedImportLeavesNothingBehind`,
  `Library.TheImportPreCheckRefusesWhatCannotBeCopiedSafely`; self-test "U018 an old project is imported …")
- A5 Second app instance on the same library opens it read-only; a stale lock is taken over only after asking.
  (`Library.ASecondInstanceFindsTheLockAndAStaleLockIsTakenOverOnlyWhenAsked`,
  `Library.ALockThatChangedSinceTheUserWasAskedIsNotTakenOver`; self-test "U018 a library open in another
  FrameKeeper …", "U018 an abandoned lock is taken over only after asking")
- A6 Deleting the cache only costs time: reopening rebuilds it and loses nothing. (`Library.DeletingTheCacheOnlyCostsTime`)
- A8 Home page (U019): a fresh start opens the Library workspace; cards show cover, name, scan and print counts,
  last opened and modified; New / Open / Import project; double-click enters; Rename keeps the folder; Remove from
  List keeps the folder; a missing folder is shown as missing; G / D switch workspaces. (`Library.TheHomePageReads…`,
  `Library.RenamingKeepsTheFolderAndWaitsForAnotherInstance`; self-test "U019 …")
- A7 Saving keeps a `.bak` and an hourly history of ten; a damaged catalog is restored from the most recent
  readable copy after asking, the damaged file kept. (`Library.BackupsRotateHourlyAndKeepTen`,
  `Library.ADamagedCatalogIsRestoredFromBakThenTheNewestBackupAndKeptAside`,
  `Library.TheMostRecentlySavedCopyIsOfferedFirst`; self-test "U018 a damaged catalog is restored …")
