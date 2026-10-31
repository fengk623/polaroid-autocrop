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
  Interaction checks click controls with real input events (injected through Qt GUI as Qt Test does, since
  0.7.0-alpha.1). A check whose precondition the scan lacks (e.g. a colour conversion on an untagged scan) is
  recorded as `"skipped": true` with the reason, never as passed; `tools/release/check-report.py` lists them.

## Implementation notes (v0.5.0)

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

## Boundary editing input (v0.7.0-alpha.3, U013, UX-03)

Status: Accepted, implemented in U013.
- **Shift tap.** `ShiftTap` (C++, QML element) watches the application's input for the main window and emits
  `tapped` only for a lone Shift: pressed with no other modifier and no mouse button held, released within 1 s,
  with no key, shortcut (`ShortcutOverride`/`Shortcut` — a shortcut consumes its key press before the window sees
  it), mouse press, wheel, touch, gesture, focus loss or window deactivation in between. It never consumes events.
  `Main.qml` acts on the tap only when Settings allow it (`shiftSwitchesBoundary`, default on), nothing is being
  typed (typing guard), no dialog is open and the focus is not in a popup or menu; `ScanCanvas.switchBoundary()`
  refuses during a drag. Tool buttons, `O`/`I` and Print ▸ Edit Outer / Edit Inner / Switch Outer ↔ Inner reach
  the same state.
- **Mode always visible.** `ScanCanvas.editTarget` ("outer" / "inner" / "") drives a capsule at the canvas's top
  centre with the boundary's outline colour and words (not colour alone), plus the Shift hint when enabled.
- **Drags.** The canvas keeps the start shape and previews `fk::editShape(start, {handle, index, delta,
  constrain})` (RFC-0002 §Editing); Shift pressed or released mid-drag updates the preview at once. Release
  commits the shape once (one undo step; unchanged shapes add none). Focus loss, mouse ungrab, Esc, a tool or
  selection change cancel the drag without committing. Radius handles sit on the rotated ellipse axes.
- **Arrows.** `ScanCanvas.handleActive` (property) gates the ← → and ⇧← ⇧→ shortcuts, so a selected handle gets
  the arrows (1 px, ⇧ 10 px) instead of stepping prints or scans.
- **Self-test.** `runShapeEditing` drives all of this with injected key and mouse events (Qt Test's
  `qt_handleKeyEvent` / `qt_handleMouseEvent`); focus loss is a `FocusOut` sent to the window. The macOS native
  menu bar is not reachable offscreen (that check is recorded as skipped there).

## Whole-scan rotation (v0.7.0-alpha.4, U014, UX-04)

Status: Accepted, implemented in U014.
- **Model.** `ScanRecord::rotation` (quarter turns clockwise) is a view attribute saved with the project
  (`scans[].view.rotation`). Boundaries, detection, hit tests, edits, exports and relink stay in scan file
  coordinates — one coordinate system, the turn applied once (no double rotation). The scan file is never rewritten.
- **Canvas.** `ScanCanvas` maps scan → display with `quarterTurnMatrix` (and back), then scale and offset; images
  (proxy pyramid, full-resolution patch, composite preview) are drawn in scan coordinates under that painter
  transform, overlays through `mapFromScan`. Fit, pan limits, zoom anchors and the navigator work in display
  coordinates; labels sit at the corner that is top-left on screen; arrow-key nudges move in screen directions.
  A turn re-fits the view; the current print and tool stay.
- **Thumbnails.** Scan thumbnails (scan list, navigator) are turned losslessly; print thumbnails keep showing each
  print upright (their own upright, RFC-0005 §Orientation).
- **Controls.** Print ▸ Rotate Whole Scan 90° Counterclockwise / Clockwise, the left rail's "Whole scan" row (with the
  current angle) and the scan list's context menu. The per-print command is renamed "Turn This Print Upright Left /
  Right" (export orientation of one print) so the two are not confused.
- **Undo.** A turn is one undo step for the whole scan. Steps of the current print and of its scan share one order
  (`seq`): undo takes the most recent of the two, redo the most recently undone.
- **Self-test** `runScanRotation` (UX-04) builds a scan with one print four times turned 0/90/180/270°, then clicks
  each print at the screen position computed independently of the canvas, before and after a turn.
