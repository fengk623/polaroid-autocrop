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
9. **Effective resolution**: scanner software may scale the output (measured: 103 %) and keep the nominal
   DPI tag. Components with a card's aspect ratio (±2 %) and size (±6 %) give the median scale k; when
   |k − 1| ≥ 1.5 % the DPI used for detection becomes DPI·k (`dpi_rescaled` warning). Not applied when the DPI
   is unknown.
10. **Single-print scans** (cropped to one print in the scanner software): when no candidate is a card and the
   scan itself is card sized (−8 … +12 mm), the whole scan is the print (`print_fills_scan`). Thin background
   slivers along the border (colour differs from the frame band 3–4 mm inside, ending within 3 mm) are
   trimmed. Sides on the scan border are locked (not refined, not counted in refinement statistics) and the
   outer is clamped to the scan; confidence ≤ 0.8 (check recommended), since the frame may continue beyond
   the scan.
11. Film type and orientation: see Inner detection.

## Stage B — full-resolution refinement (memory O(band))
All profile lines of all prints are sampled in one increasing-row pass (`readRows`, 64-row bands). Outer
lines: ≤ 200 per side, perpendicular to the proxy edge, from `half = 4·s + 16` px outside to `half` + 2 mm
inside (the inner end must be real paper even when the proxy outline includes a scanner shadow).
**Outer crossing, paper side first (since 0.7.0-alpha.2, U012, UX-02).** Per line: paper = median of the inner
fifth, backing = median of the outer fifth. Walking from the paper end outward, the paper ends at the first of
(each persisting, on 3-px smoothed values):
- *toward the backing*: the colour projected on the paper→backing direction passes half way. On a backing
  lighter than the paper, where the print casts a soft shade rising to the lid, a quarter of the way — but only
  on such a ramp (quarter → three quarters wider than max(6 px, 0.2 mm)); on a sharp step the quarter point is
  still inside the blurred edge (a generated scan showed a 0.74 px cut), so half way. On a darker backing a
  rolled, darker paper edge ("shoulder") stays paper until half way.
- *darker than the paper on a lighter backing*: the luminance drops below the paper's by 0.3 of the
  backing–paper luminance difference (persist samples; two samples when the drop reaches 1.5× that) — the
  shadow band (≈ 0.5–2 mm, darker than paper and lid) and the dark edge line of a white print on a white lid.
  A colour shift without darkening (yellowed paper) stays paper.
