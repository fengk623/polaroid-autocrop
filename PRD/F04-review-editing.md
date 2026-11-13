# F04 — Review and manual boundary editing

RFC: `rfc/0002-geometry.md`, `rfc/0011-ui-architecture.md`, `CONTRACT-ui-core.md` · Tests: `tests/core/test_geometry.cpp` (`Edit.*`), self-test `runShapeEditing`

## Scope
- Canvas modes: Scan, Full Print, Image Area, Composite.
- Overlays (semi-transparent, can be hidden): Outer blue, Inner yellow, manual edits orange, low confidence red,
  relinked green. Each state also has a distinct line style or icon (never colour alone).
- Edit Outer and Inner independently: drag corners, drag edges, rotate, numeric entry; ellipse: drag centre/radii.
- Undo/redo for every edit; edits are saved to the project; "reset to detected".
- Safety margin (outward expansion of Outer, px or mm).
- Low-confidence items must be reviewed (marked "reviewed") before they can be exported; batch export
  lists unreviewed low-confidence items and skips them unless the user confirms.
- Item order can be changed.
- UX-03 (v0.7.0-alpha.3): a lone tap of Shift switches the edited boundary (Outer ↔ Inner), on by default and
  switchable off in Settings; the canvas always states which boundary is edited. Holding Shift only constrains a
  drag: rectangle corners keep right angles, side ratio and rotation about the opposite corner; moves lock to an
  axis; ellipse radii scale together; circles always stay circles (rx = ry). Free corner editing stays the default.
  Shift+click, Shift+arrows, Shift+wheel, typing, dialogs, menus and focus loss never switch. The tool buttons and
  Print ▸ Edit Outer / Edit Inner / Switch Outer ↔ Inner do the same without modifier keys.

- UX-04 (v0.7.0-alpha.4): the whole imported scan can be turned 90° counterclockwise / clockwise (Print menu, left
  rail, scan list menu). Non-destructive: a view of the scan saved with the project; every print, its boundaries,
  the canvas, scan thumbnails, hit tests and edits follow; the scan file is never changed. Exports keep each print's
  own upright (never doubled) or, with uprights off, follow the turned view. The per-print command is named "Turn
  This Print Upright" to tell the two apart.

## Acceptance
- A1 Every boundary edit round-trips through project save/load unchanged (to 1e-9 px). (`test_project`)
- A2 Undo/redo restores exact previous geometry. (self-test `runShapeEditing`, `runScanRotation`: one drag = one undo step, undo restores the shape exactly)
- A3 Exporting an unreviewed low-confidence item via the API without the override flag fails with `Errc::needs_review`. (`test_export`)
- A4 Overlay geometry on the canvas matches the export geometry (same `core/src/geometry.cpp` call); the canvas maps scan coordinates with the core matrices (self-test `runShapeEditing`, `runLayout` clicks land on the print); a pixel snapshot test with known quads is not built.
- A5 (UX-03) A lone Shift tap switches Outer ↔ Inner and the canvas says which is edited; Shift+click, Shift+letter,
  Shift+arrows (with and without a selected handle), Shift+wheel, a hold over 1 s, typing, an open dialog, focus loss
  and Shift around a drag never switch. (self-test `runShapeEditing`, real key/mouse events)
- A6 (UX-03) Shift-drag of a corner of a rectangle and of a tilted rectangle gives an exact rectangle (corner angle
  error < 1e-9) with the same side ratio and rotation; the opposite corner does not move. (`Edit.ShiftCorner*`,
  self-test)
- A7 (UX-03) A circle stays rx = ry exactly under radius drags in any direction, with or without Shift, arrow nudges,
  undo/redo, transforms, save and reopen. (`Edit.CircleStaysACircle`, `Edit.TransformsKeepACircleExactlyRound`,
  self-test)
- A8 (UX-03) One complete drag = one undo step; an interrupted drag (focus lost) changes nothing; the export uses the
  edited geometry. (self-test)
- A9 (UX-03) The tool buttons and the menu switch the boundary without modifier keys. (self-test; macOS native menu:
  same function, not reachable offscreen)
- A10 (UX-04) A scan with four prints in four orientations, turned: all four follow (a click at each print's
  expected screen position selects it), the count stays 4, no boundary changes. (self-test `runScanRotation`)
- A11 (UX-04) Four turns restore the start (canvas pixel-identical); one undo restores the whole scan; undo/redo
  interleave print edits and turns in order. (self-test, `ScanRotation.FourQuarterTurnsAreTheIdentity`)
- A12 (UX-04) Saved and reopened, the turn is kept; older projects open unturned. (`ScanRotation.Saved…`, self-test)
- A13 (UX-04) Files exported before a turn relink after it and the final output equals the full print bit for bit;
  uprights are never doubled; with uprights off exports follow the view by a lossless quarter turn.
  (`ScanRotation.ExportTurns…`, self-test)
- A14 (UX-04) The scan file's SHA-256 is unchanged. (self-test)
