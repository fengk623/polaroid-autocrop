# F07 — Composite into the real frame, final export

RFC: `rfc/0007-compositing.md`, ADR-0010 · Tests: `tests/core/test_composite.cpp`

## Steps
1. Re-read the print region from the original scan (never from preview caches).
2. Keep every frame pixel.
3. Map the graded image into the Inner shape with the inverse matrix.
4. Replace only pixels inside the Inner mask.
5. Preview: original / graded / composite, split slider, 100 %.
6. User confirms. 7. Export full print (F05 formats; default 16-bit TIFF, pixel crop).

## Mask modes
- Hard edge: pixel replaced iff its centre lies inside the Inner shape — deterministic; the archival exact path.
- Feather (natural blend): linear ramp inside the boundary (inside distance); the default for new prints since
  0.7.0-alpha.5 (UX-08): 0.05 mm, converted with the scan's DPI (px without a reliable DPI).
- Preserve edge: Inner shrunk by an inset (preset 0.02 mm).
- Manual mask: brush strokes add/remove (stored as vector strokes in the project).
- UX-08: the mask is saved per print; new prints get Settings ▸ Mask for new prints; applying a mask to other prints
  is an explicit action on the selected prints; prints of older projects keep the project's setting. Every
  transition stays inside the image area. Strong edge changes are flagged for a manual check.

## Guarantees
- Crop mode + same ICC + depth ≥ source: frame pixels bit-identical to the scan; unmodified round trip
  bit-identical to the original print.
- Straightened mode: round trip error within TEST-PLAN thresholds (max abs and PSNR).
- External grading never touches frame pixels; frame never replaced by generated white.

## Acceptance
- A1 Unmodified round trip, crop mode, all depths/compressions: byte-identical pixels. (`Composite.UnchangedEditRoundTripIsBitExact`; `fkcli roundtrip`)
- A2 Graded composite: every pixel outside the Inner mask equals the scan. (`Composite.GradedPixelsOnlyInsideTheImageArea`, `Composite.StraightenedRoundTripKeepsFrameExact`)
- A3 Straightened round trip: max abs error and PSNR meet TEST-PLAN thresholds. (`Composite.StraightenedRoundTripKeepsFrameExact`)
- A4 Feather/preserve-edge masks behave as specified on analytic fixtures. (`Geometry.MaskWeightNeverTouchesOutside`, `NaturalBlend.UnchangedEditStaysBitExactWithEveryMask`, `NaturalBlend.PreviewAndFinalUseTheSameMask`)
- A5 (UX-08) New prints get the natural blend preset, in mm converted with the scan's DPI (px without one); it is
  saved per print, changed in the right rail (typed width, undo), applied to other prints only on request.
  (`NaturalBlend.PresetsAreMillimetresWhenTheDpiIsKnown`, `MaskChoice.*`, self-test `runMaskDefaults`)
- A6 (UX-08) With every mask an unchanged graded file composites bit-exactly; the preview and the final output use
  the same mask. (`NaturalBlend.*`, self-test)
- A7 (UX-08) The presets lower the seam at 50 % on every real sample without a visible ungraded rim, and never touch
  frame pixels outside the image area. (`fkcli maskeval`, RFC-0007 §Natural blend — owner's visual review pending)
- A8 (UX-08) A strong colour change at the edge is flagged for a manual check. (self-test)

