# F12 — Colour management

RFC: `rfc/0012-color.md`, ADR-0003, `docs/color-pipeline.md` · Tests: `tests/core/test_color.cpp`

## Rules
- Read the input ICC. *Assign* (tag without changing numbers) and *convert* (change numbers) are separate
  operations in API, UI and logs.
- No ICC: user chooses "assign profile X" or "keep untagged"; nothing is guessed.
- Working space = the scan's own space; frame pixels are never converted by default.
- A graded file in another space is converted once (LittleCMS, relative colorimetric + BPC by default,
  16-bit or float path) into the scan's space before compositing.
- Optional output conversion to a chosen space happens exactly once, at write; UI says frame pixels are then no longer bit-identical.
- 16 → 8 bit only with explicit consent.
- Display: canvas pixels converted from image space to the display profile (macOS: sRGB, the window is
  colour-managed by the OS; Windows: the ICC of the window's monitor, sRGB fallback); updated when the
  window moves to another screen, screens change, or the monitor's ICC changes (FK-011, U025).

## Acceptance
- A1 Known Lab/RGB values convert correctly between sRGB, Adobe RGB, ProPhoto and a scanner-style profile (ΔE00 < 0.5). (`test_color.KnownValues`)
- A2 Assign vs convert produce the documented different results. (`test_color.AssignVsConvert`)
- A3 16-bit path keeps 16-bit precision (no 8-bit intermediate). (`test_color.Precision16`)
- A4 Composite with a graded file in another space leaves frame pixels unchanged. (`test_composite.ForeignIcc`)
- A5 (FK-011) MinIsWhite 8/16-bit: Preview and Full show the same brightness. (scenario `miniswhite`, CI)
- A6 (FK-011) Another display profile: the proxy is converted again exactly, Preview and Full agree, the 100 %
  patch and the composite preview are made again, thumbnails follow, a result made for the previous profile is
  never shown, outputs are unchanged. (self-test `runDisplayProfile`; a real second monitor: HV)
