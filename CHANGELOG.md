# Changelog

All notable changes. Versions follow `VERSION`; release notes with measurements and known limits live in
`docs/release-notes/`.

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
