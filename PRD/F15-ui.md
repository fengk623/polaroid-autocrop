# F15 — Desktop UI and platform conventions

RFC: `rfc/0011-ui-architecture.md`, `docs/ui-reference.md`, `CONTRACT-ui-core.md` · Tests: `tests/app/*`, real-window captures (`docs/HUMAN-VERIFICATION.md`)

## Layout (learned from SpektraLab's macOS interface; see `docs/ui-reference.md`)
- Left rail: project & files, scan list, templates, detection mode, expected count, batch selection, jobs.
- Centre canvas: Scan / Full Print / Image Area / Composite; overlays; zoom, pan, 100 %, fit; navigator;
  split compare; Preview/Full badge; colour space, bit depth, DPI chips.
- Right rail (groups): Outer, Inner, Geometry, Safety margin, Export image area, Re-import, Composite, Mask,
  Colour, Final output, Metadata & privacy.
- Bottom filmstrip: scans and prints with status badges (detection, confidence, exported, relinked,
  size anomaly, ICC anomaly, composited, final exported).

## Rules
- Two resolution states only: Preview (proxy) and Full (tiles from the original at ≥ proxy scale).
- Never block the UI thread; debounce parameter changes; generation counters drop stale results.
- Dark theme default, light theme available; follows system scale and per-monitor DPI.
- Every visible control is implemented; unfinished features are absent.
- Keyboard reachable, visible focus, status not by colour alone.
- Languages: English and Simplified Chinese.
- macOS: native menu bar, ⌘ shortcuts, native dialogs, pinch zoom, full screen.
- Windows: in-window menu bar, Ctrl shortcuts, native dialogs, standard title bar, wheel zoom, Per-Monitor v2 DPI.

## Acceptance
- A1 Automated snapshots for dark/light, 1280×800 and 1920×1080, 100 % and 200 % scale, long names, Chinese, empty project, low confidence, batch running, relink error. (`tests/app/snapshots`)
- A2 Real-window captures on Windows (via the user's machine) and macOS (human verification list).
- A3 UI stays responsive (< 50 ms frame) while a 1600 DPI export runs. (`tests/app/tst_responsiveness`)
