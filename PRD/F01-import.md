# F01 — Import, scan inspection, damaged files

RFC: `rfc/0003-image-io.md`, `rfc/0009-project.md` · Tests: `tests/core/test_tiff_io.cpp`, `tests/core/test_source_untouched.cpp`

## Problem
Scans are huge, varied TIFFs. The user must be able to bring in one file, many files or a folder, and
see immediately what each file is (size, depth, ICC, DPI, compression) without the app ever writing to it.

## Scope
- Import: file dialog (multi-select), drag and drop, folder (non-recursive by default, recursive option), recent projects.
- Supported: TIFF / BigTIFF; 8- or 16-bit unsigned; RGB, RGBA (alpha preserved, ignored for detection),
  grey (MinIsBlack, MinIsWhite); uncompressed, LZW, Deflate/ZIP (incl. predictor 2), PackBits, JPEG-in-TIFF;
  strips or tiles; contiguous or separate planes; with or without ICC.
- Rejected with a clear message: palette, CIELab, 32-bit float, 1-bit, CMYK, multi-page TIFF beyond page 1
  (page 1 is used; user is told).
- Integrity check at import: header and IFD readable, strip/tile offsets inside the file, byte counts
  sane, first and last row decode. Damaged files are listed with the reason and skipped; other files continue.
- Files are opened read-only; the app never requests write access to a source file.
- Relocation: if a referenced scan is missing, the user picks a folder; files are matched by name + fingerprint.

## Non-goals
Scanner control; RAW camera files; PDF.

## Acceptance
- A1 Importing 8/16-bit RGB and grey TIFFs with each compression above succeeds and shows correct size/depth/ICC/DPI/compression. (`test_tiff_io`)
- A2 BigTIFF imports. (`test_tiff_io (TiffRead.BigTiff)`)
- A3 A truncated TIFF is reported as damaged with a reason and does not stop a batch. (`test_tiff_io (TiffRead.TruncatedFileIsReportedDamaged)`, `test_batch.ContinueOnError`)
- A4 Source file SHA-256 is identical before and after import, detection, export, composite and project save. (`test_source_untouched`)
- A5 Unsupported photometrics give an explicit error code, never a crash. (`test_tiff_io (TiffRead.UnsupportedAndMissing)`)
- A6 Paths with Chinese, Japanese, spaces and emoji work on Windows (wide-char API) and macOS. (`test_tiff_io` TiffRead.UnicodePaths)
