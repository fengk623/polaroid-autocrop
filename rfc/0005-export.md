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
