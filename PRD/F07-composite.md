# F07 — Composite into the real frame, final export

RFC: `rfc/0007-compositing.md`, ADR-0010 · Tests: `tests/core/test_composite.cpp`, `tests/core/test_roundtrip.cpp`

## Steps
1. Re-read the print region from the original scan (never from preview caches).
2. Keep every frame pixel.
3. Map the graded image into the Inner shape with the inverse matrix.
4. Replace only pixels inside the Inner mask.
5. Preview: original / graded / composite, split slider, 100 %.
6. User confirms. 7. Export full print (F05 formats; default 16-bit TIFF, pixel crop).

## Mask modes
- Hard edge (default): pixel replaced iff its centre lies inside the Inner shape — deterministic.
- Feather 0–5 px: linear blend across the boundary (inside distance).
- Preserve edge: Inner shrunk by N px (keeps original transition).
- Manual mask: brush strokes add/remove (stored as vector strokes in the project).

## Guarantees
- Crop mode + same ICC + depth ≥ source: frame pixels bit-identical to the scan; unmodified round trip
  bit-identical to the original print.
- Straightened mode: round trip error within TEST-PLAN thresholds (max abs and PSNR).
- External grading never touches frame pixels; frame never replaced by generated white.

## Acceptance
- A1 Unmodified round trip, crop mode, all depths/compressions: byte-identical pixels. (`test_roundtrip`)
- A2 Graded composite: every pixel outside the Inner mask equals the scan. (`test_composite.FrameUntouched`)
- A3 Straightened round trip: max abs error and PSNR meet TEST-PLAN thresholds. (`test_roundtrip.Straightened`)
- A4 Feather/preserve-edge masks behave as specified on analytic fixtures. (`test_composite.Masks`)
