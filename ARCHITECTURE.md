# ARCHITECTURE

This document describes what is implemented. Sections marked **[planned]** describe accepted RFC
designs not yet in code; they are updated in the same commit that implements them.

Implementation status (2026-10-31, U027 / FK-013 — the 2026-10-28 line said the Qt app was not built; it is):
- **Built:** core library `fkcore` with tests (image I/O, colour, metadata, geometry, detection, export,
  compositing, synthetic frame, relink, projects, in-memory cache budget, memory accounting); tools `fkcli` and
  `fkfixture`; the Qt Quick app `app/` (canvas, rails, filmstrip, edge strips, export/relink/final dialogs, job
  runner, display colour, memory pressure, translations, `--self-test` with scenarios); release packaging
  (`tools/release/`, `.github/workflows/release.yml`).
- **Not built (planned):** disk cache (ADR-0006), diagnostics bundles (RFC-0015), the C ABI and capability
  profiles (ADR-0007), the library storage (RFC-0018, waiting for the owner), a core job queue (jobs run in the
  app, §3).

## 1. End-to-end data flow
```
            ┌──────────── app/ (Qt Quick) ────────────┐     tools/cli (fkcli)
            │ AppController · models · ScanCanvasItem  │            │
            └───────────────┬──────────────────────────┘            │
                            │ fk:: public API (API-SPEC-core.md)     │
┌───────────────────────────▼────────────────────────────────────────▼─────────────┐
│ core (fkcore, C++20, no Qt)                                                       │
│  image_io: TiffSource ─► readRows (bands, increasing y) ─► proxy | edge bands | export bands
│  detection: proxy → mask → components → min-area rects → full-res edge refinement → items
│  geometry: shapes, matrices, export geometry, mask weights (single source of truth)
│  export: band streaming → (colour once) → (depth) → TIFF/PNG/JPEG/DNG sink → verify → sidecar
│  relink: XMP/sidecar/name/manual → content signature check
│  compositing: scan band + graded image via inverse map + mask → sink
│  project: JSON project/sidecar/template, atomic save, migrations
│  color (lcms2) · metadata · cache (in-memory budget ledger) · memory (accounting, system memory)
└──────────────────────────────────────────────────────────────────────────────────┘
        ▲ read-only                                         │ temp → rename
   original scans                                    exports, composites, sidecars
```

## 2. Module ownership
Files are under `core/src/` (implementation) and `core/include/fk/` (public headers, API-SPEC-core.md).

| Module | Files | Owns | RFC |
| --- | --- | --- | --- |
| image I/O | `tiff_source.cpp`, `tiff_sink.cpp`, `tiff_util.cpp`, `image_file.cpp`, `image.cpp` | TIFF/PNG/JPEG/DNG read & write, integrity | RFC-0003 |
| geometry | `geometry.cpp` | coordinates, shapes, matrices, export geometry, boundary edits (`editShape`, UX-03) | RFC-0002 |
| detection | `detection/` | proxy, CV primitives, outer/inner detection, templates apply | RFC-0004 |
| export | `export.cpp`, `output_stage.cpp`, `pixel_ops.cpp` | export pipeline, output plan, naming | RFC-0005 |
| relink | `relink.cpp`, `relink_assess.cpp` | matching, signatures, checks | RFC-0006 |
| compositing | `composite.cpp`, `graded_profile.cpp` | composite, synthetic frame, graded profiles | RFC-0007, RFC-0008 |
| project | `project.cpp` | project/sidecar/template model + JSON, export records | RFC-0009 |
| colour | `color.cpp` | profiles, transforms | RFC-0012 |
| metadata | `metadata.cpp` | tag read/write policy, XMP, privacy presets | RFC-0013 |
| cache | `cache.cpp` | in-memory budget ledger (a disk cache is planned, ADR-0006) | RFC-0014 |
| memory | `memory.cpp` | buffer accounting, system memory | RFC-0014, RFC-0015 |
| jobs | `app/src/Jobs.cpp` (not in core) | lanes, cancel, generations, memory reservations | RFC-0010 |
| app | `app/src/`, `app/qml/` | UI, platform glue, self-test | RFC-0011 |

## 3. Threading
UI thread: QML + O(1) getters. The app's `JobRunner` (`app/src/Jobs.cpp`) runs lanes `detect`, `exportJob` and
`io` with bounded concurrency; each job opens its own `ImageSource`; results come back on the UI thread with
generation tags and stale results are dropped (RFC-0010 §Task safety, self-test `runTaskSafety`). A core-level
`JobQueue` as first designed is not built (planned only if a second front end needs it).

## 4. Memory
Accounted buffers (`BufferAccounting`) by category: proxy, strip/tile cache, band buffers, region buffers,
display tiles. Budgets: PERFORMANCE-BUDGET.md.
**Implemented (U017, FK-005):** one byte budget for the app's in-memory caches (`fk::CacheLedger`, RFC-0014
§Memory budget): scan proxies, decoded strips of the full-resolution view and composite previews are dropped
least recently used first, never what is on screen; thumbnails and the canvas images are counted. Budget =
RAM / 16 (256 MB … 2 GB). Jobs reserve their estimated working memory from the moment they are queued
(`JobRunner`, per lane). Memory pressure (`app/src/MemoryPressure.cpp`: macOS dispatch source, Windows memory
resource notification, Linux available memory) drops every rebuildable cache, never output quality.

## 5. Cache
Disk **[planned]**: proxies + thumbnails, content-addressed, LRU, capped (ADR-0006). Memory: decoded strip/tile
LRU per source (128 MB default for jobs; the full-resolution view's source min(96 MB, budget / 4)), all app
caches under the memory budget of §4.

## 6. Colour
`docs/color-pipeline.md` (built: profile handling, conversions, display profile per monitor on Windows, U025).

## 7. Preview vs export
Preview = proxy (+ Full tiles) displayed through the same `core/src/geometry.cpp` shapes and matrices used by export.
Composite preview runs `compositeItem` on a proxy-backed `ImageSource` adaptor.

## 8. Project storage
`docs/project-format.md` (built: `fk.project/1`, atomic save, backups, recovery snapshots). Library storage:
RFC-0018, proposed, not built.

## 9. Platform adaptation
In `app/src/` (no separate `platform/` folder): `Display.cpp` (macOS: sRGB display assumption; Windows: monitor
ICC via `GetICMProfileW`), `MemoryPressure.cpp` (macOS dispatch source, Windows memory resource notification,
Linux available memory), reveal in Finder / Explorer in `AppController`, Per-Monitor v2 DPI on Windows (Qt 6
default). Menus: native macOS menu bar (`MacMenuBar.qml`); in-window menu bar on Windows (`AppMenuBar.qml`).

## 10. Mobile boundary
`docs/mobile-portability.md`, ADR-0007.

## 11. Tests and release
TEST-PLAN.md, RELEASE-CHECKLIST.md, `.github/workflows/`.
