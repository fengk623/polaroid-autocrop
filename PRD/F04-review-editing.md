# F04 — Review and manual boundary editing

RFC: `rfc/0002-geometry.md`, `rfc/0011-ui-architecture.md`, `CONTRACT-ui-core.md` · Tests: `tests/core/test_geometry.cpp`, `tests/app/tst_boundary_edit.qml`

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

## Acceptance
- A1 Every boundary edit round-trips through project save/load unchanged (to 1e-9 px). (`test_project`)
- A2 Undo/redo restores exact previous geometry. (`test_edit_history`)
- A3 Exporting an unreviewed low-confidence item via the API without the override flag fails with `Errc::needs_review`. (`test_export`)
- A4 Overlay geometry on the canvas matches the export geometry (same `core/geometry` call); verified by snapshot test with known quads. (`tst_canvas_overlay`)
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
