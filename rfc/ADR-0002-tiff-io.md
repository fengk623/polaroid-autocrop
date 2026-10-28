# ADR-0002 — TIFF I/O library and access strategy

Status: Accepted · Date: 2026-10-28

## Decision
- libtiff 4.7.2 built from source with `chunky-strip-read=ON` and `strip-chopping=ON`.
  Source: GitHub mirror `libsdl-org/libtiff` tag `v4.7.2` (upstream GitLab is not reachable from the
  development sandbox; the tag's commit is the upstream release merge by the libtiff maintainer).
  CI may switch `FK_LIBTIFF_REPO` to upstream; versions must match.
- Three read paths, chosen per file at open time:
  1. **Tiled** files: decode tiles on demand, LRU of decoded tiles bounded in bytes.
  2. **Strips with decoded size ≤ 64 MB**: decode strips on demand, LRU bounded in bytes.
  3. **Huge strips** (e.g. one strip for the whole image): `TIFFReadScanline` (chunky reading keeps raw
     buffers small) with a forward-only cursor; a backward request restarts from the strip start and is
     logged as a performance warning. All full-resolution consumers request rows in increasing order.
- Writing: our own strip writer (rows-per-strip targeting 256 KB–1 MB raw per strip), BigTIFF chosen
  automatically when the estimated output exceeds 3.9 GB.
- Unicode paths: Windows uses `TIFFOpenW`; macOS/Linux UTF-8 paths.

## Consequences
Memory per open source is bounded by the cache budget (default 128 MB, 32 MB under memory pressure).
Random access on huge single-strip compressed files is slow; algorithms are designed to be sequential.
