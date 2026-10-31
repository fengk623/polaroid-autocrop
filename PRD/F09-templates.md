# F09 — Fixed jig templates

RFC: `rfc/0004-detection.md` (§ templates), `docs/project-format.md` · Tests: `tests/core/test_project.cpp` (`Template.SaveLoadRoundTrip`), `tests/core/test_detection.cpp` (`Detection.TemplateReproducesItems`)

## Scope
- Create from a representative scan after the user has confirmed every Outer and Inner.
- Stored as JSON (`fk.template/1`) in the user's template folder: positions relative to scan size,
  the DPI it was made at, film type per slot, expected count.
- Apply: slots are mapped to the new scan (DPI-independent), then each edge snaps to the real edge
  using the full-resolution refinement; a snap offset above the threshold lowers confidence.
- Multiple templates; templates never write to scans; a corrupt or mismatched template falls back to
  normal detection with a message. Unknown newer versions open read-only.

## Acceptance
- A1 A template created at 800 DPI applied to the same layout at 1600 DPI finds all prints within 2 px after snapping. (`Detection.TemplateReproducesItems` covers one DPI; the 800 → 1600 DPI case is not built)
- A2 Corrupt template JSON → error + fallback, no crash. (not built; `Project.DamagedFileFailsCleanly` covers the same reader for projects)
- A3 Version migration test from every previous schema version. (not built: there is one template schema so far, `fk.template/1`)
