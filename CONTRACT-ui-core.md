# CONTRACT — Desktop UI ↔ core

Contract version: **1** · RFC-0011 · Applies to `app/` (Qt Quick) and any future front end.

## 1. Boundary
- The UI calls only `fk::` public headers (API-SPEC-core.md). No geometry, colour, or file-format logic in
  QML or app C++ beyond formatting values for display.
- Pixels cross the boundary only as `fk::ImageBuffer`/`Proxy` handed to the canvas item (C++), never through QML properties or JSON.
- The UI owns threads only through `fk::JobQueue`; core calls never run on the UI thread except O(1) getters.

## 2. State model
- `AppController` holds one `fk::Project` (source of truth) and publishes read-only list models.
- Every mutation is a command: `setOuter(itemId, Shape)`, `setInner(…)`, `setUpright`, `setReviewed`,
  `setExcluded`, `reorder`, `detectScan(scanId, params)`, `exportItems(ids, ExportSettings)`, `relinkFiles(paths)`,
  `composite(itemId, settings)`, `undo/redo(itemId)`. Commands are validated by core; failures surface
  `{code, message}` (Errc names, API-SPEC §2) to a non-modal notification, or a modal confirmation when the
  code is `needs_confirmation`/`needs_review`.
- Generations: each async result carries `(itemId, purpose, generation)`; the UI applies it only if generation
  equals the latest requested for that key.

## 3. Item status (filmstrip badges) — derived, never stored separately
`detection: none|auto|template|manual` · `confidence: high|medium|low` · `reviewed` · `excluded` ·
`exported (latest edit version)` · `relinked` · `sizeAnomaly` · `iccAnomaly` · `composited` · `finalExported`.

## 4. Canvas contract
- View transform: `scale` (screen px per scan px, device-independent), `offset`. 100 % = one scan pixel per
  physical device pixel. `mapFromScan()`/`mapToScan()` are the only conversion functions.
- Resolution state: `Preview` when scale ≤ 1/proxy.scale × 1.05, else `Full` once all visible tiles are
  loaded (`Full…` while loading). Badge text exactly `Preview` / `Full` / `Full…`.
- Overlays are drawn from `fk::Shape` values; colours: Outer `#3D8BFF` solid, Inner `#F2C744` solid with inward
  corner ticks (so Outer and Inner differ by shape, not only colour), manual `#FF8A3D` dashed, low confidence
  `#FF4D4D` dash-dot + warning icon, relinked `#38C172` solid + link icon.
- Boundary edits (UX-03): handles drag through `fk::editShape` only (preview = committed value); one complete
  drag = one `commitShape` = one undo step; an unchanged shape adds no step; an interrupted drag (focus lost,
  Esc, tool or selection change) commits nothing. The edited boundary is always shown (`editTarget`).

## 5. Errors shown to users
Messages come from core `Error.message` (English) mapped through the translation table `app/i18n` by `Errc`.
Raw `detail` goes to logs only.

## 6. Versioning
Breaking changes bump the contract version; additions do not. Each change: this file + API-SPEC + RFC in one commit; `tests/app/tst_contract.qml` pins model role names.
