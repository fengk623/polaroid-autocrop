# RFC-0008 — Synthetic frame

Status: Accepted · Module: `core/src/composite.cpp` · PRD F08 · Tests: `tests/core/test_composite.cpp` (`SyntheticFrame.*`)

`FrameLayout computeFrameLayout(SizeI picture, FrameSpec)` → canvas size and picture offset.
`FrameSpec { mode: Equal|CanvasRatio|Sides; unit: PercentLongEdge|Pixels; top,right,bottom,left; ratio w:h;
minMargin; colour RGB16; platform target (w,h) optional }`.
- Equal: all sides = v. Sides: independent. CanvasRatio: smallest canvas of the ratio containing picture + 2·minMargin, picture centred.
- Percent values are of the picture's long edge, rounded to whole pixels (round-half-up) — same rule in preview and export.
- Platform target: pad to target ratio first, then Lanczos-3 resize to the target; original-size output never resized.
- Colour default: median colour of the item's scanned frame band (from the original scan, frame region only).
- Output carries `_SYNTH` and XMP `fk:FrameMode="synthetic"`.
