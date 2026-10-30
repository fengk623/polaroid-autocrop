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
