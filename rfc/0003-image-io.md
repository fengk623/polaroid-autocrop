# RFC-0003 — Image I/O

Status: Accepted · Module: `core/image_io` · ADR-0002 · Tests: `tests/core/test_tiff_*.cpp`, `test_png.cpp`, `test_jpeg.cpp`, `test_dng.cpp`

## Readers
- `TiffSource` (implements `ImageSource`): `open(path)`, `info()`, `readRows(y0, nRows, x0, width, dst, dstStride)`
  delivering samples in the file's bit depth (8 or 16), interleaved, channels = samples per pixel
  (MinIsWhite inverted on read so 0 = black). Access paths per ADR-0002 (tiles / small strips / scanline
  cursor). Byte-bounded LRU for decoded strips/tiles (`setCacheBudget`). Read-only file handle.
- `integrityCheck()` validates header, IFD, offsets ⊂ file, decodes first and last rows.
- `readImageFile(path)` decodes a whole TIFF/PNG/JPEG into `ImageBuffer` (used for graded files, which are
  print-sized), with ICC, DPI and XMP.

## Writers (`ImageSink`)
`begin(header)`, `writeRows(rows)`, `finish()`, `abort()`; streaming, rows in order.
- TIFF: strips of ≈512 KB raw; none/LZW/Deflate with horizontal predictor for LZW/Deflate (16-bit
  predictor 2); BigTIFF when the estimate exceeds 3.9 GB; ICC (34675), XMP (700), resolution tags,
  Software, DateTime, whitelisted descriptive tags, optional EXIF sub-IFD subset.
- PNG: libpng, 8/16-bit, iCCP (zlib), pHYs (DPI → px/m), iTXt `XML:com.adobe.xmp`.
- JPEG: libjpeg-turbo, 8-bit only, quality 1–100 (default 100), 4:4:4, ICC in APP2 chunks, JFIF density, XMP APP1.
- DNG: TIFF writer with DNG tags (ADR-0004).
All writers write to `<target>.fk-tmp-<random>` and `rename` on `finish()`; `abort()` deletes the temp.

## Paths
UTF-8 `std::filesystem::path` on all platforms; Windows uses wide APIs (`TIFFOpenW`, `_wfopen`).
