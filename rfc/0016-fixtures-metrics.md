# RFC-0016 — Synthetic fixtures and detection metrics

Status: Accepted · Tool: `tools/fixtures` (`fkfixture`), `fkcli eval` · Tests: CTest `eval_corpus` (label `eval`)

## Generator
Deterministic (seeded PCG32) streaming renderer: rows are produced one at a time (memory O(width)).
Scene = background (black/dark grey, linear + radial illumination gradient, noise), N prints with film type,
frame colour (white, cream, aged yellow, gold, black, coloured), rotation, position, image content
(procedural: gradients, discs, stripes, noise; or near-black / near-white), handwriting strokes on the bottom
border, stains, dust specks, scratches, glare streak, drop shadow on one side. Edges anti-aliased by 4×4
supersampling near boundaries. Output TIFF 8/16-bit, RGB/grey, any compression, strips or tiles, BigTIFF,
optional ICC; DPI 300–1600. Ground truth JSON: exact outer quads, inner shapes, film types, upright.

## Ground truth (`fk.fixture-gt/1`, one JSON per scan)
`dpi`, `width`, `height`, `background {level, gradient, noise}`, and per print: `filmType`, `frame`,
`frameColor` (encoded RGB 0–1), `content`, `quarterTurns`, `tiltDeg`, `upright`, `outerQuad` (clockwise from the
smallest x+y), `outerBoundary` (256 points on the rounded outline), `inner` (`quad` points or `ellipse`
centre/rx/ry/angle). Pixel convention as in RFC-0002.

## Metrics (`fkcli eval <dir> [--enforce] [--json report] [--overlays dir]`)
Each GT print is matched to the unused detection with the nearest centroid (or the detection containing its
centre). Visibility: ΔE between `frameColor` and the background level < 4 → **low visibility** (outline not
recoverable from pixels).
- cuts_on_detected: matched prints where a GT boundary point lies outside the detected outer by > 0.5 px
  (all prints, any confidence) — must be 0.
- missed_visible (must be 0) and missed_low_visibility (allowed; the scan then carries `count_mismatch`).
- outer slack (visible): mean signed distance from the central 80 % of each GT side to the matching detected
  side, outward positive; median, P95 of |slack|, max.
- inner error (visible): max per-side slack (quad) or max(centre offset, radius error) (ellipse); median, P95.
- count accuracy on scans whose prints are all visible; film-type and orientation accuracy on visible prints.
- low-confidence recall: fraction of "bad" detections (cut, |slack| > 8 px, inner error > 15 px or wrong
  orientation) flagged < 0.6. `false_low_confidence` counts flagged good detections (informational; touching
  prints are always flagged by design).
Thresholds: TEST-PLAN §4, enforced by `--enforce` in CTest on the seed-1 corpus of 12 scans.
