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
