# v1.0 worklog

Every effective step of the v1.0 work, in time order (UX-12). One entry per step: time (America/Chicago),
requirement IDs, change or decision, files, commit, commands/tests, actual result, failures and blockers.
Entries are written when the step happens, never afterwards; a step that failed stays recorded as failed.
No secrets, no private pixels, no private paths beyond "owner's scan <name>".

Status words: done · failed · blocked · not started · hypothesis (not yet verified) · confirmed.

---

## 2026-10-30

### 14:38 — Stage A start
- Req: all (plan), UX-12
- Input: `FrameKeeper_v1.0 需求文档` (26 items: UX-01…UX-12, FK-001…FK-014), `framekeeper测试.docx` (9 feedback
  groups, 24 images), owner's plan review (conditional approval, 12 required changes).
- Decision: Stage A may start now (version registration, reproduction, failing regression tests, feature freeze,
  RFCs). Implementation touching save semantics, library migration, relink matching and default output behaviour
  waits for the revised plan's approval.
- Branch `v1/stage-a` created from `origin/release/v0.5.0` (65e209a). Reason: it carries the CI fix for the
  retired macos-14 images (without it no macOS job can run). PR #2 itself (0.5.0) is still unmerged; whether
  0.5.0 is published is decided separately (review, D1).
- Result: done.

### 14:40–14:44 — Reproduction of reported problems (Linux, offscreen app, owner's real scans)
- Req: UX-07, UX-02, UX-01, UX-04 (evidence for Stage A)
- Change: `FrameKeeper --self-test <dir> --scan <tif> --scenario relink|count|upright` (app/src/SelfTest.cpp,
  main.cpp): drives the real controllers through the reported flows and records observations (not assertions)
  in `scenario-<name>.json`. Test tooling only; no product behaviour changed.
- Build: local, branch v1/stage-a (uncommitted at the time), version 0.5.0.
- Results (status in brackets):
  - Fig 14 relink [confirmed]: on owner's scan polaroid_92 (1200 DPI, 8-bit, Adobe RGB): exported P01 then
    - R1 unchanged export (XMP + sidecar): matched by XMP, ok.
    - R2 same pixels re-written without XMP, renamed `graded_in_editor.tif`, no sidecar: "No matching print (no
      FrameKeeper metadata, sidecar or file name)." — the message of Fig 14; the dialog offers no way to pick the
      target print (checkbox disabled).
    - R3 export name in another folder, no XMP: matched by file name, ok.
    - R4 name with editor suffix `_1`, no XMP: no match.
    - R5 JPEG with the export name, no XMP: matched by file name, ok.
    - R6 project never saved, app restarted, scan imported again, unchanged export (XMP and sidecar beside it):
      matched by file name but "The export sidecar (.fk.json) is missing; geometry unknown." — the IDs in XMP and
      sidecar belong to the lost session, so neither is used.
    - Source: `findMatch` (core/src/project.cpp ~739) resolves only by item UUID (XMP/sidecar) or by the exact
      `<scan>_Pnn_EDIT_vNNN` name; `relinkFiles` (app/src/AppController.cpp ~1590) has no target-first path.
  - Fig 14 empty file name in the dialog row [not reproduced on Linux]: the name is shown (snapshot R2-dialog).
    Hypothesis: Windows-specific; needs the owner's installed build and file. Stays open.
  - Fig 5 white edge in "image only" export [confirmed]: polaroid_92 (the lake photo of Fig 5), pixel-crop and
    straighten exports both keep frame white: top ≈ 9–10 px, right ≈ 6–8 px, left ≈ 4–5 px, bottom ≈ 6–10 px
    (1200 DPI ⇒ 0.1–0.2 mm), measured on mean RGB profiles over the central 60 % of each side. Straightening does
    not remove it, so the inner quad itself lies outside the image window (not only the axis-aligned crop). The
    cream reagent band at the bottom lies inside the image window (open question: part of the picture).
  - Fig 1 stale count warning [confirmed]: detect with default 4 → warning `count_mismatch: found 1, expected 4`;
    setting the count to 1 keeps the warning (warnings are computed only at detection; the scan keeps
    expectedCount from import time).
  - Fig 6 rotate "does nothing" [confirmed]: `rotateUpright` changes the export orientation only (rail shows 90°);
    canvas pixels unchanged.
  - Fig 4 outer on the shadow band [confirmed on a comparable scan]: owner's img20260702_0005 (800 DPI 4-up,
    white frames on white lid): the outer line lies ≈ 25–28 px (≈ 0.8–0.9 mm) outside the paper surface, on the
    bright edge band. The Fig 4 scan itself (img20260702.tif, 679 MB) is not available here [blocked].

### 14:45 — Owner's unit-based authorisation
- Decision (owner): complete all 26 items autonomously, unit by unit: branch → PR → CI + PR test packages →
  merge when conditions are met → unique tag → GitHub pre-release → update unit list and worklog → next unit.
  Ask only for requirement conflicts, removal/downgrade, incompatible data changes, destructive operations, new
  permissions/costs, unconfirmed major product decisions. No bypassing of repository protection or approvals.
