# RFC-0006 — Relink

Status: Accepted · Module: `core/src/relink.cpp`, `core/src/relink_assess.cpp` · ADR-0009 · PRD F06 · Tests: `tests/core/test_relink.cpp`

- `Signature computeSignature(const ImageBuffer&)`: luma (Rec.709 weights on encoded values) → box-resample
  to 64×64 → rank transform (ties averaged) → zero-mean, unit-norm vector.
- `double compareSignatures(a, b)` = Pearson correlation; `bestRotation(a, edited)` tries 0/90/180/270.
- `RelinkCandidate match(path, const Project&)`: XMP (`fk:ItemID`, `fk:EditVersionID`, `xmpMM:DocumentID`) →
  sidecar (exact name, then stems with editor suffixes removed: `-Edit`, `_edit`, ` copy`, ` (1)`, `-1`…`-9`)
  → file-name regex `^(.*)_P(\d{2,3})_EDIT_v(\d{3,})` → none.
- `RelinkCheck check(candidate, edited)`: size/aspect/orientation, bit depth, ICC presence/equality,
  signature correlation (pass ≥ 0.80), producing `actions[]` the UI must confirm (scale, rotate, assume ICC, accept lower depth).
- Sidecar schema `fk.sidecar/1` (`docs/project-format.md §4`).

## Relink v2: target first, evidence model (v0.6.0-alpha.4, U004, UX-07)

Status: Accepted (2026-10-30), implemented in U004. Replaces "first source that hits decides" (owner's review §5):
matching sources are **evidence**, weighed together; a conflict is shown, never resolved silently.

### Order of decisions
1. **Target print.** Either chosen by the user ("Relink to this print…" in the Return tab, or "Assign to …" in the
   relink dialog — target first) or inferred from evidence (batch relink).
2. **Export version.** Among the target's export records (a print can be exported several times with different
   geometry), the one the evidence supports.
3. **Compatibility.** Size/aspect, rotation, bit depth, ICC and content against that version's record
   (`checkEdited`); every needed action is confirmed by the user before compositing.

### Evidence for one option (print × export version)
| Evidence | Supports when | Contradicts when | Weight |
| --- | --- | --- | --- |
| File SHA-256 | equals the record's file SHA-256 (unchanged export) | — | decisive (+10) |
| Embedded IDs (XMP) | item id equals the option (+1 if the edit-version id does too) | the file names another print of this project | strong (±3) |
| File name `<scan>_Pnn_EDIT_vNNN` (editor suffixes `-Edit`, `_edit`, ` copy`, ` (n)`, `-n`, `_n` stripped) | scan stem and print index equal the option (+0.5 if the version does too) | names another print of this project | weak (±1) |
| Content signature (best of 4 rotations) | correlation ≥ 0.80 | correlation < 0.50 | +2…+4 / −2 |
| User target | option belongs to the chosen print | other prints are not considered | restricts |

A file name or a similar picture never overrides stronger contradicting evidence: if the IDs name print A and the
content matches print B, the option carries a **conflict** and the decision is **ambiguous**.

### Decisions
- **unique** — the best option is *corroborated*: the file hash, or IDs/name **and** a content pass, or the user's
  target **and** a content pass; it has no conflict; and no other option passes the content check without any
  contradicting evidence (an option of the same print that the IDs/name do not pick as the export version does not
  count). Pre-selected; pre-accepted only for an existing export whose size and content match.
- **ambiguous** — some evidence supports an option but the rule above is not met (similarity alone, a conflict,
  several plausible prints or exports). Nothing is pre-selected; the dialog lists each candidate with its evidence.
- **none** — nothing supports any option. The row offers "Assign to …" (every print of the project), which
  re-assesses the file with that target.

The dialog lists only options that some evidence supports (with a target: all of that print's exports and the new
alignment); every other print stays reachable through "Assign to …".

### New alignment (no export history)
When the chosen target has no export that matches (exported by another project, or the project was not saved) the
user may explicitly **establish a new alignment**: the graded image is placed on the print's *current* Inner.
- Shape: an unresized export has exactly the size of a pixel crop or of the straightened image area (either
  orientation) — that shape is used (the pixel crop, the default export, when both have that size); otherwise the shape whose aspect ratio is closer; when both fit within the
  scaling tolerance (0.5 %, the tolerance of every scaled graded file in compositing) the picture decides
  (`alignmentGeometry` with the preview).
