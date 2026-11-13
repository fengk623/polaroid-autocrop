# RFC-0018 — Library storage (`.fklibrary`)

Status: **Accepted** — the owner answered Q1–Q4 as proposed on 2026-11-12 (PLAN §2 #3 and #4: the library
format and any change of save semantics are reviewed before they are implemented). Storage built in U018
(0.8.0-alpha.1): `core/include/fk/library.hpp`, `tests/core/test_library.cpp`, self-test "U018 …"; the home page
is U019, the review workspace U020 · Unit U018 · Tickets UX-05; FK-001, FK-008 ·
Builds on RFC-0009 (project), RFC-0017 (save semantics), ADR-0005, ADR-0006 (cache).

## 1. What a library is
A library is the **existing project model, kept in a folder with what a long-lived collection needs**: several
libraries side by side, a home page that lists them, ratings/labels/order, a cache that can be deleted, backups
that rotate, protection against two app instances writing at once. It is not a second data model: scans, prints,
boundaries, export records, relinks, masks and undo history are exactly the `fk.project/1` records of today
(PLAN §3.1 "evolve the existing model").

```
Summer 2026.fklibrary/            ← a normal folder (not a macOS package: Finder/Explorer show it as a folder)
  library.json                    ← manifest: schema fk.library/1, id, name, cover, sources, created/opened times
  catalog.fkproj                  ← the library's content: a fk.project/1 document (same reader/writer as today)
  catalog.fkproj.bak              ← previous good catalog (RFC-0017 rule: a damaged file never replaces it)
  backups/catalog-<UTC>.fkproj    ← rotating history: the last 10 good catalogs, at most one per hour of work
  library.lock                    ← present while an app instance has the library open for writing (§5)
  cache/                          ← thumbnails / proxies on disk (ADR-0006): deletable, rebuilt, never unique data
```
Scans and graded files stay where they are: the library **references** them (path + relative path + fingerprint),
never copies, moves or deletes them (UX-05: "folder browsing is not copying or deleting originals").

## 2. Library-level data (new, inside the same documents)
- Per print and per scan, in `catalog.fkproj` (new optional object `library` on items and scans; unknown to older
  versions and preserved by them, `Project.UnknownKeysArePreserved`): `rating` 0–5, `pick` (none / picked /
  rejected), `colorLabel` (none / red / yellow / green / blue / purple), `textLabels` [strings], `manualOrder`
  (integer, stable), `addedAt`, `editedAt`, `editCount`.
- In `library.json`: `cover` (item or scan id; default the first print), `sources` (folders imported from, for the
  left-hand folder list; a source is a reference, removing it never touches the disk), `sort` (last sort key and
  direction per view), `createdAt`, `openedAt`, `modifiedAt`, `app` (version that last wrote it).
- Unknown values are shown as "unknown"; the scan date is never presented as the photo's capture date (UX-05).

## 3. Schema and versions
- `library.json`: `schema: "fk.library/1"`; `catalog.fkproj`: `schema: "fk.project/1"` (unchanged).
- A newer major schema (written by a newer app) opens **read-only** with a notice, as projects do today.
- Additions within a major version are optional keys; readers preserve what they do not know.

## 4. Writing, backups, recovery
- **Atomic write** as today (temp → fsync → rename), for both files; `library.json` is written after
  `catalog.fkproj` and never names something the catalog does not contain.
- **Backups**: `.bak` (previous good file, RFC-0017) plus `backups/` rotation: 10 entries, at most one per hour,
  each a readable catalog; the oldest removed after a new one is written successfully.
- **Recovery snapshots** (unsaved work after a crash): unchanged mechanism (app data `recovery/<key>`), keyed by
  the **library id** — an imported library keeps the project's id (§6), so the project file and the library would
  otherwise share one snapshot. The snapshot names the library folder; recovering opens that library (its lock) with
  the snapshot's content, unsaved.
