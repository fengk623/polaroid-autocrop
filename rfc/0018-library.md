# RFC-0018 — Library storage (`.fklibrary`)

Status: **Proposed — waiting for the owner's review** (PLAN §2 #3 and #4: the library format and any change of
save semantics are reviewed before they are implemented) · Unit U018 · Tickets UX-05; FK-001, FK-008 ·
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
- **Recovery snapshots** (unsaved work after a crash): unchanged mechanism (app data `recovery/<projectId>`),
  keyed by the catalog's project id; recovering opens the library unsaved.
- **Restore order** when the catalog cannot be read: `.bak` → newest readable `backups/` entry → read-only open of
  what can be read; the damaged file is kept as `catalog.fkproj.damaged-<UTC>` (RFC-0017 §4).

## 5. Two instances
- Opening for writing creates `library.lock` exclusively (`{pid, host, user, app, startedAt, heartbeatAt}`), the
  heartbeat is refreshed every 60 s and the file removed on close.
- A second instance finding a fresh lock opens the library **read-only** and says who holds it. A lock is stale
  when its heartbeat is older than 3 minutes, or when it names this host and a process that no longer exists;
  then the user is asked before taking it over (never silently).
- Network folders: the lock is advisory (no OS lock semantics are relied on); the same rules apply.

## 6. Old projects (`.fkproj`)
- **Migration copy, never in place**: "Import project into a new library" pre-checks the project (reads, schema,
  scans reachable, size), writes the new library into a temporary folder next to the destination, verifies it reads
  back identically (same scans, prints, records, hashes), then renames it into place. Any failure removes the
  temporary folder and leaves nothing behind; the original `.fkproj` is never modified.
- Relative paths are rewritten for the new location; absolute paths and fingerprints are kept.
- Opening a `.fkproj` directly keeps working as today (proposal: see question Q3).

## 7. Paths and relocation
- Each reference stores the absolute path (native form), a relative path from the library folder with `/`
  separators, and the fingerprint (RFC-0009 §Source identity). On open: absolute path → relative path → missing.
- Missing files are relocated by content (fingerprint), never by name alone — the existing Relocate.
- Moving a whole drive with the library and its scans keeps working through the relative paths (macOS ↔ Windows
  only when the relative paths still resolve; drive letters / volume names are not translated).

## 8. Cache
- `cache/` holds only data that can be rebuilt from the scans (proxies, thumbnails; ADR-0006 keys and cap). Deleting
  it — or a damaged cache — only costs time. No association, rating or edit is ever stored only in the cache.
- Grid virtualisation (U020): only visible cells request thumbnails; no full scan is decoded for the grid.

## 9. Save semantics (decision needed — Q2)
Today saving is manual (RFC-0017 §1). Libraries are long-lived collections; the plan requires a separate review
before any automatic saving (PLAN §2 #3). Proposal for v1.0: **keep manual saving** inside libraries, plus recovery
snapshots and rotating backups; switching between the Library and Edit workspaces only changes the view (no save,
no reload, no cancelled jobs). Automatic commits would be a later revision of RFC-0017.

## 10. Home page registry
- The app remembers known libraries (path, id, name, last opened) in its settings; the home page shows name, cover,
  scan and print counts, opened/modified times, and offers New, Open, Rename, Recent. Removing a library from the
  list never deletes it; deleting a library folder is not offered in v1.0.
- Default location for new libraries: the user's Documents folder (system API, D3), changeable when creating one.

## 11. Tests (planned with the implementation)
Core: manifest read/write and schema versions; unknown keys preserved; atomic write interrupted; backup rotation;
restore order; lock create/fresh/stale/take-over; migration pre-check, failure leaves nothing, original unchanged,
read-back identical; relative-path resolution after moving the library with its scans. Self-test: create library,
import, rate/label/order, save, reopen (kept), second instance read-only, migration of the self-test project.

## Questions for the owner
- **Q1 Format** — a folder `<Name>.fklibrary/` with `library.json` + `catalog.fkproj` (the same project model) +
  backups/lock/cache, as above?
- **Q2 Saving** — keep manual saving inside libraries for v1.0 (recommended), or automatic saving (then a
  save-semantics revision comes first)?
- **Q3 Old projects** — keep opening `.fkproj` directly as today *and* offer "import into a new library" (copy), or
  only the import?
- **Q4 Default location** — Documents (system folder), changeable on creation?
