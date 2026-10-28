# F09 — Fixed jig templates

RFC: `rfc/0004-detection.md` (§ templates), `docs/project-format.md` · Tests: `tests/core/test_templates.cpp`

## Scope
- Create from a representative scan after the user has confirmed every Outer and Inner.
- Stored as JSON (`fk.template/1`) in the user's template folder: positions relative to scan size,
  the DPI it was made at, film type per slot, expected count.
- Apply: slots are mapped to the new scan (DPI-independent), then each edge snaps to the real edge
  using the full-resolution refinement; a snap offset above the threshold lowers confidence.
- Multiple templates; templates never write to scans; a corrupt or mismatched template falls back to
  normal detection with a message. Unknown newer versions open read-only.

## Acceptance
- A1 A template created at 800 DPI applied to the same layout at 1600 DPI finds all prints within 2 px after snapping. (`test_templates.CrossDpi`)
- A2 Corrupt template JSON → error + fallback, no crash. (`test_templates.Corrupt`)
- A3 Version migration test from every previous schema version. (`test_templates.Migration`)
