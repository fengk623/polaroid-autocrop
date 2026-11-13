# F08 — Synthetic frame (PhotoFrame-style) output

RFC: `rfc/0008-synthetic-frame.md` · Tests: `tests/core/test_composite.cpp` (`SyntheticFrame.*`)

Reference: behaviour observed in PhotoFrame / Encadré (see `docs/reference-audit.md`); no code or assets copied.

## Scope (v1)
- Output mode distinct from archival mode: "Synthetic frame". The real scanned frame mode stays default.
- Input: the image area (graded composite or original), never the scanned frame.
- Modes: equal width; canvas ratio (1:1, 4:5, 3:4, 2:3, 9:16, custom; picture centred, minimum margin);
  four independent sides (instant-film style default: top 5 %, left 5 %, right 5 %, bottom 18 % of the long edge).
- Sizes in % of long edge or px; colour: picked from the scanned frame's average colour (default), eyedropper, swatches.
- Picture never cropped or resized at original size; frame added outside.
- Presets (JSON), per-item override, batch export, platform sizes (pad, then high-quality resize).
- File name suffix `_SYNTH`; XMP `fk:FrameMode="synthetic"`; UI label "Synthetic frame".

## Non-goals (v1)
Watermarks, camera databases, dominant-colour palettes, "cleaned real frame".

## Acceptance
- A1 Output size = picture + borders exactly; picture pixels identical at original size. (`SyntheticFrame.LayoutModes`, `SyntheticFrame.PictureCopiedExactlyAndColourApplied`, `SyntheticFrame.PlatformTargetHasExactSize`)
- A2 % of long edge gives identical relative borders for portrait and landscape inputs.
- A3 Synthetic outputs are always labelled (suffix + XMP).