The previous rule crossed from the backing side and landed on the outer end of that band: on the owner's
826 dpi white-on-white scan 0.9–1.9 mm outside the paper. Lines whose inner end is not this side's paper
(colour ≠ the side's median paper by > max(0.25 C, 4σ)) are dropped — near the corners they run into the
neighbouring side's shadow. The contrast is judged against the backing's noise.
**Profiles cut off by the scan border** (tightly cropped single-print scans): their backing is the most distant
in-scan samples and their threshold uses the print's contrast from complete profiles; when the paper reaches
the border, the border position is a containment point (not a fit point) — the paper continues outside the scan.
Robust line fit (LMedS-initialised Tukey IRLS) on the lines outside the corner zones; the line is translated
outward until every accepted crossing is inside — **containment by construction** — plus the safety margin
(default 1 px). Not counted for the translation: isolated single-line outliers beyond 4σ + 2 px, and outliers
beyond that gate inside the corner zones (the corner is contained by the two side lines; profiles there cross
the neighbour side's shadow or the rounded corner). Corners = intersections.
**Single-print scans**: a side whose line passes its own scan border (the paper leaves the scan there) becomes
that border; clamping its corners instead turned the side and cut paper between them (owner's img001: 3–5 px).
**Locked sides**: a side whose probes 1.5 mm outside are foreground in the stage-A mask (> 30 %) abuts a
neighbour; it keeps the proxy estimate plus the safety margin (no background to measure against),
`touching_neighbour`, confidence ≤ 0.55.
Inner quad edges: same sampling, frame→image crossing, robust fit; a fit that moves more than 0.9·half from the
proxy estimate is rejected. Round frames: 180 radial rays, robust circle fit.

## Inner detection (proxy)
Per side of the print rectangle: frame colour F and spread τ from a band 1–3 mm inside the edge; for each
depth, the fraction of non-frame pixels and the line-averaged gradient |ΔE(d+1) − ΔE(d−1)| over the central
60 % of the side. For every film type × 4 orientations within size tolerance, the predicted border per side
selects the strongest real edge (peak/texture ≥ 2.5; ≥ 4 = strong) within ±max(3 mm, 50 % of the border),
weighted by a Gaussian of the distance to the prediction; texture peaks are used only when no real edge
exists. Real Polaroid 600 prints measured 5.6 / 6.8 / 7.9 / ≈21 mm borders (left/right/top/bottom) against
the nominal 4.5 / 4.5 / 6 / 22 mm, so the window must be wide. Score = size error + per-side distance to prediction +
corner evidence (round frames: the image-square corners are frame coloured). Best (type, orientation) wins;
the inner quad uses found edges, the prior elsewhere (`inner_prior_fallback`). Left/right asymmetry
> max(1.5 mm, 35 %) → `inner_asymmetric`. Round frames: radial refinement on the proxy, circle prior otherwise
(`inner_circle_prior`). Orientation = the type's upright border layout (widest border at the bottom).

## Confidence
Outer: contrast/noise (< 5 → ×0.3 `low_contrast`, < 10 → ×0.75), valid line fraction (< 0.6 → ×0.6
`refine_residual`), residual spread > 3 px ×0.8, unrefined sides ×0.6, rectangularity < 0.9 ×0.8, split ×0.55,
touching scan edge ≤ 0.8, single-print scan ≤ 0.8, no film type ×0.6, caps for `frame_not_visible` (0.3), `low_frame_contrast` and locked
sides (0.55). Inner: found edge count/strength, symmetry, full-resolution valid fraction. Combined = min(outer,
inner); < 0.6 low (must review), 0.6–0.85 medium, ≥ 0.85 high. A count different from the expected count adds
`count_mismatch` to every item and a scan warning (manual count only, see below). Reasons are machine-readable
strings.

## Print count (UX-01, since 0.7.0-alpha.1)
Each scan is counted **automatically** (default for new scans) or against a **manual** expected count (1–12).
The count never changes what is detected: no outline is made up to reach a number and no two objects are
merged; it only decides the `count_mismatch` warning.
- `DetectParams.expectedCount = 0` = automatic: no `count_mismatch`. The app passes 0 for automatic scans.
- `updateCountWarning(ScanRecord&)` re-derives `count_mismatch` (scan warning and item reasons) from the scan's
  current setting and its prints, without detecting again: switching automatic ↔ manual or changing the number
  updates the warning at once. The manual number is kept while the scan is automatic.
- Hints kept in both modes: low confidence (per print), `too_many_candidates` (more than `maxCount` = 12
  objects; the 12 largest are kept), `no_prints_found` (zero prints) and `possible_missed: N`.
- `possible_missed: N` counts objects that may be prints the detection left out:
  (a) components of the detection mask with 25–60 % of the smallest image area (Instax Mini 46 × 62 mm), centre
  outside every print — too large for dust, too small for any print (e.g. the visible part of a print whose frame
  matches the background); (b) **faint print-sized areas**: on a 1 mm grid, cell means compared with a quadratic
  background surface fitted to the 5 mm border band only (the detection's own background model also fits faint
  objects and hides them); cells with ΔE > max(1.5, 4·σ) outside the found prints (σ: median + robust spread of
  the border residuals), opened and closed with a 5 mm square, components of at least 60 % of the smallest image
  area that fill ≥ 70 % of their bounding rectangle. Corpus: faint prints filled 0.59–0.99 (those below 0.7 are
  not counted, but the other prints of the same scan still raise the hint); the one uneven background 0.57.
- Measured (`fkcli eval`, generated corpus of 24 scans): the 3 scans with missed (low-visibility) prints are all
  flagged without a count (`no_prints_found` or `possible_missed`), no complete scan is flagged; the owner's 4 real
  scans (1200 dpi single prints, an 800 dpi scan of four) have no `possible_missed`. `eval --enforce` requires
  both (`missed_scans_flagged_automatic == missed_scans`, `false_possible_missed == 0`).

## Known limits (measured, TEST-PLAN §4)
On a backing lighter than the paper, a paper edge that is itself darker than the paper by more than 0.3 of the
backing–paper luminance difference (a dirty or darkened edge) ends the paper early: that edge would be cut.
Generated and real scans do not show it; it is the price of excluding the shadow band (real-scan GT, U012).
A frame whose colour is within ΔE ≈ 4 of the background (black frame on a black backing) cannot be outlined
from pixels: the print is either contained conservatively and flagged, or missed with a count warning (manual
count) or a `possible_missed` / `no_prints_found` hint (automatic count) when its picture is dark too. Users are told to scan dark frames on a light backing (docs/HUMAN-VERIFICATION.md,
in-app hint).

## Ordering
Rows by clustering print centres on y (tolerance ½ median print height), left→right within a row.

## Templates
`fk.template/1`: slots with relative quads (outer, inner), film type, DPI at creation. Applying: scale to the
new scan, then stage B snapping; snap offset > 1.5 mm lowers confidence.

## Determinism
No randomness; identical input → identical output on every platform (tests compare to tolerance 1e-6 px).
