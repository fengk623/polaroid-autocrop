# F16 — Libraries (UX-05)

Status: **Proposed — waiting for the owner's review together with RFC-0018** · RFC: `rfc/0018-library.md` ·
Units U018 (storage), U019 (home page), U020 (grid / single view, metadata, sort, ratings and labels) ·
Tests: planned with each unit.

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
- A2 Two libraries are isolated (no scan, rating or cache shared). (core + self-test)
- A3 100 scans in the grid never decode all originals at once (virtualised; memory measured as in FK-005).
- A4 Old `.fkproj`: migration copy, failure leaves nothing behind, original unchanged, backup kept. (core)
- A5 Second app instance on the same library opens it read-only; a stale lock is taken over only after asking. (core)
- A6 Deleting the cache only costs time: reopening rebuilds it and loses nothing. (core + self-test)
