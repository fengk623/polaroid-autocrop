# PROGRESS

Read this first after any interruption. Newest entry on top.

## Current state
- Phase: 2 (core) complete locally → push, CI on macOS/Windows, then Phase 3 (Qt app)
- Branch: `phase-2-core`
- Last full test run: 2026-10-28, Linux, 134/134 passed (incl. `eval_corpus`), no skips
- Blockers: none. Host Qt 6.8.3 (Quick, Controls, offscreen) built at work/qt-host for UI tests.

## Log
### 2026-10-28 — Phase 2 (core)
- fkcore: TIFF source (strips/tiles/scanlines, LRU strip cache, sequential decode for single compressed strips — trap T10), sinks (TIFF/BigTIFF/Linear DNG/PNG/JPEG, temp→rename, never overwrite), colour (lcms2, NOOPTIMIZE for 16-bit — T11), metadata/XMP/privacy, geometry, OutputStage, export, composite, synthetic frame, relink signatures, projects/sidecars/templates.
- Detection: hysteresis mask, card test with tight size tolerance (T12), hull splitting of touching/offset prints, faint-frame search, conservative expansion for invisible frames, stage B with shadow-proof profiles (T13) and locked sides for touching neighbours, gradient-based inner edges.
- Eval (24-scan corpus): cuts 0, visible missed 0, slack median 1.18 px / P95 1.77 / max 8.0, inner median 0.08 px, type/orientation 100 %, low-confidence recall 100 %. 10 black-on-black prints missed with count warning (documented limit).
- Perf (2 vCPU VM): 1600 DPI A4 16-bit 1.68 GB — detect 18 s, peak RSS 322 MB; buffered upright export peak RSS 558 MB.
- Tests found and fixed: sidecar stem regex had unbalanced parentheses; robust line fit failed with gross outliers (LMedS init); 2 touching minis accepted as one Instax Wide.
### 2026-10-28 — Phase 0 + Phase 1
- Cloned PhotoFrame (02807a0) and SpektraLab (fd891c6) outside the repo; audit notes → `docs/reference-audit.md`, `docs/ui-reference.md`.
- Dev sandbox facts: apt/pip/npm blocked; GitHub git + release downloads work → dependency superbuild from pinned tags; host Qt 6.8.3 built from GitHub sources (minimal modules) for local UI tests.
- Wrote PRD overview + F01–F15, ADR-0001…0014, RFC-0001…0016, API-SPEC-core, CONTRACT-ui-core, TEST-PLAN, PERFORMANCE-BUDGET, RELEASE-CHECKLIST, SECURITY-PRIVACY, THIRD-PARTY-LICENSES, docs/*.
- Next: CMake skeleton + CI skeleton (Linux/macOS/Windows build an empty core), then Phase 2.
