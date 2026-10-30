# RFC-0012 — Colour

Status: Accepted · Module: `core/color` · ADR-0003 · PRD F12 · Tests: `tests/core/test_color.cpp`

- `Profile` (RAII over `cmsHPROFILE`): `fromIcc(bytes)`, built-ins `sRGB()`, `adobeRGB1998()` (generated
  from published primaries, γ 563/256), `proPhoto()` (D50, γ 1.8), `gray22()`, `linearRec709()`; `description()`, `sha256()`.
- `Transform` cached by (src hash, dst hash, format, intent, flags); formats RGB8/RGB16/Gray8/Gray16/RGBA*;
  default intent relative colorimetric + BPC; `apply(rows)` is thread-safe (cmsFLAGS_NOCACHE off, one transform per thread).
- Policies: `AssignProfile`, `ConvertToProfile`, `KeepUntagged`. Logged with the operation name.
- Display: `DisplayTransform(imageProfile, displayProfile)` → RGBA8.
- Linearisation for DNG: TRC inversion via a transform to the profile's own primaries with γ 1.0.

## Graded files without a usable profile (v0.6.0-alpha.6, U006, FK-003)

Status: Accepted (2026-10-30), implemented in U006. Before: a graded file without an embedded profile was silently
treated as being in the scan's colour space — wrong whenever the export had been converted (e.g. Adobe RGB scan →
sRGB export: colours shift by ~1 % mean in the test), and an unreadable embedded profile failed only at output.

- **Detection** (`checkEdited`): `iccMissing` (no profile, the export had one) or `iccInvalid` (embedded but lcms
  cannot read it) → action `choose_icc`.
- **Choices** (`iccChoicesFor(record, scanIcc, stored)`): the export's own profile — identified only by the record's
  SHA-256 among the scan's profile, the built-ins (sRGB, Adobe RGB (1998)) and the project's stored profiles, never
  by name; the scan's profile; sRGB; Adobe RGB; a profile file chosen by the user (`AppController::loadIccProfile`,
  refused when lcms cannot read it). Duplicates by content are listed once.
- **Confirmation**: the export's profile is pre-selected only for a *missing* profile that was identified; an
  unreadable one has no pre-selection. The row is never pre-accepted; ticking confirms; `applyRelink` refuses a row
  with `choose_icc` but no chosen profile.
- **Persistence**: `edits[].relinked.transform.iccSource` (export/scan/srgb/adobe_rgb/file) + `iccSha256`; the bytes
  go into the project's top-level `iccProfiles` (SHA-256 → base64, verified on load; a mismatching entry is
  dropped), so the choice resolves after reopening even if the profile file or the scan is gone.
- **Use** (`gradedProfile`): confirmed interpretation → its bytes (error if no longer available); else a usable
  embedded profile; else none for an untagged export (the scan's space); relinks of earlier versions get the
  export's profile only when it is identified (what the old dialog promised: "assume the exported one"); otherwise
  an error asking to relink. Compositing converts once from that profile to the scan's profile; frame pixels stay
  the scan's own.

Compatibility: `iccProfiles` is a new top-level key (preserved verbatim by earlier versions); `iccSource`/`iccSha256`
live in `relinked.transform` (dropped by earlier versions on save → handled as an earlier version's relink).
Tests: core `GradedProfile.*` (3: Adobe RGB scan → sRGB export → untagged graded file restored with mean error
< 0.1 % vs ≈ 1 % when assumed to be in the scan's space, frame unchanged; unreadable/unknown profiles not guessed;
saved with the project); self-test FK-003 checks (4).