- Plan: v0.5.0 (PR #2) is the frozen baseline; units start at v0.6.0-alpha.1.

### 14:46 — v0.5.0 baseline notes (PR #2)
- Req: D1 (review), FK-014
- Change: docs/release-notes/v0.5.0.md lists the issues found in the owner's testing (EN + ZH). Commit 492de9e on
  `release/v0.5.0`, pushed. PR #2 checks on 492de9e: all success (CI core/app ×3 platforms, release packaging ×2).
- Result: done.

### 14:50 — Merge PR #2 → v0.5.0 frozen baseline
- Merged PR #2 via the GitHub API (owner's unit authorisation): merge commit `ce93e057a0e60485075d41bbac912b59919f48db`.
- Release workflow dispatched on main (run 41119771659, old release.yml: creates a draft, published afterwards as a
  pre-release). Result: pending at the time of writing.

### 14:50–15:05 — U001 implementation (branch `unit/U001-foundation`, based on 492de9e, to be rebased on ce93e05)
- Req: UX-12, FK-013, FK-014 (+ evidence for all)
- Build provenance: VERSION accepts SemVer pre-release (`0.6.0-alpha.1`); CMake splits numeric/pre-release, embeds
  `FK_GIT_SHA` (git rev-parse, `-dirty` when local changes) and `FK_BUILD_ID` (CI run.attempt or "local") into
  `fk/product_info.hpp`; `fk::buildCommit()/buildId()/buildInfoLine()`; About dialog (selectable text), `--version`,
  self-test reports, Info.plist (FKVersion/FKCommit/FKBuild), Windows VERSIONINFO comment. CMake re-runs when VERSION
  changes. Files: cmake/ProductInfo.cmake, CMakeLists.txt, core/include/fk/{product_info.hpp.in,version.hpp},
  core/src/version.cpp, app/CMakeLists.txt, app/packaging/{macos/Info.plist.in,windows/app.rc.in},
  app/src/{AppController,main,SelfTest}.*, app/qml/AboutDialog.qml, zh_CN (+2 strings).
- Release pipeline: release.yml builds PR test packages from the PR head SHA, passes FK_GIT_SHA/FK_BUILD_ID, writes
  BUILDINFO.txt into DMG/app/stage (tools/release/buildinfo.sh), checks the packaged app's self-test report against
  VERSION and the source SHA (tools/release/check-report.py), uploads per-platform SHA256SUMS; publish job (manual
  run with publish) creates tag v<VERSION> + release (pre-release when VERSION has a suffix), refuses existing
  tags/releases, appends commit/build/compare link/SHA-256 to the notes, downloads the published files and verifies
  checksums and the tag commit. The tag-push trigger was removed (single release path).
  - Mistake on the way: a first scripted edit matched the indented `publish:` input instead of the job and
    truncated release.yml; restored from git and re-applied with an exact anchor (verified: 269 lines, jobs
    macos/windows/publish).
- Known-issue tests: tests/core/test_known_issues.cpp (FK-004, FK-007, FK-008), registered one ctest each with
  WILL_FAIL, label `known-issue`. Verified each fails for the stated reason (`bin/test_known_issues`: FK004 XMP still
  contains dc:creator; FK007 replaced file accepted; FK008 error text has no ".bak"). App self-test `known()` checks
  (status reproduced / NOT_REPRODUCED → run fails): UX-01 stale warning; UX-07 renamed/no XMP, `_1` suffix, new
  session; FK-010 16→8-bit claims bit-identical; FK-002 180° graded file composited unrotated (content check
  suggested 180°, final ≠ unrotated print, "row 75 differs"); scenario `miniswhite`: FK-011 preview mean 15.8 vs
  full 239.2 on an fkfixture MinIsWhite 16-bit scan. Local run (Linux, synthetic scan): self-test passed, all 7
  app known issues "reproduced". CI app job runs the miniswhite scenario too.
- Observation (source, to handle in U004): the window drop handler imports any dropped TIFF whose name lacks
  `_edit_v` as a new scan, so a renamed graded TIFF dropped on the window becomes a scan instead of a relink.
- Docs: docs/v1/{PLAN,UNITS,TRACEABILITY,BASELINE,FEATURE-FREEZE,PROCESS-CHECKLIST,FORMAT-MATRIX,REAL-SCAN-GT,
  USER-GUIDE.zh-CN}.md; docs/release-notes/v0.6.0-alpha.1.md. docs/reference-audit.md row ids renamed FK-nnn → SL-nnn
  (374 occurrences) so they cannot be confused with the requirement tickets FK-001…FK-014.
- Checks: check-docs, check-repo, check-licenses, check-no-network, check-i18n (480 strings), check-format: ok.

### 15:08 — U001 PR
- PR #3 opened (`unit/U001-foundation`, head 7a97934 after rebase on ce93e05). Checks running.

### 15:10 — v0.5.0 baseline release
- Release run 41119771659 (old workflow) succeeded: macOS universal DMG 64 418 114 B, Windows portable ZIP
  59 085 479 B, Setup.exe 48 012 621 B, SHA256SUMS. It created a **draft** v0.5.0 (target ce93e05).
- Publishing it from this session failed: `PATCH /releases/429444571` → HTTP 403 "Creating, editing, or deleting
  releases is not permitted for this session type." [blocked → owner]. Not worked around; the owner was asked to
  publish the draft as a pre-release. From U001 on, the release workflow (Actions token) creates published
  pre-releases itself, which is the pipeline the owner authorised.

### 15:00–15:18 — U002 task safety (branch `unit/U002-task-safety`, on top of U001)
- Req: FK-006 (part 1); related UX-07, UX-09, FK-005
- Change: Jobs: states queued/running/succeeded/failed/cancelled/outdated, `JobContext::fail`, `done` returns
  `JobDone{applied|outdated|failed, message}`, late progress never reopens a finished job, test delay hook,
  `finalState(id)`. AppController: `JobSnapshot` generations (project/scan/item/composite), `invalidateItem` replaces
  the scattered preview erasures and is also called by upright rotation and scan removal; all nine completion
  handlers check freshness (table in RFC-0010 §Task safety); composite previews coalesce per print; mask settings bump
  the composite generation. LeftRail job list shows every state and failure messages. tools/check-i18n.py now joins
  adjacent C++ string literals (it missed a split `tr()` string). 6 new zh strings.
- Tests: self-test TS1–TS4 (Linux local): all pass; existing self-test checks and the 7 known issues unchanged.
  Mutation run with both freshness checks disabled (`false &&`): TS1 FAIL "job succeeded, preview stored 1", TS2
  FAIL "job succeeded, corner x 260.589 (edited to 263.589)" — the old behaviour silently replaced the user's edit.
  Code restored (no MUTATION markers left; verified by grep).
- Docs: RFC-0010 §Task safety, release notes v0.6.0-alpha.2, USER-GUIDE §8, UNITS, TRACEABILITY.

### 15:20–15:45 — U001 merge/release, U002 PR, U003
- PR #3 (U001) checks all green incl. packaging and the packaged-app version/commit check (release run 41120596615,
  artifacts windows 105.9 MB, macos 60.0 MB). Merged → `3c009f881254fd3494861e63f2a1a636640245fe`.
