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

## Natural blend (v0.7.0-alpha.5, U015, UX-08)

Status: Accepted, implemented in U015 (D5: "natural blend is the right direction; values from real samples").
- **Per print.** Each print keeps its own mask (`ItemRecord.composite.mask`: mode + one width, in mm when the
  scan's DPI is reliable, else px). `resolveMask(choice, dpi)` is the one conversion; the composite preview and the
  final output both call it (tests `NaturalBlend.PreviewAndFinalUseTheSameMask`, self-test `masksUsed`). Prints of
  projects before 0.7.0-alpha.5 have no own mask and keep compositing with the project's output settings in px —
  nothing already set up changes. Applying a mask to other prints is an explicit action on the selected prints.
- **Defaults for new prints** (Settings ▸ Mask for new prints): natural blend = feather **0.05 mm**; the preserve-edge
  preset is an inset of **0.02 mm**; never both at once. Without a reliable DPI: 2.4 px / 0.9 px (the 1200-dpi
  values). Hard edge, 0 px, stays available as the archival exact path. Every transition lies inside the image
  area (`maskWeight` is 0 outside): the real frame is never changed. An unchanged graded file still composites
  bit-exactly with every mask (`NaturalBlend.UnchangedEditStaysBitExactWithEveryMask`, 8/16-bit, 4 uprights).
- **How the values were chosen** — `fkcli maskeval` (tools/cli/maskeval.cpp) on the owner's scans with real-scan
  ground truth (agent-draft GT, REAL-SCAN-GT): each detected image area exported as a pixel crop, graded
  synthetically (none, exposure +12 %, warm, contrast ×1.15, fade = "normal"; strong colour shift), composited back
  at full resolution exactly as the final output, measured in a 0.15 mm band along the edge: frame contamination
  (grade on true frame pixels inside the detected area), ungraded rim (picture pixels left ungraded: a double edge),
  seam at 50 % (p99 ΔE00 after a 2×2 reduction against the ideal: frame kept, picture graded). Worst case over the
  normal grades, per print:

  | Scan (dpi) | Print | Seam 50 % hard | feather 0.03 mm | **feather 0.05 mm** | preserve 0.02 mm | Rim at 0.05 mm |
  | --- | --- | --- | --- | --- | --- | --- |
  | polaroid_92 (1200) | P01 | 3.73 | 2.14 | 2.75 | 3.00 | 0.33 px |
  | polaroid_93 (1200) | P01 | 6.61 | 4.71 | 3.64 | 3.89 | 0.11 px |
  | img20260702_0005 (826) | P01 | 3.03 | 1.32 | 2.01 | 2.51 | 0.06 px |
  | img20260702_0005 (826) | P02 | 3.71 | 2.35 | 1.54 | 2.35 | 0.01 px |
  | img20260702_0005 (826) | P03 | 5.39 | 3.47 | 2.46 | 3.63 | 0.00 px |
  | img20260702_0005 (826) | P04 | 5.18 | 3.53 | 2.42 | 3.68 | 0.00 px |
  | img001 (1200, 16-bit) | P01 | 6.19 | 4.05 | 2.95 | 3.96 | 0.12 px |

  Over all prints (grid 0.02–0.08 mm): feather 0.05 and 0.06 mm share the smallest worst case (3.64), 0.05 mm with
  the narrower rim (0.33 vs 0.48 px); 0.03 mm (the suggested trial value) leaves 4.71, 0.08 mm 3.86 with a 0.79 px
  rim. Unchanged files: seam 0 (bit-exact). The remaining seam comes from true frame pixels inside the detected image
  area (detection error, UX-02): a mask can only fade them, never move the edge outward.
- **Not promised.** Strong colour changes, misalignment and unusual paper edges: the preview measures how strongly
  the grade changes the picture just inside the edge (`edgeChange`, median ΔE00 0.2–0.6 mm inside); above
  `kStrongEdgeChange` = 10 (normal grades measured ≤ 5.6, strong ≥ 15) the right rail asks for a check at 100 % and
  a wider mask or the alignment.
- **Limits of the evidence.** 7 prints, 826 and 1200 dpi; no 1600 dpi or round image area among the real samples;
  the ground truth is an agent draft awaiting the owner's review; ΔE00 is computed treating values as sRGB. Visual
  review sheets (`maskeval --crops`, 4× edge crops: scan · hard · feather 0.05 · preserve 0.02) stay local.
