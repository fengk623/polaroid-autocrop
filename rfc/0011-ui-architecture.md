# RFC-0011 — UI architecture

Status: Accepted · Module: `app/` · PRD F04, F15 · Contract: `CONTRACT-ui-core.md` · Reference: `docs/ui-reference.md`

- Qt 6.8 Qt Quick, Controls Basic style with our own theme (`app/qml/Theme.qml` singleton); no Widgets.
- C++ ↔ QML: one `AppController` (QML singleton) owning the `fk::Project`, a `JobQueue`, list models
  (`ScanListModel`, `ItemListModel`, `JobListModel`) and controllers (`ExportController`, `RelinkController`,
  `CompositeController`, `SettingsController`). All mutations go through `AppController` methods that call
  core APIs; QML never computes geometry.
- Canvas: `ScanCanvasItem` (QQuickItem) draws a proxy texture plus full-resolution tiles (256 px, display-
  converted RGBA8) when zoom > 1/proxyScale; overlays drawn by `OverlayItem` (QQuickPaintedItem) from shapes
  returned by `AppController.overlayShapes(viewTransform)` = core geometry; handles are QML items positioned via
  `canvas.mapFromScan()`.
- Thumbnails: `QQuickAsyncImageProvider` `image://fk/…` backed by the disk cache.
- Threads: core jobs on `JobQueue` threads; results posted with `QMetaObject::invokeMethod(…, Qt::QueuedConnection)`
  tagged with generation numbers.
- Platform layer `platform/{macos,windows}`: display ICC, memory-pressure notifications, file reveal, menu flavour.
- Self-test mode `FrameKeeper --self-test <dir>`: offscreen QPA + software backend, loads a generated fixture,
  runs detect/export/composite through the UI controllers, captures window snapshots, writes a JSON report.

## Implementation notes (v0.1.0)

What shipped differs from the plan above in these ways:
- One `AppController` (QML singleton `FrameKeeper.Core/App`) holds the project and exposes models
  (`ScanListModel`, `ItemListModel`, `JobModel`) and invokables; export, relink and composite are methods on it
  rather than separate controllers.
- `ScanCanvas` is a `QQuickPaintedItem` that paints a proxy pyramid, a full-resolution detail patch of the
  visible region (worker thread, 150 ms debounce) and the overlays/handles itself; there is no separate
  `OverlayItem` and no 256 px tile cache.
- Thumbnails come from a synchronous `QQuickImageProvider` over in-memory proxies (`image://fk/scan|item/…`);
  there is no disk thumbnail cache yet (RFC-0014), so a reopened project re-reads each scan once.
- Display conversion runs once per proxy/detail (ADR-0003): to sRGB on macOS (sRGB-tagged windows), to the
  primary monitor's ICC profile on Windows (docs/color-pipeline.md). Per-monitor profiles are not tracked.
- Undo/redo is per print (up to 100 steps each, boundaries, upright, reviewed) and in memory only; it is not
  saved with the project.
- Translations: `app/i18n/<lang>.json` loaded by `JsonTranslator`; `tools/check-i18n.py` fails CI when a
  `qsTr`/`tr` string or core error message lacks a zh_CN translation.
