# F15 — Desktop UI and platform conventions

RFC: `rfc/0011-ui-architecture.md`, `docs/ui-reference.md`, `CONTRACT-ui-core.md` · Tests: `tests/app/*`, real-window captures (`docs/HUMAN-VERIFICATION.md`)

## Layout (learned from SpektraLab's macOS interface; see `docs/ui-reference.md`)
- Left rail: project & files, scan list, templates, detection mode, expected count, batch selection, jobs.
- Centre canvas: Scan / Full Print / Image Area / Composite; overlays; zoom, pan, 100 %, fit; navigator;
  split compare; Preview/Full badge; colour space, bit depth, DPI chips.
- Right rail (groups): Outer, Inner, Geometry, Safety margin, Export image area, Re-import, Composite, Mask,
  Colour, Final output, Metadata & privacy.
- Bottom filmstrip: scans and prints with status badges (detection, confidence, exported, relinked,
  size anomaly, ICC anomaly, composited, final exported). It spans the whole window; the rails end above it (UX-10).
- Each panel folds with a thin strip on the window edge outside it (UX-10); View menu items and shortcuts stay.

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
- A1 Automated snapshots (offscreen self-test, software renderer) are evidence for people to look at, never a pass by
  themselves (FK-009). The `tests/app/snapshots` suite this line used to name was never built (FK-013); the checks
  are A7.
- A2 Real-window captures on Windows (via the user's machine) and macOS (human verification list).
- A3 UI stays responsive (< 50 ms frame) while a 1600 DPI export runs. (`tests/app/tst_responsiveness`)
- A4 (UX-09) Value fields that opt in (the mask feather/inset width, U016): press and drag left/right adjusts the value
  (2 logical px per step, Shift ×10, Alt ×0.1) past a 4 px threshold, so a click is never a drag and never selects
  text; a double-click (or Enter) opens keyboard input; Enter or focus loss commits, Esc cancels, invalid characters
  are refused; arrow keys still step; bounds clamp; one drag = one undo step; live updates at most every 100 ms and
  at most one composite preview in flight per print; the last preview shows the last value. Same behaviour at 2×.
  Other fields keep plain keyboard input until a consistency review (UX-09). (self-test `runScrubFields`, at 1× and
  2× device pixel ratio)
- A5 (UX-10) The left rail, the right rail and the filmstrip each fold and unfold with an 8 px strip on the window edge
  outside the panel; the strip stays in place while folded, its chevron points where a click moves the panel, its
  hit area reaches 6 px over an empty neighbouring inset (never over a scroll bar), it takes keyboard focus (Space /
  Enter) and its tooltip says Hide/Show with the shortcut. The filmstrip spans the whole window and the rails end
  above it. Folding, unfolding and resizing the window keep the whole scan at fit when the canvas was at fit, and a
  click still lands on the print under the pointer. View ▸ Left Rail / Right Rail / Filmstrip / All Panels and
  Ctrl+Alt+1/2/3/0 (⌥⌘ on macOS) stay; Reset Layout restores the default widths and shows every panel; widths and
  folded state are stored. The top bar no longer has fold buttons. (self-test `runLayout` at 1100×700, 1440×900,
  1920×1080; whole run also at 2× and in Chinese)
- A6 (UX-06, U021 part 1) The view switch (Scan · Print · Image · Composite) sits between the overlays button (eye)
  and the zoom field; keys 1–4 and the View menu unchanged. At every window size from 1100×700 and every rail width
  the top bar keeps everything inside its column without overlap: it gives up the file buttons, then shows the view
  switch as a menu button, then drops − / +, always keeping eye → view → zoom; the canvas stays at least 480 px wide
  (the rails narrow). The right rail shows the current print's workflow steps (import, detect / review, export image
  area, relink, composite / final output: done, next, not done) and a click leads to each step. (self-test
  `runToolbar`; the UX-06 known issue in `runLayout` is a regular check) Part 2 (two workspaces, Library / Edit)
  follows the library (U018–U020).
- A7 (FK-009, U024) Every visible text — main window, Settings, About and Export dialogs; dark and light; 1100×700,
  1440×900, 1920×1080; rails shown and folded; the three right-rail tabs — has a glyph for every character in its
  font or its fallbacks (no boxes), stays inside its own box and the window, is not cut at the side by its panel, and
  labels, section titles, segment and button texts are never shortened (values such as paths and file names may
  be); the strips, top-bar buttons, view switch, zoom field and workflow steps are not covered by anything painted
  later. CI runs it in English (1×) with the self-test and as `--scenario text` in Chinese (1×, 2×) and English
  (1.5×) on Windows, macOS and Linux. Real windows (Direct3D, Metal) at 100/150/200 % and Retina stay human
  verification (HV-02, HV-03, HV-07). User messages carry no notes for programmers (the depth-reduction refusal
  had "set allowDepthReduction …").

