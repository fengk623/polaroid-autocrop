# ARCHITECTURE

This document describes what is implemented. Sections marked **[planned]** describe accepted RFC
designs not yet in code; they are updated in the same commit that implements them.

Implementation status (2026-10-28): specs only; no product code yet.

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
│  color (lcms2) · metadata · cache (disk LRU) · jobs (lanes, budgets) · diagnostics (logs, memory)
└──────────────────────────────────────────────────────────────────────────────────┘
        ▲ read-only                                         │ temp → rename
   original scans                                    exports, composites, sidecars
```

## 2. Module ownership
| Module | Owns | RFC |
| --- | --- | --- |
| core/image_io | TIFF/PNG/JPEG/DNG read & write, integrity | RFC-0003 |
| core/geometry | coordinates, shapes, matrices, export geometry | RFC-0002 |
| core/detection | proxy, CV primitives, outer/inner detection, templates apply | RFC-0004 |
| core/export | export pipeline, naming | RFC-0005 |
| core/relink | matching, signatures, checks | RFC-0006 |
| core/compositing | composite, synthetic frame | RFC-0007, RFC-0008 |
| core/project | project/sidecar/template model + JSON | RFC-0009 |
| core/color | profiles, transforms | RFC-0012 |
| core/metadata | tag read/write policy, XMP | RFC-0013 |
| core/cache | disk cache, byte-bounded LRU | RFC-0014 |
| core/jobs | cancel, queue, generations | RFC-0010 |
| core/diagnostics | logging, memory stats, bundles | RFC-0015 |
| app/ | UI, platform glue | RFC-0011 |

## 3. Threading **[planned]**
UI thread: QML + O(1) getters. `JobQueue` lanes: export (1), detect (2), thumb (2), io-light (2). Each job
opens its own `ImageSource`. Results posted back with generation tags; stale results dropped.

## 4. Memory **[planned]**
Accounted buffers (`BufferAccounting`) by category: proxy, strip/tile cache, band buffers, region buffers,
display tiles. Budgets: PERFORMANCE-BUDGET.md. Memory pressure shrinks caches only.

## 5. Cache **[planned]**
Disk: proxies + thumbnails, content-addressed, LRU, capped (ADR-0006). Memory: decoded strip/tile LRU per
source (128 MB default), display tile LRU (256 MB default).

## 6. Colour **[planned]**
`docs/color-pipeline.md`.

## 7. Preview vs export **[planned]**
Preview = proxy (+ Full tiles) displayed through the same `core/geometry` shapes and matrices used by export.
Composite preview runs `compositeItem` on a proxy-backed `ImageSource` adaptor.

## 8. Project storage **[planned]**
`docs/project-format.md`.

## 9. Platform adaptation **[planned]**
`platform/macos` (display = sRGB, memory pressure via dispatch source, reveal in Finder),
`platform/windows` (monitor ICC via GetICMProfileW, CreateMemoryResourceNotification, reveal in Explorer,
Per-Monitor v2 DPI manifest). Menus: native macOS menu bar; in-window menu bar on Windows.

## 10. Mobile boundary
`docs/mobile-portability.md`, ADR-0007.

## 11. Tests and release
TEST-PLAN.md, RELEASE-CHECKLIST.md, `.github/workflows/`.
