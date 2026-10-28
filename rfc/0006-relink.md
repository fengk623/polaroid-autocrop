# RFC-0006 — Relink

Status: Accepted · Module: `core/relink` · ADR-0009 · PRD F06 · Tests: `tests/core/test_relink.cpp`

- `Signature computeSignature(const ImageBuffer&)`: luma (Rec.709 weights on encoded values) → box-resample
  to 64×64 → rank transform (ties averaged) → zero-mean, unit-norm vector.
- `double compareSignatures(a, b)` = Pearson correlation; `bestRotation(a, edited)` tries 0/90/180/270.
- `RelinkCandidate match(path, const Project&)`: XMP (`fk:ItemID`, `fk:EditVersionID`, `xmpMM:DocumentID`) →
  sidecar (exact name, then stems with editor suffixes removed: `-Edit`, `_edit`, ` copy`, ` (1)`, `-1`…`-9`)
  → file-name regex `^(.*)_P(\d{2,3})_EDIT_v(\d{3,})` → none.
- `RelinkCheck check(candidate, edited)`: size/aspect/orientation, bit depth, ICC presence/equality,
  signature correlation (pass ≥ 0.80), producing `actions[]` the UI must confirm (scale, rotate, assume ICC, accept lower depth).
- Sidecar schema `fk.sidecar/1` (`docs/project-format.md §4`).
