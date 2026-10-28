# ADR-0004 — DNG strategy

Status: Accepted · Date: 2026-10-28

## Decision
- Write Linear DNG ourselves through libtiff (no Adobe DNG SDK in the product): DNGVersion 1.4.0.0,
  DNGBackwardVersion 1.1.0.0, NewSubFileType 0, PhotometricInterpretation 34892 (LinearRaw),
  SamplesPerPixel 3, BitsPerSample 16, uncompressed or Deflate, UniqueCameraModel "FrameKeeper Linear Scan",
  CalibrationIlluminant1 = D50 (23), ColorMatrix1 = inverse of the source ICC's D50-adapted colourant
  matrix (XYZ D50 → linear source RGB), AsShotNeutral (1,1,1), BaselineExposure 0, LinearizationTable none.
- Pixel data: source RGB linearised through the source ICC's TRC (LittleCMS) to 16-bit linear values in
  the source primaries. Grey sources are expanded to RGB.
- Validation: (1) our structural validator (`fkcli dng-check`), always; (2) LibRaw decode test in CI;
  (3) Adobe `dng_validate` in CI when the SDK download is available — results are reported, never assumed.
- UI copy: "Linear DNG contains no camera sensor data. For scans, 16-bit TIFF is recommended."

## Consequences
No Adobe SDK licence in the shipped app. Colour accuracy of DNG in third-party RAW software is
tested within tolerance, not guaranteed to be identical (RAW converters apply their own rendering).
