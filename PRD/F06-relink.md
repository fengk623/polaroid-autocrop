# F06 — Relink graded files

RFC: `rfc/0006-relink.md`, ADR-0009 · Tests: `tests/core/test_relink.cpp`

## IDs
Project ID, Source Scan ID, Item ID, Edit Version ID (UUID v4). File name `SourceName_P01_EDIT_v001.tif`,
sidecar `SourceName_P01_EDIT_v001.fk.json`.

## Sidecar (schema `fk.sidecar/1`)
project_id, scan_id, scan fingerprint, item_id, edit_version_id, version, inner & outer shapes, geometry
mode, forward & inverse 3×3 matrices, source pixel size, export pixel size, orientation, bit depth,
ICC description + SHA-256, DPI, export file SHA-256, structure signature (64×64 rank-normalised luma),
software version, export time.

## Matching order
1. XMP embedded ID (`fk:ItemID`, `xmpMM:DocumentID = uuid:<edit_version_id>`).
2. Sidecar next to the file (name variants: `-Edit`, `_edit`, ` copy`, `-1` … are stripped).
3. File-name pattern `<scan>_P<nn>_EDIT_v<nnn>`.
4. Manual choice.
Every match is then content-checked against the stored structure signature (insensitive to tone/colour
changes). A failing check is shown; the user decides.

## Import checks
| Case | Behaviour |
| --- | --- |
| Same size | accept |
| Same aspect, different size | show scale; resample only after confirmation |
| Different aspect or rotated | refuse automatic placement; offer manual alignment (rotation 90/180/270, suggested by signature) |
| Lower bit depth | warn; continue only after confirmation |
| No ICC | ask: interpret as exported ICC, choose another, or cancel — never silent |
| Different ICC | convert once into the scan's space at composite time (F12) |

## Acceptance
- A1 Each matching path selects the right item on fixtures (XMP kept, XMP stripped + sidecar, only file name, renamed). (`test_relink`)
- A2 A graded (curves + saturation) version passes the content check; a different picture fails. (`test_relink.Signature`)
- A3 Wrong-aspect files are never placed automatically. (`test_relink.Aspect`)
