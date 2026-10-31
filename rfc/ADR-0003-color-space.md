# ADR-0003 — Working colour space and display pipeline

Status: Accepted · Date: 2026-10-28

## Decision
- The working space is the scan's own encoding (its ICC, or "untagged"). No working-space conversion.
  Reason: archival mode requires frame pixels to be bit-identical; converting into a large working space
  (as SpektraLab does with ProPhoto RGB for its simulation) would change every frame pixel.
- Graded images are converted once into the scan's space (LittleCMS 2, relative colorimetric + black-point
  compensation, 16-bit or float path; intent configurable) before compositing.
- Optional output conversion to a user-chosen profile happens exactly once, at write.
- Untagged sources: user chooses assign X / keep untagged; display treats untagged as sRGB and shows an "untagged" chip.
- Display: proxy and tiles are converted on the CPU from image space to the display profile:
  macOS → sRGB (Qt windows are tagged sRGB and the OS colour-manages to the screen);
  Windows → the monitor's ICC (`GetICMProfileW` on the window's screen), sRGB fallback; recomputed on screen change
  (implemented in U025 / FK-011: screen change, screens added/removed, 5 s poll for a changed ICC; see
  docs/color-pipeline.md).
  Qt Quick does not perform display-profile conversion itself; we do not rely on it.

## Consequences
Display conversion costs CPU per tile (8-bit output; cached per tile). Verified with known colour patches in real windows (human verification list).