- Release run 41122756426 (publish, main 3c009f8): Windows packaging ok; macOS failed at `hdiutil: create failed -
  Resource busy` (known intermittent runner problem) [failed]. Re-ran the failed jobs on the same commit (no new
  tag elsewhere). Added an hdiutil retry to tools/release/package-macos.sh on the U002 branch (484555e), trap T19.
- U002 rebased on main, PR #4 opened.
- U003 (branch `unit/U003-export-records`): export records in the project. Decision: no schema bump — a top-level
  `exportRecords` key in fk.project/1 is preserved verbatim by every earlier version (unknown top-level keys), so the
  change is backward and forward compatible and needs no migration copy (UNITS U003 updated, RFC-0009 §Export
  records). Core: sidecar JSON helpers shared by files and records, `findExportRecord`, `exportRecordFor`,
  `importSidecarRecords` (on open). App: export writes the record (sidecar file only with the new `writeSidecar`
  setting, default off), composite preview and final export read the record, relink message for a missing record,
  Settings checkbox, export dialog text; 6 zh strings.
- Tests: core `ExportRecords.*` ×4 pass; full core suite 144/144 (incl. 3 known-issue WILL_FAIL); app self-test 33
  checks pass incl. 4 new U003 checks; 7 known issues still reproduced (UX-07 "new session" message changed to the
  missing-record text — still reproduced, unsaved project).

### 15:45–16:20 — releases, PR #4/#5, process change, U004
- v0.6.0-alpha.1: release run 41122756426 re-run succeeded; tag `v0.6.0-alpha.1` → `3c009f8`, pre-release with DMG,
  Setup.exe, portable zip, SHA256SUMS, BUILDINFO ×2; publish job verified the SHA-256 sums. v0.5.0 draft was published
  as a pre-release by the owner (tag → `ce93e05`).
- PR #4 (U002) all checks green incl. test packages on head `484555e`. Merging it through the API was refused by this
  session's permission check (PR #2/#3 had worked); not retried by other means (authorisation forbids bypassing access
  limits) — asked the owner, who merged it → `7413e55`.
- Release run 41128749850 (v0.6.0-alpha.2) dispatched, then **cancelled before publishing** after the owner's change
  at 16:13: *no release per Unit; keep the other steps; package and release once all tasks are done.* No tag or
  release was created for alpha.2. PLAN §4, UNITS, TRACEABILITY and the unit notes updated accordingly; VERSION keeps
  increasing per Unit for traceable PR test packages.
- U003 rebased onto `7413e55` (→ `cd2b1a3`), pushed, PR #5 opened; checks green so far (docs, core ×3, app ×3).
- U004 (branch `unit/U004-relink-evidence`), UX-07 main part — relink v2 (RFC-0006 §Relink v2, rewritten to match):
  - Core (`core/src/relink_assess.cpp`): `describeGradedFile`; `assessRelink` scores per print × export version
    (hash +10, IDs ±3 (+1 version), name ±1 (+0.5 version), content +2…+4 / −2 below 0.50), marks conflicts, decides
    unique / ambiguous / none (unique needs hash, IDs/name + content, or target + content; no uncontradicted
    competitor); target restricts to that print and adds a new-alignment option when none of its exports matches;
    `alignmentGeometry` (exact size → closer aspect → picture), `signatureOfExport` (box-filtered sampling of the
    preview through the export geometry incl. upright), `alignmentRecord` (`origin: "manual_alignment"`, next version,
    no export date). Sidecar JSON writes `origin` only for manual alignments. Editor suffix `_n` stripped.
  - Design refinements, recorded in RFC/UNITS: aspect tolerance for a new alignment 0.5 % (the compositing scale
    tolerance) instead of the draft's ±2 % — anything wider could only be placed by stretching; shape chosen by exact
    size, then aspect, then picture (a first version always used the straightened shape and scored 74–75 % against a
    pixel-crop export of a tilted print [failed] — found by the self-test, fixed).
  - App: `relinkFiles(urls, target, update)`, `relinkTargets`, `dropFiles` (graded TIFFs by our IDs/export name go
    to relink, other TIFFs import), `applyRelink` (new alignment adds the record + edit version; skipped if the print
    changed since the check); dialog with candidates, evidence ✓/✗, "Assign to …" re-check; Return tab "Relink to
    this print…"; relink commands enabled whenever prints exist. 31 zh strings.
  - Tests: core `RelinkV2.*` ×12 pass; full core suite 157/157. App self-test (Linux, fkfixture scan): all pass; the 3
    UX-07 known issues became regular checks (9 checks) incl. "new session: unchanged export through a new alignment
    is bit-exact"; remaining known issues UX-01, FK-010, FK-002 (+FK-011 scenario) still reproduced. Relink scenario:
    R1 unique (hash), R2 ambiguous with 1 candidate, R3/R4 unique (name + picture), R6/R6b none (choose the print).

