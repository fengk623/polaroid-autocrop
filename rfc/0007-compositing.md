# RFC-0007 — Compositing

Status: Accepted · Module: `core/compositing` · ADR-0010 · PRD F07 · Tests: `tests/core/test_composite.cpp`, `test_roundtrip.cpp`

`compositeItem(source, CompositeRequest, cancel, progress) → ExportReport`
- Inputs: item (outer, inner, export geometry of the edit version), graded `ImageBuffer` (decoded fully;
  print-sized), confirmed relink actions, mask mode, output options (RFC-0005).
- Graded image preparation (once): apply confirmed rotation/scale; convert into the scan's colour space
  (RFC-0012) and depth.
- Output geometry: full print, PixelCrop by default (Straightened optional).
- Per output band: read the scan rows (sliding window), for each pixel compute scan position, mask weight
  (RFC-0002), and for w > 0 the graded sample at `scanToEdit(position)`: exact integer copy when the edit
  version was PixelCrop (translation/permutation), Lanczos-3 otherwise.
- Manual mask strokes (polylines with radius and add/remove) rasterised per band.
- Bit-exact guarantees as in ADR-0010; tests cover all.
- Preview: same function on the proxy-scale source (`ImageSource` adaptor over the proxy) → identical geometry.
