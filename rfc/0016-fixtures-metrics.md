# RFC-0016 — Synthetic fixtures and detection metrics

Status: Accepted · Tool: `tools/fixtures` (`fkfixture`), `fkcli eval` · Tests: CI `detection-eval`

## Generator
Deterministic (seeded PCG32) streaming renderer: rows are produced one at a time (memory O(width)).
Scene = background (black/dark grey, linear + radial illumination gradient, noise), N prints with film type,
frame colour (white, cream, aged yellow, gold, black, coloured), rotation, position, image content
(procedural: gradients, discs, stripes, noise; or near-black / near-white), handwriting strokes on the bottom
border, stains, dust specks, scratches, glare streak, drop shadow on one side. Edges anti-aliased by 4×4
supersampling near boundaries. Output TIFF 8/16-bit, RGB/grey, any compression, strips or tiles, BigTIFF,
optional ICC; DPI 300–1600. Ground truth JSON: exact outer quads, inner shapes, film types, upright.

## Metrics (`fkcli eval <dir>`)
- inner_cut_violations: count of prints where any ground-truth outer corner or edge midpoint lies outside the
  detected outer by > 0.5 px.
- outer slack: per side, distance from GT edge to detected edge (outward positive); median, P95, max.
- inner error: per side (quad) or centre/radius (ellipse); median, P95.
- count accuracy, film-type accuracy, orientation accuracy, low-confidence recall (fraction of prints with
  outer slack > 8 px or inner error > 15 px that were flagged < 0.6).
Thresholds are recorded in TEST-PLAN.md after the first measured run and enforced in CI on a fixed seed corpus.
