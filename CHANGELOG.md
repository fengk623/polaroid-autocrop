# Changelog

All notable changes. Versions follow `VERSION`; release notes with measurements and known limits live in
`docs/release-notes/`.

## 0.7.0-alpha.2 — U012: outer edges from the paper side, exact straightening, real-scan GT tools (UX-02)
- The outer edge is found from the paper side: on a white lid the shadow band is no longer included (owner's
  scan: 0.9–1.9 mm → 0.1–0.5 mm), a single-print scan's side that leaves the scan becomes the scan border instead of
  cutting paper; "straightened" maps a quad image area exactly onto the output (no frame slivers from scanner
  shear). New `fkcli realeval` and `tools/gt/draft-gt.py`. Notes: docs/release-notes/v0.7.0-alpha.2.md.

## 0.7.0-alpha.1 — U011: automatic print count (UX-01)
- Scans are counted automatically by default (no count warning made from a default number); each scan can switch to
  a manual expected count, kept while automatic; the count warning follows the setting at once, without detecting
  again. Automatic mode still reports low confidence, possibly missed prints (`possible_missed`), blank scans
  (`no_prints_found`) and the 12-object limit. Also: a new project clears the canvas; stored settings keep their
  types. Notes: docs/release-notes/v0.7.0-alpha.1.md.

## 0.6.0-alpha.10 — U010: export output plan, labelled straightening, round image areas (UX-02 export, FK-010)
- Every export prompt is worded from the core's plan of the real output (no "bit-identical" for 16 → 8 bits);
  16 → 8 bits needs an explicit yes; image areas are straightened by default and say so; round image areas are
  transparent (PNG/TIFF) or filled white (JPEG). Notes: docs/release-notes/v0.6.0-alpha.10.md.

## 0.6.0-alpha.9 — U009: privacy presets on every channel (FK-004)
- "Remove location and identity" no longer lets the source XMP creator through; one filter feeds every writer
  (TIFF/DNG tags, XMP in TIFF/PNG/JPEG/DNG); the Export tab states exactly which fields an export carries.
  Notes: docs/release-notes/v0.6.0-alpha.9.md.

## 0.6.0-alpha.8 — U008: data protection (FK-001, FK-008)
- Every way of replacing the open project asks Save / Don't save / Cancel (also for dropped project files and with
  running tasks); a failed save keeps everything. Recovery snapshots of unsaved work (never the project file),
  restore of a damaged project from its backup (the damaged file is kept), a damaged file never replaces a good
  backup, output parameters saved with the project. Manual save stays (RFC-0017).
  Notes: docs/release-notes/v0.6.0-alpha.8.md.

## 0.6.0-alpha.7 — U007: source identity (FK-007)
- Scans are hashed whole at import and verified (streamed, off the UI thread) before every preview, detection and
  output; a replaced or edited scan is refused with the reason and can be relocated by content; outputs made while a
  file changed are discarded; a graded file saved over is used only if size, picture and profile are unchanged.
  Notes: docs/release-notes/v0.6.0-alpha.7.md.

## 0.6.0-alpha.6 — U006: graded files without a usable colour profile (FK-003)
- A missing or unreadable profile is never guessed: the export's profile (identified by hash), the scan's, sRGB,
  Adobe RGB or a profile file is chosen and confirmed when relinking, stored with the project (`iccProfiles`) and
  converted once when compositing. Notes: docs/release-notes/v0.6.0-alpha.6.md.

## 0.6.0-alpha.5 — U005: confirmed relink transform (FK-002)
- The turn a graded file was given in the editor is limited by its size, decided by the picture or chosen by the
  user, confirmed by ticking, stored with the relink and used as stored by preview and output (fixes 180° files
  composited upside down). Notes: docs/release-notes/v0.6.0-alpha.5.md.

## 0.6.0-alpha.4 — U004: target-first relink, evidence model (UX-07)
- Relink weighs file hash, embedded IDs, file name and picture together; conflicts and ambiguities are shown, never
  resolved silently; "Relink to this print…" (target first); "Assign to …" re-checks a file against a chosen print.
- New alignment for prints without an export in this project (`origin: "manual_alignment"` record); `_n` editor
  suffix; window drop sends graded TIFFs to relink. Notes: docs/release-notes/v0.6.0-alpha.4.md.

## 0.6.0-alpha.3 — U003: export records in the project, portable sidecar optional (UX-07)
- docs/release-notes/v0.6.0-alpha.3.md

## 0.6.0-alpha.2 — U002: background task safety (FK-006)
- docs/release-notes/v0.6.0-alpha.2.md

## 0.6.0-alpha.1 — U001: traceable builds, reproductions, known-issue tests (UX-12, FK-013, FK-014)
- docs/release-notes/v0.6.0-alpha.1.md

## 0.5.0 — first public build (pre-release)

- Detection of instant prints in flatbed TIFF scans (Polaroid 600 / i-Type / SX-70, Polaroid round frame,
  Instax Mini / Square / Wide), with confidence classes and review flags.
- Boundary editing, templates, undo/redo, review queue.
- Image-area export (TIFF, PNG, JPEG, Linear DNG), relink of graded files, compositing into the real scanned
  frame, synthetic frames.
- Projects, colour management (ICC keep/convert), metadata privacy, English and Simplified Chinese UI.
- macOS universal DMG, Windows installer and portable ZIP (unsigned).
