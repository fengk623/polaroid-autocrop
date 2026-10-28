# Changelog

All notable changes. Versions follow `VERSION`; release notes with measurements and known limits live in
`docs/release-notes/`.

## 0.1.0 — first public build

- Detection of instant prints in flatbed TIFF scans (Polaroid 600 / i-Type / SX-70, Polaroid round frame,
  Instax Mini / Square / Wide), with confidence classes and review flags.
- Boundary editing, templates, undo/redo, review queue.
- Image-area export (TIFF, PNG, JPEG, Linear DNG), relink of graded files, compositing into the real scanned
  frame, synthetic frames.
- Projects, colour management (ICC keep/convert), metadata privacy, English and Simplified Chinese UI.
- macOS universal DMG, Windows installer and portable ZIP (unsigned).
