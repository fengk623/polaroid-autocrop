# F13 — Metadata and privacy

RFC: `rfc/0013-metadata.md` · Tests: `tests/core/test_metadata.cpp`

## Scope
- Preserve: XResolution, YResolution, ResolutionUnit, ICC, Orientation (written to match pixels),
  DateTime, Make/Model (scanner), Artist, Copyright, ImageDescription; Software set to FrameKeeper + version.
- XMP written by us: `fk:` namespace IDs, `xmpMM:DocumentID/InstanceID`, `fk:FrameMode`.
- Listed for user decision (privacy): GPS IFD, serial numbers, owner name, maker notes, unknown private tags,
  source XMP history. Three presets (observed in PhotoFrame): keep all safe tags / remove location / also remove identity.
- Never copied: tags that would contradict the new pixels (StripOffsets, TileOffsets, sizes, compression, etc.).

## Acceptance
- A1 DPI and ICC preserved through every export path. (`Export.DpiPreservedAndScaledForStandardSize`, `Export.IccPoliciesKeepAssignConvertUntagged`)
- A2 Privacy presets remove exactly the documented tags. (`test_metadata.Privacy`)
- A3 Orientation tag always matches pixel order (1 after lossless upright). (not tested yet, planned: no test checks the orientation tag after an upright export)
