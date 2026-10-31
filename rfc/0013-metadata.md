# RFC-0013 — Metadata

Status: Accepted · Module: `core/metadata` · PRD F13 · Tests: `tests/core/test_metadata.cpp`

- `MetadataSet { dpi, unit, icc, orientation, descriptive (Make, Model, DateTime, Artist, Copyright,
  ImageDescription, DocumentName), exifSubset, xmpPacket, privateTags[] }` read from TIFF/PNG/JPEG.
- `classify()` → each field tagged Safe / Location / Identity / Unknown.
- `PrivacyPreset { KeepAllSafe, RemoveLocation, RemoveIdentity }`.
- `buildXmp(ids, frameMode, sourceXmp?)`: our packet with namespaces `fk`, `xmpMM`, `dc`; from the source XMP
  only `dc:title/description/creator/rights` are carried over.
- Writers never copy structural tags. Orientation is always written as 1 with pixels physically ordered.

## Privacy presets on every channel (v0.6.0-alpha.9, U009, FK-004)

Status: Accepted (2026-10-30), implemented in U009. Before: "Remove location and identity" cleared the TIFF Artist
and HostComputer tags, but `buildXmp` still copied `dc:creator` from the source XMP into the XMP packet of every
output (TIFF, PNG, JPEG, DNG) — the creator's name left with the file (known issue
`FK004_RemoveIdentityDropsCreatorFromSourceXmp`).

- `applyPrivacy` is the single filter; its result (`MetadataSet`, now carrying the preset) is the only input of
  every writer: TIFF/DNG tags (tiff_sink) and the XMP packet (TIFF, PNG iTXt, JPEG APP1, DNG). `buildXmp` copies
  `dc:title`, `dc:description`, `dc:rights` always and `dc:creator` only when identity is kept.
- Never written by any preset: GPS (no GPS IFD), EXIF owner and serial numbers (no EXIF IFD is written), any other
  XMP from the source.
- **Stated explicitly**: `writtenMetadata(filtered, TagChannel)` lists exactly what an output carries per channel
  (TIFF tags; DNG tags — whose Make/Model name FrameKeeper's linear scan; XMP dc fields), marking identity fields. The
  Export tab's privacy section shows this list for the current scan, preset and format, and what is never written.

Tests: core `Privacy.*` — the former known issue (now regular), the per-channel list, and an independent read-back of
TIFF, PNG, JPEG and DNG outputs (raw bytes searched) for all three presets from a scan with identity in TIFF tags and
XMP; every listed value is present in the file, nothing identity-related when removed. A mutation that copies the
creator again fails 3 of them. Self-test FK-004 ×2 on a copy of the scan carrying identity.