- Allowed only within that tolerance (after a 90° turn if needed); otherwise refused ("crop or resize it to the
  image area first") — never stretched.
- Content check: the graded file's signature against the print's current image area sampled from the scan preview
  the way an export would be (`signatureOfExport`: same geometry, upright, box-filtered).
- Never pre-accepted: the user ticks the row. The record created is marked `origin: "manual_alignment"` (not an
  export: no export date; file name and SHA-256 are the graded file's), with the next version number of the print, so
  no history is faked. Relinking the same file again is then decided by its hash.
- If the print's boundaries change between the check and "Relink", the alignment is not applied (re-check needed).
- An unchanged export placed by a new alignment of the same shape composites bit for bit (self-test).

### Drop routing
Window drop: `.fkproj` opens; a TIFF that carries our IDs or has an export name is a graded file; other TIFFs import
as scans; every other image is a graded file (`AppController::dropFiles`). The menu/rail command relinks any file.

### Compatibility
`origin` is written only for manual alignments (absent = export), so files of earlier versions read unchanged and
export records written by this version are byte-compatible. 0.6.0-alpha.3 reads a manual-alignment record as an
export record (it ignores `origin`), which places the file the same way; versions before alpha.3 ignore
`exportRecords` altogether (unknown top-level key, preserved).

### Not in this unit
Persisting the confirmed rotation/scale and using it in preview and output (FK-002, U005), ICC choice (FK-003,
U006), re-verification of changed files (FK-007, U007), recovery snapshot of an unsaved project (U008).

### API (core/project.hpp)
`describeGradedFile(path, pixels)` (embedded IDs, SHA-256, signature, size);
`assessRelink(input, project, targetItemId = {}, targetSignature = nullptr)` → options with evidence + decision;
`alignmentGeometry(scan, item, gradedSize[, preview, downscale, gradedSignature])`;
`signatureOfExport(image, geometry, downscale)`;
`alignmentRecord(project, scan, item, geometry, areaSignature, gradedFileName, gradedSha256)`.
Tests: core `RelinkV2.*` (12), app self-test UX-07 checks (9; they replace the 3 former UX-07 known issues).

## Confirmed transform (v0.6.0-alpha.5, U005, FK-002)

Status: Accepted (2026-10-30), implemented in U005. Before: the output re-guessed the orientation at every composite
and ignored it whenever the graded file kept the export size (`sizeMatches ? 0 : suggestedRotate`), so a graded file
turned by 180° was placed upside down (FK-002 known issue).

- **Turns the size allows.** Same aspect as the export → {0°, 180°}; swapped → {90°, 270°}; a square image area →
  all four; none → a different crop (manual alignment, never stretched). The picture is compared only among these.
- **Reliable or the user's choice.** The best allowed turn is used when its picture correlation passes (≥ 0.80) and
  leads every other allowed turn by ≥ 0.15 (`kOrientationMargin`). Otherwise — pictures symmetric under the turn, low
  texture — the action `choose_rotation` asks the user (dialog "Orientation"; the row cannot be ticked until chosen;
  `applyRelink` refuses an unchosen one).
- **Confirmed, then stored.** Ticking a row confirms its turn and, separately, the resampling to the export size
  (`scale`, same aspect after the turn) and a lower bit depth. Any turn or resampling needs that tick (no
  pre-accepting). Stored per relinked edit version as `relinked.transform` {rotate, rotateSource none|content|user,
  scale, lowerDepth, contentScore, gradedSize}.
- **Used as stored.** Composite preview and final output take the stored transform; nothing is re-guessed. A graded
  file whose size differs from the confirmed size is refused ("changed since it was relinked") — full change
  detection is FK-007/U007. Relinks saved by earlier versions (no transform) get the turn only when the picture
  decides it unambiguously; otherwise the user is asked to relink.
- Crop stays separate: export geometry is versioned per export record (U003); a graded file with a different crop is
  a manual alignment, not a scale.

Compatibility: `transform` is a new member of `edits[].relinked` in `fk.project/1`. Earlier versions ignore it when
reading and drop it when they save; reopened here, such a relink behaves like one of an earlier version (above).
Tests: core `RelinkTransform.*` (3); self-test FK-002 checks (5, replacing the known issue), relink scenario R7.
