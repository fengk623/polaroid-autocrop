# RFC-0005 — Export

Status: Accepted · Module: `core/export` · PRD F05 · Tests: `tests/core/test_export_*.cpp`

## Pipeline
`exportRegion(source, request, cancel, progress) → ExportReport`
1. Validate: reviewed flag (low confidence), capability, depth reduction consent, target directory, free space.
2. Geometry: `computeExportGeometry` (RFC-0002).
3. Read: PixelCrop without rotation streams bands of 256 rows from the source rectangle directly to the sink
   (memory ≈ 2 × band). With a 90° permutation the source rectangle is buffered once (memory = region size,
   accounted before start). Straightened/standard: a sliding source-row window covering the band's
   pre-image + kernel support; Lanczos-3 per output pixel, rows parallelised over ≤ cores-1 threads.
4. Colour: optional single LittleCMS transform per band (RFC-0012).
5. Depth: 8→16 (×257) or 16→8 (round-half-up, consent required).
6. Sink: TIFF/PNG/JPEG/DNG writer (RFC-0003) with metadata from RFC-0013.
7. Verify: reopen output; check size, depth, ICC hash, DPI; hash sampled rows against the in-memory hashes taken while writing.
8. Report: output path, sizes, hashes, `frameBitExact` flag + reasons, timings, peak memory estimate.

## Naming
`<ScanStem>_P<nn>_EDIT_v<nnn>.<ext>` for image areas; `<ScanStem>_P<nn>_FULL.<ext>` for full prints;
`<ScanStem>_P<nn>_FINAL_v<nnn>.<ext>` for composites; `_SYNTH` suffix for synthetic frames. Never overwrite:
the next free version number is chosen.

## Output plan drives every prompt; labelled straightening by default (v0.6.0-alpha.10, U010, UX-02, FK-010)

Status: Accepted (2026-10-30), implemented in U010; owner's decision D4 ("labelled straightening as the default is
acceptable").

- **Plan = output.** `planExport(info, request)` (no file access) returns what will be written: size, bits, channels,
  ICC, geometry (mode, angle), interpolation ("none" / "Lanczos-3"), `frameBitExact` and the reasons it is not,
  `reducesDepth`, notes. `exportRegion` uses the same plan; a test compares plan and report (and an independent
  read-back) over format × source depth × output depth × geometry × colour policy. The app's "bit-identical" text,
  the Export tab and the export dialog are all worded from this plan (FK-010: before, the app guessed from two
  settings and claimed "bit-identical" for a 16 → 8-bit export).
- **Explicit consent for 16 → 8 bits.** The app no longer sets `allowDepthReduction` silently; the export dialog shows
  a checkbox ("Write 8 bits per channel … the extra precision is lost") and Export stays disabled until it is ticked.
- **Image area: straightened by default**, labelled "straightened (rotated x°, resampled with Lanczos-3)"; the pixel
  crop stays available and is labelled "no resampling; a tilted print's corners may include frame". Existing users
  who chose a mode keep it.
- **Round image area** (an ellipse Inner): PNG and TIFF get an alpha channel — transparent outside the circle with a
  one-pixel edge ramp; JPEG is filled white outside; Linear DNG keeps the frame in the corners (no transparency). Each
  case is stated in the plan's notes and the report. Compositing ignores the alpha (the Inner mask decides).
- Output size, interpolation and transform are recorded in the export record (geometry snapshot, U003).

Tests: core `ExportPlan.*` ×3 (the matrix; consent; round area per format), self-test: default straightening
labelled, FK-010 (no "bit-identical" claim for 16 → 8; no export without consent, one with it).

## Exact straightening of a quad image area (v0.7.0-alpha.2, U012, UX-02)

Status: Accepted, implemented in U012. "Straightened" used to rotate by the **frame's** angle and take the bounding
box of the rotated image area. On real scans the image window is not a perfect rectangle (scanner shear ≈ 0.3°)
and its angle differs from the frame's (tightly cropped scans: frame = scan border, angle 0), so the box kept
6–17 px of frame per side (owner's scans; real-scan GT G6, baseline 0.37 mm).
- A **quad** image area is now mapped projectively onto the output rectangle: its four corners are the output's
  corners, its edges the output's edges (`squareToQuad`, Heckbert). Output size = mean lengths of opposite edges
  (rounded up). One resampling with the same Lanczos-3 kernel; the label's angle is the mean of the window's top
  and bottom edges. A rotated rectangle maps exactly as before.
- Round image areas keep rotation by the frame angle plus their bounding box (the circle is clipped as before).
- Exports keep their geometry matrices in the export record, so files exported earlier composite as before;
  new alignments (RFC-0006) use the new geometry.
Measured: G6 0.37 mm → 0.05 mm (= the detected inner's own error). Tests:
`Geometry.StraightenedImageAreaFillsTheOutputExactly` (sheared window, exact corners and edges), `realeval` G6.


## Orientation of exports from a turned scan (v0.7.0-alpha.4, U014, UX-04)

Status: Accepted, implemented in U014. The whole-scan view rotation (RFC-0011 §Whole-scan rotation) never moves a
boundary, so export geometry is computed in scan coordinates as before; only the output's quarter turns follow
`exportTurns(scan, item, applyUpright)`:
- uprights applied (default): the print's own upright. The view rotation does not change which way is up for a
  print, so it is **never added** (no double rotation);
- uprights off ("as in the scan"): the scan's view rotation, so the export looks like the canvas.
Quarter turns are pixel permutations (pixel crop stays bit-exact, RFC-0002). Export records keep the geometry used
at export time, so files exported before a turn relink and composite exactly after it (RFC-0006). The same rule
gives the final output's orientation. Tests: `ScanRotation.ExportTurnsNeverAddTheViewToThePrintsUpright`; self-test
UX-04 (exports before the turn relinked after it, final = full print bit for bit; uprights off = the earlier export
turned 90° pixel for pixel; uprights on = identical whether turned or not).
