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

## Acceptance
- A1 Every boundary edit round-trips through project save/load unchanged (to 1e-9 px). (`test_project`)
- A2 Undo/redo restores exact previous geometry. (`test_edit_history`)
- A3 Exporting an unreviewed low-confidence item via the API without the override flag fails with `Errc::needs_review`. (`test_export`)
- A4 Overlay geometry on the canvas matches the export geometry (same `core/geometry` call); verified by snapshot test with known quads. (`tst_canvas_overlay`)
