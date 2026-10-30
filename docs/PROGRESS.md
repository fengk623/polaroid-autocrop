# PROGRESS

Read this first after any interruption. Newest entry on top.

## Current state
- Version 0.5.0 (owner's decision 2026-10-30: the first public build is 0.5.0, not 0.1.0). Branch
  `release/v0.5.0` → PR → main; then release.yml → draft v0.5.0 → published as a GitHub pre-release.
- PR #1 (phase-2-core) merged into main as 379c87d on 2026-10-28; all CI and packaging checks green.
- Last full test run: 2026-10-28, CI green on Linux / macOS arm64 / Windows x64 (core 136 tests incl. `eval_corpus`,
  app self-test, packaged-app smoke tests); app self-test also passes on the owner's real scans.
- Blockers: none. Host Qt 6.8.3 (Quick, Controls, offscreen) built at work/qt-host for UI tests.

## Log
### 2026-10-30 — version 0.5.0
- First public build renumbered 0.1.0 → 0.5.0 at the owner's request (VERSION, release notes, CHANGELOG, PRD §7).
  The unpublished 0.1.0 draft release is superseded.
### 2026-10-28 — CI green, real-scan app run
- Real 6213 × 7482 scan opened at 10 % and was cut off: the canvas fitted before the proxy existed. Fixed;
  self-test now checks that the whole scan is visible at fit.
- Windows canvas converts to the primary monitor's ICC profile; `--live` self-test for HV-02/03 captures;
  self-test keeps its settings in its own folder.
- CI: separate deps caches for core/app (T17), Windows CMake prefix paths via `cygpath -m`.
### 2026-10-28 — Phase 3 (app) and Phase 6 (release pipeline)
- Qt Quick app (`app/`): AppController singleton, JobRunner lanes, ScanCanvas (proxy pyramid + full-resolution
  detail, contract overlays, handle editing, composite compare), rails/filmstrip/menus (macOS native menu bar,
  in-window menu elsewhere), dialogs for export / relink / final / templates / settings, zh_CN translation
  (478 strings, `tools/check-i18n.py` in CI), self-test covering import → detect → export → relink → final
  (bit-exact round trip) → synthetic frame → project round trip.
- TIFF error messages now keep numbers in `detail` so the message itself is translatable.
- CI `app` job (Linux/macOS/Windows, Qt 6.8.3, self-test); `release.yml` builds a universal DMG and a Windows
  installer (Inno Setup) + portable ZIP, smoke-tests them, publishes with SHA256SUMS and release notes.
- Docs: README (en/zh), docs/INSTALL.md, docs/release-notes/v0.1.0.md, RFC-0010/0011 implementation notes.
### 2026-10-28 — real scans (HV-01, partly)
- Owner's 5 test scans (kept outside the repo): fixed single-print scans (whole scan = print), effective-DPI estimate (803→826), wider inner search with real-edge preference, scan-edge penalty softened to "check recommended". 136/136 tests pass; synthetic eval unchanged.
- CI: macOS/Windows build failures on phase-2-core; raw job logs are not reachable from the dev sandbox, so CI now emits build/test errors as annotations (`tools/ci-annotate.py`).
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
