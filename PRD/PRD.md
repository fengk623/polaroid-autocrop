# FrameKeeper — Product Requirements (overview)

Status: v1 scope frozen for 0.x development · Owner: project maintainers · Last updated: 2026-10-28

Development codename: *Polaroid AutoCrop* (repository `polaroid-autocrop`). Product name: **FrameKeeper**
(ADR-0012). "Polaroid" and "Instax" are third-party trademarks and are only used descriptively.

## 1. User problem

Photographers digitise instant-film prints (Polaroid 600 / SX-70 / i-Type, Polaroid round-frame,
Instax Mini / Square / Wide) on flatbed scanners such as the Epson Perfection V750 Pro, typically four
prints per scan at 800–1600 DPI. Each scan is a 100–1500 MB TIFF. Today they:

1. crop every print by hand in Photoshop, frequently shaving off part of the real frame;
2. colour-grade the picture, but the grade also hits the frame (yellowing, ink, stains change colour);
3. lose the original frame when they export a "clean" image, or keep it and cannot grade the image alone.

The frame is part of the object: handwriting, ageing, stains and texture are the archive.

## 2. Positioning

A focused, offline, lossless desktop tool for macOS and Windows that:

- finds every print in a scan and its exact outer edge without ever cutting into the paper;
- finds the image area inside each print;
- exports the image area for grading in any editor, and later puts the graded image back inside the
  untouched scanned frame;
- archives the full print losslessly (bit depth, ICC and DPI preserved).

It is not an editor. Grading happens in Photoshop / Lightroom / Capture One / Affinity / GIMP.

## 3. Non-goals (v1)

- Colour grading, retouching, dust removal inside the image.
- Scanner control (TWAIN/ICA). Files only.
- Cloud sync, accounts, telemetry, any network access.
- Mobile releases (architecture keeps them possible: `docs/mobile-portability.md`).
- GPU-only processing paths.
- "Cleaned real frame" display mode (deferred to v1.1; not shown in v1 UI).

## 4. Features (one PRD each)

| ID | Feature | PRD | v1 |
| --- | --- | --- | --- |
| F01 | Import, scan inspection, damaged-file handling | `PRD/F01-import.md` | yes |
| F02 | Outer (paper) detection with full-resolution refinement | `PRD/F02-outer-detection.md` | yes |
| F03 | Inner (image area) detection incl. round frames | `PRD/F03-inner-detection.md` | yes |
| F04 | Review and manual boundary editing | `PRD/F04-review-editing.md` | yes |
| F05 | Image-area / full-print export (TIFF, PNG, JPEG, Linear DNG) | `PRD/F05-export.md` | yes |
| F06 | Relink graded files (re-import) | `PRD/F06-relink.md` | yes |
| F07 | Composite into the real frame and final export | `PRD/F07-composite.md` | yes |
| F08 | Synthetic frame (PhotoFrame-style) output mode | `PRD/F08-synthetic-frame.md` | yes (basic) |
| F09 | Fixed jig templates | `PRD/F09-templates.md` | yes |
| F10 | Batch processing and grid review | `PRD/F10-batch.md` | yes |
| F11 | Non-destructive projects | `PRD/F11-projects.md` | yes |
| F12 | Colour management | `PRD/F12-color.md` | yes |
| F13 | Metadata and privacy | `PRD/F13-metadata.md` | yes |
| F14 | Cache, memory, diagnostics, settings | `PRD/F14-diagnostics.md` | yes |
| F15 | Desktop UI and platform conventions | `PRD/F15-ui.md` | yes |

## 5. Key decisions (details in `rfc/`)

- C++20 core with no Qt dependency; Qt 6.8 Qt Quick desktop UI (ADR-0001).
- No libvips/OpenCV/Exiv2 in v1: streaming over libtiff plus in-house CV primitives (ADR-0001).
- Working colour space = the scan's own ICC; the frame is never colour-converted by default (ADR-0003).
- Default export geometry = pixel-exact crop; straightening is an explicit, labelled resampling (ADR-0014).
- Projects store references and geometry only, never copies of scans (ADR-0005).
- MIT licence; all core dependencies permissive; Qt used under LGPL-3.0 via dynamic linking (ADR-0011).

## 6. Measurable claims (must be backed by tests or measurements)

| Claim | Evidence |
| --- | --- |
| Original scans are byte-identical after any operation | `tests/core/test_source_untouched.cpp` (SHA-256 before/after) |
| No detected outer boundary cuts into a print on the synthetic corpus | `tools/fixtures` corpus + `fkcli eval` metric `inner_cut_violations == 0` |
| Unmodified round trip reproduces the print bit-exactly (crop mode) | `tests/core/test_roundtrip.cpp` |
| Frame pixels unchanged after compositing a graded image (crop mode, same ICC/depth) | `tests/core/test_composite.cpp` |
| Peak memory bounded and independent of batch size | `PERFORMANCE-BUDGET.md` measurements |
| Exported DNG is a valid Linear DNG | structural validator test + LibRaw decode test; Adobe `dng_validate` in CI when available |

## 7. Version scope

- **0.1–0.4** internal development milestones (phases 0–2; never released).
- **0.5.0** first public build, published as a GitHub pre-release (owner's decision, 2026-10-30): core and
  desktop app, unsigned packages for macOS (universal) and Windows (x64); items that need human verification are
  listed in `docs/HUMAN-VERIFICATION.md` and in the release notes.
- **0.6–0.8** further milestones (phases 4–5, fixes from real use).
- **0.9.0** release candidate: all v1 features, human-verification items addressed or explicitly deferred.
- **1.0.0** after human verification on real Epson scans, real macOS/Windows windows and the 8 GB device tier.

## 8. Open questions

- Q1: Exact top/bottom frame margins per film type are not published by manufacturers; detection must
  measure them (F03). Priors in `core/detection/film_types.json` are approximate.
- Q2: Real Epson V750 TIFF layout (strip size, compression, tags) to be confirmed with user samples.
- Q3: Round-frame image diameter is not published; measured per print.

## 9. Acceptance

The release acceptance list is `docs/acceptance-matrix.md`; each line maps to PRD, RFC, tests, CI job
and evidence.
