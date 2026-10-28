# F05 — Export (image area or full print)

RFC: `rfc/0005-export.md`, `rfc/0002-geometry.md`, ADR-0004 (DNG), ADR-0014 (resampling) · Tests: `tests/core/test_export_*.cpp`

## Geometry modes (always shown before export)
1. **Pixel crop** (default, archival): axis-aligned bounding box of the boundary, no rotation or
   resampling; optional *lossless upright* (90° multiples) reorders pixels without changing values.
2. **Straightened**: one Lanczos-3 resampling; UI states "resampled".
3. **Standard size**: straightened + scaled to a user size or film nominal size × DPI; UI shows the scale factor.

## Formats
- TIFF 8/16-bit; none/LZW/ZIP(+predictor); ICC, DPI, XMP IDs; BigTIFF automatically above 4 GB.
- PNG 8/16-bit; iCCP, pHYs, iTXt XMP.
- JPEG 8-bit, quality default 100, 4:4:4; ICC (APP2), density, XMP (APP1); explicit "lossy" warning.
- Linear DNG (16-bit, LinearRaw, ColorMatrix from the source ICC); UI explains that 16-bit TIFF is the
  recommended scan format and that Linear DNG contains no camera sensor data.

## Before export the dialog shows
Geometry mode, resampled yes/no, output pixel size, bit depth, ICC, format, compression, estimated size,
whether frame pixels stay bit-identical (and why not), free space at target.

## Safety
- Write to temp file → flush → atomic rename. Never overwrite: `_v002` etc. on clash.
- Re-read after write: size, depth, ICC, DPI, pixel hash of a sample of rows must match.
- Disk-space check before writing (estimate + 10 %).
- Sidecar `<name>.fk.json` next to every image-area export (F06).

## Acceptance
- A1 Pixel-crop TIFF of the full print equals the source region bit-for-bit (all depths, compressions). (`test_export_crop`)
- A2 16-bit source never silently becomes 8-bit; JPEG export of a 16-bit source requires `allow_depth_reduction`. (`test_export_depth`)
- A3 Output carries the source ICC (byte-identical) unless a conversion was requested. (`test_export_icc`)
- A4 DPI preserved (scaled for standard size). (`test_export_dpi`)
- A5 Exported DNG passes the structural validator and decodes with LibRaw to values matching the TIFF within 1 LSB (linear). (`test_dng`)
- A6 Export of a 1600 DPI print streams with peak memory < 600 MB (no rotation) / < 1.2 GB (lossless 90° rotation). (`PERFORMANCE-BUDGET.md`)
- A7 Name clash never overwrites. (`test_export_safety`)
