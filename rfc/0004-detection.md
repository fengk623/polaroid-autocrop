# RFC-0004 — Detection

Status: Accepted · Module: `core/detection` · PRD F02, F03, F09 · Tests: `tests/core/test_cv_*.cpp`, `test_detection_*.cpp`, `fkcli eval`

## Stage A — proxy (memory O(proxy))
1. Proxy: exact box filter while streaming rows; factor `s = ceil(longSide / target)` (target 3000);
   float RGB in [0,1] of the encoded values (grey replicated).
2. Background model: robust median and MAD of a 2 % border band; a 2nd-order polynomial surface fitted
   to background-classified pixels (iterated twice) absorbs uneven lighting.
3. Distance: CIE76 ΔE in approximate Lab (sRGB assumption only for detection) between pixel and local background.
4. Threshold: max(Otsu(ΔE), 6·σ_bg, 8 ΔE); binary mask.
5. Morphology: close (radius ≈ 0.6 mm), open (≈ 0.3 mm), fill holes.
6. Connected components (8-connected); keep area ≥ 40 % of the smallest film type at the scan's DPI.
7. Per component: convex hull → minimum-area rectangle (rotating calipers). Components whose area ≈ k×
   a single print and rectangularity < 0.85 are split along mask-projection valleys (touching prints).
8. Film type: nearest nominal size in mm (DPI known) or aspect + relative size (DPI unknown).

## Stage B — full-resolution refinement (memory O(band))
For every side of every print a band (±max(48 px, 3 proxy px·s)) is read with `readRows` in increasing
y order (all prints and sides in one pass where the reader is sequential-only). Along the side, every k-th
scan line perpendicular to the side gives a profile; background level from the outer end, paper level from
the inner end; a pixel is paper when its distance to background exceeds half the paper–background distance
(3-px box smoothing). For each line take the outermost paper position. Fit the side line with a robust
regression (Tukey) to those positions, then translate it outward until every outermost position is inside
(**containment by construction**) plus 1 px. Corners = intersections of the four lines. Instax corner radius
measured from the corner profiles. Low contrast (paper–background distance < 5·noise) → confidence penalty.

## Inner detection
On the print rectified from the proxy (≈1000 px long side): frame colour F = robust median of a band just
inside each edge. For each side, profile of "fraction of non-frame pixels" per depth over the central 60 % of
the side; the inner edge is the first depth where the fraction exceeds 0.5 for ≥ 3 consecutive lines and the
mean colour gradient peaks; search window from the film-type prior (± 45 % border width). No clear edge →
prior position, confidence ≤ 0.5. Round frame: if the four corners of the inner square are frame-coloured,
radial profiles from the centre (360 rays) + robust circle fit. Then full-resolution refinement of inner edges
in bands (same reader pass). Orientation = side with the widest measured border.

## Confidence
Outer: contrast/noise, rectangularity, size plausibility vs film type, refinement residual spread, count
match. Inner: edge sharpness, prior agreement, left/right symmetry. Combined 0–1; < 0.6 low (must review),
0.6–0.85 medium, ≥ 0.85 high. Reasons are machine-readable strings.

## Ordering
Rows by clustering print centres on y (tolerance ½ median print height), left→right within a row.

## Templates
`fk.template/1`: slots with relative quads (outer, inner), film type, DPI at creation. Applying: scale to the
new scan, then stage B snapping; snap offset > 1.5 mm lowers confidence.

## Determinism
No randomness; identical input → identical output on every platform (tests compare to tolerance 1e-6 px).
