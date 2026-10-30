# RFC-0017 — Save semantics, recovery snapshots and backups

Status: Accepted (2026-10-30) · Unit U008 · Tickets FK-001, FK-008 (UX-05 later) · Owner's review §3 and D2
("keep manual save; add recovery snapshots/backups; a separate save-semantics RFC before any autosave").

## Decisions

1. **Saving stays manual.** The project file is written only by Save / Save As (and by Save in the "unsaved
   changes" question). Nothing writes the user's project file automatically. Autosave is *not* introduced; doing so
   later needs its own revision of this RFC.
2. **Three different things, kept apart:**
   | | What | Where | Written | Removed |
   | --- | --- | --- | --- | --- |
   | Project file | the user's work | `<name>.fkproj` | Save | never by the app |
   | Backup | the previous **good** project file | `<name>.fkproj.bak` | each successful save (only if the previous file is a readable project) | replaced by the next good one |
   | Recovery snapshot | the open project's unsaved state | app data `recovery/<projectId>.fkrecovery` | ~3 s after a change while unsaved | after a successful save, or when the user chooses "Don't save" |
   A damaged project file never replaces a good backup (the save checks that the previous file reads).
3. **Every replacement asks first (FK-001).** New, Open (menu, shortcut, recent, dropped file), Restore backup,
   Recover snapshot and Quit go through one question when there are unsaved changes or running tasks:
   *Save / Don't save / Cancel* (without unsaved changes: *Continue / Cancel*, naming the tasks that will be
   cancelled). Save without a path opens Save As; cancelling that dialog keeps the project as it is. A failed save
   keeps the project, its path, name and unsaved state, and replaces nothing.
4. **Damaged project (FK-008).** Opening fails with the reason; when the backup reads, the user is offered to restore
   it. Restoring moves the damaged file aside as `<name>.fkproj.damaged-<UTC time>` (kept, never deleted) and opens the
   backup bound to the original path, **unsaved** — the user decides to save.
5. **Recovery (crash, forced quit).** At start, and from File ▸ Recover Unsaved Work…, snapshots of earlier sessions
   are listed (name, time, path or "never saved"): Recover (through the question of §3), Discard, Later. Recovered
   work opens unsaved, bound to its original path. Opening a project that has a snapshot tells the user.
6. **Output parameters travel with the project.** Besides the typed export settings, the output parameters
   (geometry, upright, final mode/format/depth, mask, frame, sidecar option, JPEG quality) are saved in
   `settings.output`, so a reopened project — also on another machine — produces the same outputs. Projects of
   earlier versions keep the current parameters.
7. **Undo history.** Per-print undo/redo stacks are already saved with the project (bounded); no change.

## Atomic write (unchanged, recorded here)
temp file `<name>.fk-tmp-<random>` → flush + fsync → copy the previous good file to `.bak` → rename over the
project. An interruption before the rename leaves the old project untouched; a failed write removes the temp file and
reports "disk full" or "cannot write".

## Not in this unit
Library format (`.fklibrary`, UX-05/U018) with its own migration, two-process and relocation rules; snapshots of
library state.

## Tests
Core `DataProtection.*` ×5 (former `KnownIssue.FK008`; a damaged file never replaces a good backup; restoring keeps
the damaged file; output parameters saved; interrupted save / unwritable folder). Self-test FK-001/FK-008 ×11 (New
and dropped project ask first; Cancel keeps unsaved work; failed Save As keeps path and state; snapshot recovered
unsaved with its path; "Don't save" discards it; output parameters reopened; damaged project offers the backup;
restore keeps the damaged file; restored project saves).
