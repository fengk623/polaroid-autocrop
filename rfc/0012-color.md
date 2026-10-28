# RFC-0012 — Colour

Status: Accepted · Module: `core/color` · ADR-0003 · PRD F12 · Tests: `tests/core/test_color.cpp`

- `Profile` (RAII over `cmsHPROFILE`): `fromIcc(bytes)`, built-ins `sRGB()`, `adobeRGB1998()` (generated
  from published primaries, γ 563/256), `proPhoto()` (D50, γ 1.8), `gray22()`, `linearRec709()`; `description()`, `sha256()`.
- `Transform` cached by (src hash, dst hash, format, intent, flags); formats RGB8/RGB16/Gray8/Gray16/RGBA*;
  default intent relative colorimetric + BPC; `apply(rows)` is thread-safe (cmsFLAGS_NOCACHE off, one transform per thread).
- Policies: `AssignProfile`, `ConvertToProfile`, `KeepUntagged`. Logged with the operation name.
- Display: `DisplayTransform(imageProfile, displayProfile)` → RGBA8.
- Linearisation for DNG: TRC inversion via a transform to the profile's own primaries with γ 1.0.
