# RFC-0013 — Metadata

Status: Accepted · Module: `core/metadata` · PRD F13 · Tests: `tests/core/test_metadata.cpp`

- `MetadataSet { dpi, unit, icc, orientation, descriptive (Make, Model, DateTime, Artist, Copyright,
  ImageDescription, DocumentName), exifSubset, xmpPacket, privateTags[] }` read from TIFF/PNG/JPEG.
- `classify()` → each field tagged Safe / Location / Identity / Unknown.
- `PrivacyPreset { KeepAllSafe, RemoveLocation, RemoveIdentity }`.
- `buildXmp(ids, frameMode, sourceXmp?)`: our packet with namespaces `fk`, `xmpMM`, `dc`; from the source XMP
  only `dc:title/description/creator/rights` are carried over.
- Writers never copy structural tags. Orientation is always written as 1 with pixels physically ordered.
