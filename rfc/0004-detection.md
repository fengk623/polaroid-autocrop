# RFC-0004 — Detection

Status: Accepted · Module: `core/detection` · PRD F02, F03, F09 · Tests: `tests/core/test_detection.cpp`, CTest `eval_corpus` (`fkcli eval`)

## Stage A — proxy (memory O(proxy))
1. Proxy: exact box filter while streaming rows; factor `s = ceil(longSide / target)` (target 3000);
   float RGB in [0,1] of the encoded values (grey replicated).
2. Background model: median of a 2 % border band, then a 2nd-order polynomial surface (L, a, b) fitted to
   background-classified pixels (two iterations) absorbs uneven lighting; σ_bg = MAD of ΔE on background.
3. Distance: CIE76 ΔE in approximate Lab (sRGB assumption only for detection) to the local background.
4. Hysteresis mask. Strong: ΔE > max(6σ_bg + 3, min(Otsu, 20)), opened (≈ 0.3 mm). Weak:
   ΔE > max(3.5σ_bg + 1.5, 2.5), closed (≈ 0.6 mm) then opened with ≈ 1 mm (frames are ≥ 4 mm wide; dust and
   scratches are thin). Weak components that touch a strong component are kept (a dark frame next to a
   visible picture); then close and fill holes.
5. Connected components (8-connected); keep area ≥ 60 % of the smallest image area at the scan's DPI.
6. Per component: convex hull → minimum-area rectangle (rotating calipers).
7. Card test: a component is one card when its size matches a film type within
   min(type tolerance, 2.5 mm) + 1 % of the long side (DPI known; 4 mm + 3 % when the DPI is estimated).
   The outline may include ≈ 1 mm of scanner shadow; stage B removes it.
8. Not a card →
   a. **Touching prints**: `splitByHull` cuts the component's boundary points with a straight line (tilted up to
      ±6° against the merged rectangle, every 1° and L/160) so that both parts are card sized; points within
      ±0.8 mm of the cut are the shared seam and are ignored when sizing the parts, whose seam side is then
      moved onto the cut. Cuts where both parts fit are preferred; otherwise one part must fit and the other is
      split recursively (≤ 4 prints). Fallback: equal division. Parts get `touching_split`.
   b. **Faint frame** (component smaller than a card): line-averaged outward profiles (central 60 % of each
      side, 1 px steps, 3-tap smoothing) find the frame→background transition at half the contrast between
      the level right outside the component and this profile's background level. Accepted only when the
      component has the size of a film type's image area and every growth matches that type's border for one
      orientation (±max(1.5 mm, 30 %)), and the grown rectangle is a card → `low_frame_contrast`
      (confidence ≤ 0.55, always reviewed).
   c. Otherwise **frame not visible**: grow every side by max(found growth, largest border of any film type)
      so that the real card is certainly contained → `frame_not_visible` (confidence ≤ 0.3).
9. Film type and orientation: see Inner detection.

## Stage B — full-resolution refinement (memory O(band))
All profile lines of all prints are sampled in one increasing-row pass (`readRows`, 64-row bands). Outer
lines: ≤ 200 per side, perpendicular to the proxy edge, from `half = 4·s + 16` px outside to `half` + 2 mm
inside (the inner end must be real paper even when the proxy outline includes a scanner shadow). Per line:
background = median of the outer fifth, paper = median of the inner fifth, distance in RGB; a crossing is the
first position after the **last background run** (≥ persist+1 samples) where the 3-px smoothed distance to the
background exceeds half the contrast for persist+1 samples **and** those samples are closer to the paper
than half the contrast (a detached dust speck, or a light scratch on a dark frame, is not paper). Robust
line fit (LMedS-initialised Tukey IRLS) on the lines outside the corner zones; the line is translated outward
until every accepted crossing (isolated single-line outliers excepted) is inside — **containment by
construction** — plus the safety margin (default 1 px). Corners = intersections.
**Locked sides**: a side whose probes 1.5 mm outside are foreground in the stage-A mask (> 30 %) abuts a
neighbour; it keeps the proxy estimate plus the safety margin (no background to measure against),
`touching_neighbour`, confidence ≤ 0.55.
Inner quad edges: same sampling, frame→image crossing, robust fit; a fit that moves more than 0.9·half from the
proxy estimate is rejected. Round frames: 180 radial rays, robust circle fit.

## Inner detection (proxy)
Per side of the print rectangle: frame colour F and spread τ from a band 1–3 mm inside the edge; for each
depth, the fraction of non-frame pixels and the line-averaged gradient |ΔE(d+1) − ΔE(d−1)| over the central
60 % of the side. For every film type × 4 orientations within size tolerance, the predicted border per side
selects the strongest gradient peak within ±max(1.5 mm, 30 % of the border) weighted by distance to the
prediction (peak/texture ≥ 2.5 = found; ≥ 4 = strong). Score = size error + per-side distance to prediction +
corner evidence (round frames: the image-square corners are frame coloured). Best (type, orientation) wins;
the inner quad uses found edges, the prior elsewhere (`inner_prior_fallback`). Left/right asymmetry
> max(1.5 mm, 35 %) → `inner_asymmetric`. Round frames: radial refinement on the proxy, circle prior otherwise
(`inner_circle_prior`). Orientation = the type's upright border layout (widest border at the bottom).

## Confidence
Outer: contrast/noise (< 5 → ×0.3 `low_contrast`, < 10 → ×0.75), valid line fraction (< 0.6 → ×0.6
`refine_residual`), residual spread > 3 px ×0.8, unrefined sides ×0.6, rectangularity < 0.9 ×0.8, split ×0.55,
touching scan edge ×0.5, no film type ×0.6, caps for `frame_not_visible` (0.3), `low_frame_contrast` and locked
sides (0.55). Inner: found edge count/strength, symmetry, full-resolution valid fraction. Combined = min(outer,
inner); < 0.6 low (must review), 0.6–0.85 medium, ≥ 0.85 high. A count different from the expected count adds
`count_mismatch` to every item and a scan warning. Reasons are machine-readable strings.

## Known limits (measured, TEST-PLAN §4)
A frame whose colour is within ΔE ≈ 4 of the background (black frame on a black backing) cannot be outlined
from pixels: the print is either contained conservatively and flagged, or missed with a count warning when
its picture is dark too. Users are told to scan dark frames on a light backing (docs/HUMAN-VERIFICATION.md,
in-app hint).

## Ordering
Rows by clustering print centres on y (tolerance ½ median print height), left→right within a row.

## Templates
`fk.template/1`: slots with relative quads (outer, inner), film type, DPI at creation. Applying: scale to the
new scan, then stage B snapping; snap offset > 1.5 mm lowers confidence.

## Determinism
No randomness; identical input → identical output on every platform (tests compare to tolerance 1e-6 px).