### 16:15–16:35 — PR #5 merged, PR #6, U004 fix, U005
- Owner merged PR #5 (U003). PR #6 (U004) opened against main (stacked on U003); all checks green on `76428d0`.
- Owner asked whether a PR can be merged once all checks pass: yes — docs/repo, core ×3, app ×3 and both test
  packages green ("Publish release" is skipped on PRs by design).
- U004 real-scan run (owner's scans, Linux, offscreen): all relink v2 checks pass on polaroid_92 (1200 dpi) and
  img20260702_0005 (800 dpi 4-up). Found: on polaroid_92 the new alignment chose the straightened shape for an
  unchanged pixel-crop export because both shapes had the same size [failed: composite resampled, "row 259
  differs"]. Fix `76428d0` on the U004 branch: the pixel crop wins a size tie → re-run bit-exact [passed].
- The same runs flagged two known-issue checks as "no longer reproduces" although their precondition was not met
  (8-bit scans for the 16-bit FK-010 issue; no count warning for UX-01). Not a fix — the checks now record
  "not applicable" (U005).
- U005 (branch `unit/U005-relink-transform`), FK-002: `checkEdited` limits turns by size, the picture decides only
  when unambiguous (pass + lead ≥ 0.15), otherwise `choose_rotation`; ticking confirms turn/scale/lower depth, stored
  as `edits[].relinked.transform`; preview and output use it as stored (root cause of FK-002:
  `sizeMatches ? 0 : suggestedRotate` ignored a 180° turn of a same-size file). Relink dialog "Orientation" combo;
  rail shows the stored turn; final dialog asks only for relinks of earlier versions. Dialog combo selection now
  follows the model (U004 snapshot showed "Choose the print…" on a unique row) [U004 bug, fixed here].
- Tests: core `RelinkTransform.*` ×3, full core suite 160/160. Self-test (fixture): all pass; FK-002 known issue →
  5 checks (turn found + confirmation required, stored, final bit-exact with the full print, kept after
  save/reopen, still bit-exact). Real scans: polaroid_92 47 PASS / UX-01 known / FK-010 n/a; img20260702_0005 all
  pass, UX-01 and FK-010 n/a. Relink scenario R7 (turned 180°) snapshot checked.

### 16:35–16:50 — PR #6/#7 merged, U006
- Owner merged PR #6 (U004) and PR #7 (U005).
- U006 (branch `unit/U006-missing-icc`), FK-003. Root cause: `req.editedIcc = ei.icc` — a graded file without a
  profile was composited as if in the scan's colour space; an unreadable profile failed only at output. Core:
  `checkEdited` sets `iccInvalid`; `iccChoicesFor` (export profile only by SHA-256 among scan/built-ins/stored,
  then scan, sRGB, Adobe RGB, deduplicated); `gradedProfile` (confirmed → stored bytes; usable embedded; untagged
  export → none; earlier-version relink → identified export profile; else error); project top-level `iccProfiles`
  (base64, SHA-256 verified on load); `base64Encode/Decode` in util. App: relink rows carry the choices (export
  profile pre-selected only for a missing, identified profile), `loadIccProfile` for .icc/.icm files,
  `applyRelink` refuses an unconfirmed profile and stores the choice + bytes; composite preview and final output
  use `gradedProfile`. Dialog "No colour profile; read the colours as" + "Profile file…". tools/check-i18n.py now
  joins adjacent literals in core `makeError` messages too (a split message was reported half).
- Tests: core `GradedProfile.*` ×3 — Adobe RGB scan → sRGB export → profile dropped: confirmed interpretation mean
  error 0.0000 vs 0.0096 when assumed to be the scan's space (first draft of the test used saturated colours that
  sRGB clips: 0.024 [failed] — test changed to in-gamut colours, which is what the claim is about); frame unchanged.
  Core suite 163/163. Self-test (fixture, sRGB scan, export converted to Adobe RGB, profile dropped): 4 FK-003 checks
  pass, final vs full print mean difference 0.00000; all other checks pass.

### 16:50–17:10 — PR #8 merged, U007
- Owner merged PR #8 (U006).
- U007 (branch `unit/U007-source-identity`), FK-007. Root cause: `resolveScanPaths` trusted any file at the recorded
  path; output jobs never checked the scan; a relinked graded file was read without comparing its hash. Core:
  `fingerprintFile(path, full, cancel)`, `FileStamp`/`fileStamp`, `verifyFileIdentity` (size → first/last MiB →
  whole file), `verifyScans`, runtime `ScanRecord::changed/changeReason` (size compared on load without reading),
  `relocateScans` also for changed scans, `recheckGraded` (size, picture at the confirmed turn, embedded profile —
  recorded from now on, `gradedIccRecorded`). App: detection job hashes the whole file at import and verifies an
  already detected scan first; preview job verifies (and adds the whole-file hash to older projects); `ScanGuard` in
  the image-area, full-print and final jobs verifies before reading and compares stamps after (output removed when
  the file changed meanwhile); graded file hashed + stamped in final and preview, re-graded files used with the new
  hash recorded and a notice, others refused with the reason; "changed" state in the scan list, canvas, Relocate.
- Known issue `KnownIssue.FK007_ReplacedScanAtSamePathIsNotTrusted` moved into `SourceIdentity.*` (WILL_FAIL list
  updated). First version of the self-test refusal checks would also have passed if the export had failed for another
  reason (a tampered LZW file may not decode) — the checks now also require the user-visible reason [tightened].
- Tests: core `SourceIdentity.*` ×5 pass; core suite 167/167. Self-test (fixture, on a copy of the scan): all pass incl.
  6 FK-007 checks; the original scan file is never modified.

### 17:10–17:25 — PR #9 green, U008
- PR #9 (U007): all checks green incl. both test packages; waiting for the owner's merge.
- U008 (branch `unit/U008-data-protection`), FK-001 + FK-008; RFC-0017 (save semantics; manual save stays).
  Root causes: only the window's close asked about unsaved changes (New/Open/drop replaced the project directly);
  `saveProjectAs` changed the project path before saving, so a failed Save As left a wrong path; a damaged project
  only reported "damaged"; every save copied the previous file to `.bak`, so saving over a damaged file would
  replace a good backup with it.
  - Core: `usableBackup`, `setAsideDamaged`, loadProject error detail names the backup, save rotates the backup only
    when the previous file reads, `settings.output`.
  - App: `replaceInfo`, `adoptProject`, `restoreBackup`, recovery snapshots (`recovery/<projectId>.fkrecovery`,
    written ~3 s after a change while unsaved, removed after save or "Don't save", listed at start and in File ▸
    Recover Unsaved Work…), Save As restores path/name/read-only on failure, dropped projects go to QML via
    `openRequested`, `JobRunner::activeCount`.
  - QML: one question (Save / Don't save / Cancel; Continue / Cancel with running tasks) for New, Open, recent,
    drop, restore, recover and Quit; damaged-project and recovery dialogs.
- Tests: core `DataProtection.*` ×5 (incl. the former `KnownIssue.FK008`, WILL_FAIL list now only FK-004); core
  suite 171 pass. Self-test (fixture): 11 FK-001/FK-008 checks pass, all others pass. Read-only/disk-full simulated by
  an unwritable path (the container runs as root, so permission-based read-only cannot be simulated reliably).

### 17:25–17:40 — PR #10 green, U009
- PR #10 (U008): all checks green incl. test packages; waiting for the owner's merge (after #9).
- U009 (branch `unit/U009-privacy`), FK-004. Root cause: `buildXmp` copied `dc:creator` from the source XMP for every
  preset; `applyPrivacy` cleared only the TIFF Artist/HostComputer. Fix: the filtered `MetadataSet` carries its
  preset and `buildXmp` copies the creator only when identity is kept; `writtenMetadata(filtered, TagChannel)` states
  per channel what an output carries (DNG Make/Model are FrameKeeper's own — found by the read-back test, which first
  expected the scanner make in DNG [failed]; the list now says what DNG really writes). Export tab lists the written
  fields for the current scan/preset/format and what is never written. Scan metadata cached in the preview/detection
  jobs.
- Known issue `KnownIssue.FK004_*` moved into `Privacy.*`; no core known issues remain, `test_known_issues` removed
  (CMake comment says how to add one again).
- Tests: core `Privacy.*` incl. byte-level read-back of TIFF/PNG/JPEG/DNG × 3 presets; mutation (always copy the
  creator) → 3 failures, restored (no MUTATION markers). Core suite 173/173. Self-test: 2 FK-004 checks on a copy of
  the scan carrying identity (preview lists identity only when kept; TIFF/PNG/JPEG exports carry none).

### 17:40–17:55 — PR #11 green, U010
- PR #11 (U009) all checks green. PRs #9, #10, #11 wait for the owner's merge, in order.
- U010 (branch `unit/U010-export-plan`), UX-02 export part + FK-010. Root cause FK-010: the app computed
  "bit-identical" as `crop && format != jpg`, ignoring the output depth, and set `allowDepthReduction = true` for every
  export. Core: `planExport(ImageInfo, request)` (no file access) with `reducesDepth`, `interpolation`, notes;
  `exportRegion` uses it and refuses an unconsented reduction; round image areas in `OutputStage` (PNG/TIFF alpha
  with a one-pixel ramp, JPEG white, DNG unchanged + note). App: `outputPlan` worded for the UI (Export tab line,
  export dialog first-print line, "not identical" reasons), explicit 8-bit consent checkbox, `exportImageAreas` /
  `exportFullPrints(ids, allowDepthReduction)`, default geometry "straighten" (D4), Segmented order/tooltips.
- Tests: core `ExportPlan.*` ×3 — matrix of 96 combinations (first run read DNG back with the image reader, which
  does not decode Linear DNG [failed] — now libtiff tags), consent, round area per format; core suite 176 pass.
  Self-test (fixture): default straightening labelled (2.81°, Lanczos-3); FK-010 known issue → 2 checks; all pass.

### 17:55–19:30 — PR #12 green, U011
- PR #12 (U010): all checks green incl. test packages. PRs #9–#12 wait for the owner's merge, in order.
- Correction: the headers of the six entries before this one (now 16:15…17:55) had been estimated instead of read
  from the clock and ran to 21:00, past the actual time; corrected from the commit times (`git log`) and the PRs'
  `merged_at`. Contents unchanged.
- U011 (branch `unit/U011-auto-count`), UX-01. Root cause: the expected count was compared only during detection
  (`detect.cpp`), and the left-rail number edited the global default, never the scan, so a corrected count left the
  stored warning; with no automatic mode every single-print scan was compared with the default 4.
- Core: `DetectParams.expectedCount = 0` = automatic; `ScanRecord.autoCount` + kept manual `expectedCount`,
  `ProjectSettings.autoCount` (default on; scans of older projects stay manual); `updateCountWarning` re-derives
  `count_mismatch` without detecting. Hints kept when automatic: `no_prints_found`, `too_many_candidates`, new
  `possible_missed: N`. Design measured on the corpus (`fkcli eval`, 24 scans, 3 with missed low-visibility prints):
  leftover mask components of 25–60 % of the smallest image area catch 1 of 3; a 1 mm grid of faint differences
  first compared with the detection's background model, which also fits the faint prints (synthetic test: ΔE
  1.1–1.4 < 1.5, not found [failed]) → background fitted to the 5 mm border band only: 3 of 3 flagged; the first
  version without a shape test flagged an uneven background on a complete scan (fill 0.57) → fill ≥ 0.7: 0 false.
  Owner's 4 real scans (`fkcli detect`, automatic): no `possible_missed`. `eval --enforce` gains both gates.
- App: left-rail "Count prints automatically" + number (greyed, "Prints found", while automatic), Settings default,
  `setScanCount`, notifications and warning texts (zh_CN). Settings read back from INI keep the default's type.
  New project now clears the canvas (seen in a self-test snapshot: the previous project's scan stayed visible).
- Self-test: UX-01 known issue → 17 checks with mouse clicks on the switch and −/+. First attempt sent QMouseEvent to
  the window: the CheckBox was pressed but never toggled [failed]; then a modal "project damaged" dialog left open by
  the data-protection phase took the input [failed] → clicks injected the way Qt Test does (`qt_handleMouseEvent`,
  exported by Qt GUI), all dialogs closed first. Owner's polaroid_92 (first run): 2 FK-008 checks failed — the U008
  check counted every recovery snapshot, and on a slow scan the 3 s timer had also saved earlier phases' projects
  (test flaw, app correct) → the checks follow this project's snapshot only; the blank scan derived from a
  single-print scan was card-sized and correctly detected as one print → blank scans are A4.
- Owner's img20260702_0005 (untagged 800 dpi 4-up, first run with the U006 checks on this scan): 2 FK-003 checks
  failed — exporting with "convert to Adobe RGB" is (correctly) refused for an untagged scan, so the check had no
  export to relink. Precondition missing, behaviour correct → the self-test records such checks as `skipped` with
  the reason (never as passed; `check-report.py` lists them); FK-003 stays covered on tagged scans (fixture,
  polaroid_92).
- Tests: core suite 184/184 (`CountSetting.*` ×4, `Detection.*` +4); mutations — no-op `updateCountWarning` → 2
  failures; count compared when automatic + no `possible_missed` → 3 failures; restored (no MUTATION markers).
  Self-test: fixture 92 pass; owner's polaroid_92 90 pass; img20260702_0005 86 pass + 4 skipped (FK-003, untagged).
  Scenario `count`: C1 automatic, no warning (3 prints); C2 manual 4 → "found 3, expected 4"; C3 manual 3 → none.

### 19:30–20:40 — PR #13 green, all five PRs merged, U012
- PR #13 (U011): all checks green. The owner merged PRs #9–#13 at 19:59–20:00 (merge commits b936a6a … 05291c4).
- U012 (branch `unit/U012-gt-accuracy`, from U011), UX-02 detection part. Order kept as REAL-SCAN-GT requires:
  1. Tools first: `fkcli realeval` (fk.realgt/1; scans found by SHA-256; per side and per stratum) and
     `tools/gt/draft-gt.py` (pixels only, independent of the detector). First GT rule (half level between plateaus)
     put polaroid_92's bottom inner edge on the image/reagent-band boundary (29 px off, seen in a zoomed crop)
     [failed] → first significant gradient peak walking from the known material; outer plateau moved to 2.5–3.5 mm
     inside the seed after the 4-up bottom seeds (1.7 mm outside) started inside the shadow band [failed].
     Drafts for the owner's 4 scans (7 prints) stay outside the repository (REAL-SCAN-GT §4).
  2. Gates G1–G6 frozen in REAL-SCAN-GT §3 before any detection change; baseline (U011 code): G1 = 1 cut (img001,
     regression set → registered and moved to tuning), G3 0.97/1.90 mm, G6 0.37 mm.
  3. Fixes. Export: "straightened" rotated by the frame's angle and took a bounding box — on tightly cropped scans
     the frame is the scan border (angle 0) and the window is sheared ≈ 0.3° → quad windows now mapped exactly
     (projective, `squareToQuad`); G6 → 0.05 mm, all 184 existing tests unchanged. Outer: paper-side-first rule;
     iterations measured on GT + generated corpora (each recorded): plain inside-out at half contrast left corner
     lines on the lid [failed]; corner lines ending in the neighbour's shadow → paper-consistency filter; 0.3
     threshold over all colour shifts would cut a rolled dark paper edge on dark backings (img001: 2–3 px)
     [failed] → only darkening on lighter backings; onset-width tests rejected real band edges [failed, removed];
     quarter-way crossing on lighter backings cut a blurred synthetic step by 0.74 px [failed] → only on ramps
     wider than max(6 px, 0.2 mm); the paper's texture noise discarded edges of a low-contrast synthetic print
     (9.7 px cut) [failed] → contrast judged against the backing noise as before. img001: clamping corners turned
     a side that leaves the scan and cut 3–5 px → such a side becomes the scan border.
- Result (realeval --enforce, all gates): G1 0, G2 0/0, G3 0.25/0.55 mm, G4 0.05, G5 0, G6 0.05/0 mm. Generated
  corpora: 12 scans and 24 scans, 0 cuts, slack median 1.15 px. Owner's regression scan polaroid_93: outer not
  measurable (all sides on the scan border), inner/export 0.035 mm.
- Tests: core 189/189 (+ `Detection.WhitePrintOnWhiteLidExcludesTheShadowBand`, `DarkPrintOnLighterBackingIsNotCut`,
  `SinglePrintLeavingTheScanIsContained`, `Geometry.StraightenedImageAreaFillsTheOutputExactly`, ctest
  `realeval_fixture`). Mutations (darkening rule off; corner clamping back; rotation straightening back;
  quarter-way on steps) → each caught by its test; restored (no MUTATION markers).
- Self-test (app rebuilt from this branch): fixture 92 pass; owner's polaroid_92 90 pass; img20260702_0005 86 pass +
  4 skipped (FK-003 conversion checks need a tagged scan). `draft-gt.py --overlays DIR` writes review sheets (edge
  crops with the draft line) for the owner's GT review; they show scan content and stay local.
- Found, not fixed (registered in UNITS U012): generated scan seed 1 (216×150 mm, 300 dpi) gets a false
  `dpi_rescaled` 306 and an Instax Mini inner 47 px off on one side (low confidence; identical on the baseline).
- Blocked: no white-backing regression sample (img20260702, 679 MB), no round/dark/1600 dpi real samples; GT drafts
  need the owner's review before they count as evidence.

### 20:40–21:40 — PR #14 opened and green, U013
- U012 committed (13616be) and PR #14 opened via the REST API; an extra docs-only commit (b1fa3b8) brings the
  RFC-0002 export-geometry table in line with the exact straightening. All checks green (Publish release skipped).
- U013 (branch `unit/U013-shift-shapes`, stacked on U012), UX-03. PRD F04 A5–A9, RFC-0002 §Editing, RFC-0011
  §Boundary editing input, CONTRACT-ui-core §4, API-SPEC §4 written with the code.
  - Core: `fk::editShape` (one geometry for preview, commit and nudges), `ellipseHandle`, `isCircle`;
    `transformShape` keeps circles round — measured first: translation + rotation made rx ≠ ry in the last bits
    for 1820 of 2000 sampled maps. The first version of the transform test used maps that happen to be exact and
    passed with the rule removed [weak test] → 50 translation + rotation maps, now fails without the rule.
  - App: `ShiftTap` watches the application's events for the main window. A window-only filter would miss
    Shift+→: the shortcut consumes the key press before the window sees it, so `ShortcutOverride`/`Shortcut` also
    disarm. Arming is refused while a mouse button is held (otherwise Shift pressed during a drag and released
    after it counts as a tap). `ScanCanvas`: `editTarget` badge, `switchBoundary`, Shift mid-drag, focus-out /
    ungrab / Esc cancel, `handleActive` property for the arrow shortcuts, radius handles on the rotated axes.
    Main.qml did not import the app module (`ShiftTap is not a type`) → fixed at the first run.
  - Found by reading the old code, now covered: Shift+←/→ stepped scans although a handle was selected (the plain
    ←/→ guard called a non-reactive function); nudging a circle's radius changed only rx or ry.
- Tests: core 197/197 (+8 `Edit.*`). Self-test `runShapeEditing`: 33 checks with real key and mouse events
  (fixture: all pass at the first complete run). Mutations, each guard removed → the self-test fails at the
  targeted check: mouse disarm (Shift+click), shortcut disarm (Shift+→ next scan), focus-out cancel (focus lost
  mid-drag), typing guard, dialog/popup guard, Shift+→ handle guard (nudge moved 0 px = the old bug), constraint
  off (rectangle checks), mouse-held arming (Shift pressed during a drag). Restored; no markers left.
- Self-test results (app from this branch): fixture 126 pass; owner's polaroid_92 124 pass; img20260702_0005 120 pass + 4 skipped (FK-003 conversion checks need a tagged scan); the 33 UX-03 checks pass on all three.
- Not verified: real window feel, macOS native menu (skipped offscreen), Shift with an input method active.

### 21:40–22:30 — PR #15 opened and green, PR #14 merged, U014
- U013 committed (8fb15dc), PR #15 opened; all checks green, including the packaged self-test on Windows and macOS
  (the macOS menu check is skipped there). The owner merged PR #14 at 21:21.
- U014 (branch `unit/U014-scan-rotation`, stacked on U013), UX-04. Design choice: the turn is a **view** of the scan
  (`ScanRecord::rotation`); every boundary stays in scan-file coordinates, so detection, edits, exports, export
  records and relink need no conversion and nothing is rotated twice. Rejected: turning the geometry and reading
  the scan turned (every reader and every stored shape would change; old export records would need migrating).
  - Exports: `exportTurns` = the print's own upright when uprights are applied (a view turn does not change which
    way is up for a print — adding it would turn correctly detected prints twice), else the view rotation.
  - Canvas: scan → display mapping applied once (`quarterTurnMatrix`), images drawn under the painter transform,
    overlays/hits/nudges through the same mapping; labels at the on-screen top-left corner.
  - Undo: print steps and scan steps share one order (`seq`) so one Ctrl+Z restores the whole scan and undo/redo
    interleave correctly.
  - Self-test scan: one print of the test scan four times, turned 0/90/180/270° (detection found 4 prints with 4
    different uprights). Each print is clicked at the screen position computed independently of the canvas. The
    first run failed one check: the generated bed was square, so a turned thumbnail had the same size [test flaw]
    → non-square bed and a pixel comparison with the thumbnail turned 90°.
- Tests: ctest 201/201 (+ `ScanRotation.*` ×3, `Geometry.QuarterTurnsAreExactPermutations`); self-test
  `runScanRotation` 14 checks + 5 snapshots (scan, 100 % with the full-resolution patch, composite with split,
  image area — all turned).
- Mutations (each → the self-test fails at the targeted check): scan thumbnail not turned; canvas ignoring the turn
  (clicks select the wrong prints); uprights doubled ("never doubled"); turn ignored with uprights off (pixel 0,0);
  undo ignoring the scan's steps; turn not saved. The first form of the "ignored when off" mutation did not compile
  (unused parameter) and was rerun as `scan.rotation * 0`. Restored; no markers left.
- Self-test results: fixture 145 pass; polaroid_92 143 pass; img20260702_0005 139 pass + 4 skipped (FK-003 needs a
  tagged scan); 14 UX-04 checks pass on all three. polaroid_92 failed "four orientations" at first (4 prints, one
  upright): the print fills that scan, so the "backing" copied from the scan border was its own white frame and the
  bed was white-on-white [test flaw] → a dark lid for scans filled by their print (≥ 60 % of the area).

### 22:30–00:15 — PR #16 opened and green, PR #15 and #16 merged, U015
- U014 committed (a535c93), PR #16 opened; all checks green. The owner merged PR #15 at 22:20 and PR #16 at 22:41.
- U015 (branch `unit/U015-mask-defaults`, stacked on U014), UX-08.
  - Measured before choosing: new `fkcli maskeval` composites each detected image area back exactly as the final
    output, with synthetic grades, and measures frame contamination, ungraded rim and the seam at 50 %. First
    version classified frame vs picture by colour: white picture content near the edge counted as "frame" and
    inflated every number [weak metric] → the real-scan GT window edge decides (colour only without GT). ΔE00 moved
    into the core and checked against the published CIEDE2000 test pairs.
  - Result on the owner's 4 scans / 7 prints (826 and 1200 dpi): worst-case seam at 50 % hard edge 6.61, feather
    0.03 mm (the suggested trial value) 4.71, 0.05 mm 3.64 (every print better than hard: 26–56 %), 0.08 mm 3.86;
    rim at 0.05 mm ≤ 0.33 px; preserve edge best at 0.02 mm (3.96). Chosen: feather 0.05 mm natural default,
    preserve 0.02 mm preset, px fallbacks 2.4 / 0.9. Remaining seam = true frame pixels inside the detected image
    area (detection error); a mask cannot move the edge outward. Unchanged files: seam 0.
  - Model: per-print `composite.mask` (mode, width, unit); prints of older projects keep the project setting;
    `resolveMask` shared by preview and final; explicit batch; strong-edge hint from the preview (`edgeChange`).
  - Self-test first run: the "strong grade" (gains only) gave ΔE00 2 on a dark print [test flaw: gains do not change
    near-black] → a red offset; normal 1.49, strong 16.47. A snapshot showed the mask controls widened the right
    rail (caption and buttons on one row) → caption on its own line, buttons in a Flow. (The rail's other rows also
    overflow at 300 px — FK-009, U024.)
- Tests: ctest 206/206 (+ `NaturalBlend.*` ×3, `MaskChoice.*`, `DeltaE2000.*`); self-test `runMaskDefaults`.
- Mutations (each caught): mm widths treated as px (core test + self-test; the first self-test run predated the
  independent px expectation and passed [weak check] → expectation computed without `resolveMask`, rerun caught
  it); preview using the project setting instead of the print's mask; no default for new prints; hint never shown;
  batch applied to every print; mask not saved. Restored; no markers left.
- Self-test results: fixture 157 pass; polaroid_92 153 pass + 2 UX-08 skipped (one print: no batch, no second
  print for the strong grade — the strong-grade check was first silently left out on single-print scans, now
  recorded as skipped); img20260702_0005 151 pass + 4 FK-003 skips. UX-08: 11 / 9 / 11 pass.
- Not verified: 100 % / 200 % visual review by the owner (review sheets from `maskeval --crops` stay local); no 1600
  dpi or round real samples; GT is a draft.
