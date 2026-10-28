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