- **Restore order** when the catalog cannot be read: the readable copies — `.bak` and the `backups/` entries —
  the most recently saved first (by the catalog's own `modifiedAt`; on equal times `.bak` first). A history entry
  copies the catalog as it was saved, so it can be newer than `.bak` (the save before the last); the proposal's
  fixed order "`.bak`, then the newest entry" would have restored the older state in that case
  (`Library.TheMostRecentlySavedCopyIsOfferedFirst`). The user is asked first (FK-008); the damaged file is kept as
  `catalog.fkproj.damaged-<UTC>` (RFC-0017 §4). With no readable copy the library does not open and nothing is
  changed (a JSON document that does not parse has nothing that can be read; the proposal's "read-only open of
  what can be read" does not apply) — `Library.WithNoReadableCopyTheLibraryDoesNotOpenAndNothingChanges`.
- **Damaged `library.json`**: its `.bak` (kept by every save), else rebuilt from the catalog (name from the
  folder; cover, sources and sort start again — nothing else lives only there); the app says which.

## 5. Two instances
- Opening for writing creates `library.lock` exclusively (`O_EXCL` / `_O_EXCL`; schema `fk.lock/1`:
  `{pid, host, user, app, token, startedAt, heartbeatAt}` — the token is random per app session, so a reused
  process id is never mistaken for this session), the heartbeat is refreshed every 60 s and the file removed on
  close. Saving renews it first; a lock that is no longer ours (taken over elsewhere) makes the library read-only
  here, with Save As still available.
- A second instance finding a fresh lock opens the library **read-only** and says who holds it. A lock is stale
  when its heartbeat is older than 3 minutes, or when it names this host and a process that no longer exists;
  then the user is asked before taking it over (never silently). Taking over replaces the lock only if it is still
  the one the user was asked about and still stale; a lock renewed meanwhile is asked about again. A lock file
  that cannot be read (being written) counts as held until it is 3 minutes old.
- Network folders: the lock is advisory (no OS lock semantics are relied on); the same rules apply.

## 6. Old projects (`.fkproj`)
- **Migration copy, never in place**: "Import project into a new library" pre-checks the project (reads, schema,
  scans reachable, size), writes the new library into a temporary folder next to the destination, verifies it reads
  back identically (same scans, prints, records, hashes), then renames it into place. Any failure removes the
  temporary folder and leaves nothing behind; the original `.fkproj` is never modified.
- Pre-check refusals: damaged (restore its backup first), a later schema (import with that version), the
  destination exists, free space below twice the project's size plus 1 MiB, missing parent folder. Missing scans
  are counted and shown, not refused: the library references them as the project did.
- The catalog keeps the project's id (exports carry it in their XMP, so their graded files still relink); the
  library gets a new library id. `sources` lists the folders the scans are in. The history starts with the imported
  state (one `backups/` entry).
- Relative paths are rewritten for the new location; absolute paths and fingerprints are kept.
- Opening a `.fkproj` directly keeps working as today (Q3). Opening `catalog.fkproj` or `library.json` inside a
  library opens the library, never its catalog as a loose project.

## 7. Paths and relocation
- Each reference stores the absolute path (native form), a relative path from the library folder with `/`
  separators, and the fingerprint (RFC-0009 §Source identity). On open: absolute path → relative path → missing.
  Since 0.8.0-alpha.1 every project writes `relPath` with `/` (Windows wrote `\` before); a `\` path written by
  an earlier Windows version still resolves on macOS and Linux.
- Missing files are relocated by content (fingerprint), never by name alone — the existing Relocate.
- Moving a whole drive with the library and its scans keeps working through the relative paths (macOS ↔ Windows
  only when the relative paths still resolve; drive letters / volume names are not translated).

## 8. Cache
- `cache/` holds only data that can be rebuilt from the scans (proxies, thumbnails; ADR-0006 keys and cap). Deleting
  it — or a damaged cache — only costs time. No association, rating or edit is ever stored only in the cache.
- Grid virtualisation (U020): only visible cells request thumbnails; no full scan is decoded for the grid.

## 9. Save semantics (Q2: manual saving kept)
Today saving is manual (RFC-0017 §1). Libraries are long-lived collections; the plan requires a separate review
before any automatic saving (PLAN §2 #3). Proposal for v1.0: **keep manual saving** inside libraries, plus recovery
snapshots and rotating backups; switching between the Library and Edit workspaces only changes the view (no save,
no reload, no cancelled jobs). Automatic commits would be a later revision of RFC-0017.

## 10. Home page registry
- The app remembers known libraries (setting `recentLibraries`, folders, newest first; U018) in its settings; the
  home page (U019) shows name, cover,
  scan and print counts, opened/modified times, and offers New, Open, Rename, Recent. Removing a library from the
  list never deletes it; deleting a library folder is not offered in v1.0.
- Default location for new libraries: the user's Documents folder (system API, D3), changeable when creating one.

## 11. Tests (U018)
Core (`test_library`, `Library.*`): times; folder names; create; newer schemas read-only; ratings/labels/order and
unknown keys; projects without library data unchanged; backups hourly, ten kept; interrupted save; restore order and
damaged file kept; most recent copy first; no readable copy; damaged manifest; second instance, stale lock, take
over; ended process; lock changed while asking; unreadable lock; migration copy (project and `.bak` byte-identical);
failed migration leaves nothing (write, verify, rename); pre-check refusals; library moved with its scans
(and Windows separators); two libraries isolated (A2); cache deleted (A6). Self-test (`runLibrary`, scenario
`library`): ten checks named "U018 …" through the menus' replace path, drag and drop, the stale-lock dialog,
restore and recovery.

## Questions for the owner — answered 2026-11-12: all as proposed
- **Q1 Format** — a folder `<Name>.fklibrary/` with `library.json` + `catalog.fkproj` (the same project model) +
  backups/lock/cache, as above?
- **Q2 Saving** — keep manual saving inside libraries for v1.0 (recommended), or automatic saving (then a
  save-semantics revision comes first)?
- **Q3 Old projects** — keep opening `.fkproj` directly as today *and* offer "import into a new library" (copy), or
  only the import?
- **Q4 Default location** — Documents (system folder), changeable on creation?
