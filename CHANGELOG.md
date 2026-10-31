# Changelog

All notable changes. Versions follow `VERSION`; release notes with measurements and known limits live in
`docs/release-notes/`.

## 0.7.0-alpha.14 — U027: specifications name only what exists; outcomes counted apart (FK-013)
- `tools/check-claims.py` (CI docs job): tests, test programs, self-test sections, scenarios, files and CI jobs
  named in the PRDs, RFCs, test plan, acceptance matrix and traceability exist, and every name the API spec declares
  exists in the core; what is not built says so on the same line. First run: 89 claims without code — tests that
  never existed (`test_dng`, `test_roundtrip`, `test_export_*`, `test_templates`, `test_jobs`, `tests/app/*`, …), a
  C ABI, disk cache, job queue, capability profiles and a `core-mobile-lite` CI job that were never built, an
  architecture page saying the Qt app did not exist, seven API signatures that differed from the headers. All
  corrected or marked planned; the acceptance matrix now names real tests with honest status.
- `tests/core/test_api_contract.cpp`: the spec's enum values, constants and function signatures are checked at
  compile time, stable names at run time.
- Test reports: CTest outcomes passed / failed / skipped / not started / blocked (not started and unlisted skips
  fail the report); the app self-test prints passed / failed / skipped / known issues / not applicable and writes
  it to `report.json`. Notes: docs/release-notes/v0.7.0-alpha.14.md.

## 0.7.0-alpha.13 — U024: text and layout checks; right rail no longer wider than itself (FK-009)
- Self-test `runTextLayout` (also `--scenario text`): every visible text has glyphs (no boxes), stays inside its box,
  the window and its panel's sides; labels, titles and button texts are never shortened; key controls are not
  covered. Dark and light, three window sizes, rails shown and folded, the three right-rail tabs, Settings, About
  and Export dialogs; CI runs it in English (1×, 1.5×) and Chinese (1×, 2×) on all three platforms.
- Found and fixed: the Return tab was wider than the right rail (two buttons in one row), cutting texts and buttons
  at its right edge — the buttons now wrap and the final-output frame choice sits under its label; a field label
  in Settings was shortened — long field labels wrap; the 8-bit refusal ended with a note for programmers
  ("set allowDepthReduction …") — confirmation details go to the log, not to the message.
- The historical boxes in Windows offscreen self-test snapshots: Qt's offscreen platform on Windows found no fonts
  (it reads QT_QPA_FONTDIR or Qt's own lib/fonts) and drew every character as a box as wide as the font size; the
  self-test now points it at the system fonts, and the glyph check recognises that box engine. CI Linux installs
  a CJK font for the Chinese checks. Real windows (Direct3D, Metal, 150/200 %, Retina) stay human verification.
  Notes: docs/release-notes/v0.7.0-alpha.13.md.
- Found on the owner's scan polaroid_92: long values in the Boundaries tab (film type, confidence) kept their own
  width and widened the right rail by 7 px at 1100×700 — value texts now fill the row and wrap; the overflow report
  names the row that widens a rail. Windows offscreen self-test: Chinese font collections (.ttc) are loaded and the
  fallback families set (the offscreen font database reads neither .ttc files nor falls back).

## 0.7.0-alpha.12 — U021 part 1: view switch between the eye and the zoom; the top bar fits narrow windows; workflow steps (UX-06)
- The Scan · Print · Image · Composite switch sits between the overlays button and the zoom field (keys 1–4 and the
  View menu unchanged). In a narrow centre column the top bar gives up the file buttons, then shows the view switch
  as a menu button, then drops − / +, always keeping eye → view → zoom and staying inside its column; the rails
  narrow (not below their minimums, stored widths kept) so the canvas stays at least 480 px wide. Fixes the
  overflow at 1100×700 found in U022 (known issue → regular check).
- The right rail shows the current print's five workflow steps (import, detect / review, export image area, relink,
  composite / final output): done, next, not done; a click goes to the step. Self-test `runToolbar`.
- Part 2 (the Library / Edit workspaces) follows the library units. Notes: docs/release-notes/v0.7.0-alpha.12.md.

## 0.7.0-alpha.11 — U022: edge strips fold the panels; the filmstrip spans the window (UX-10)
- The left rail, the right rail and the filmstrip each fold and unfold with an 8 px strip on the window edge outside
  them; the strip stays put when folded, its chevron shows where the panel goes, it takes keyboard focus and its hit
  area reaches 6 px over an empty neighbouring inset (never over a scroll bar). The filmstrip now spans the whole
  window under both rails. The ◧ / ◨ buttons left the top bar; View menu items and Ctrl+Alt+1/2/3/0 (⌥⌘ on macOS)
  stay; Reset Layout also shows every panel. Self-test `runLayout` (14 checks). Notes:
  docs/release-notes/v0.7.0-alpha.11.md.

## 0.7.0-alpha.10 — U026: Linear DNG decoded independently; DNG given back refused with guidance (FK-012)
- CI (Core, linux) decodes our Linear DNG export with LibRaw (rawpy, test-only, in a venv): its camera-space data
  equals the linearised 16-bit TIFF export of the same image area within 1 LSB (measured ≈ 0.3 LSB, gain
  1.0000–1.0001) and the camera-to-sRGB matrix it derives from our ColorMatrix1 is the expected one (2.5e-6);
  LibRaw's own sRGB rendering is reported only (it treats the D50 matrix as D65). `tools/dng-interop.py`, results as
  CI notices. PRD F05 A5 named a `test_dng` that never existed — now points at this check.
- Relinking or dropping a Linear DNG (ours or any LinearRaw file) is refused with "export a 16-bit TIFF from the
  editor and use that" instead of a generic unsupported-format error, and is never imported as a scan. Keeping DNG
  relink unsupported for v1.0 is a proposal for the owner. Notes: docs/release-notes/v0.7.0-alpha.10.md.

## 0.7.0-alpha.9 — U023: simpler app icon (UX-11)
- The sun is gone from the icon; the print, its frame and the corner brackets stay (same colours, no rebranding).
  Every ICO / ICNS size is drawn on its own with strokes kept visible at 16–32 px, and the ICNS now has the 1×
  16 / 32 / 64 px entries too. `tools/check-icons.py` checks the shipped icons in CI. Notes:
  docs/release-notes/v0.7.0-alpha.9.md.

## 0.7.0-alpha.8 — U025: display colour follows the window's monitor; MinIsWhite full resolution fixed (FK-011)
- Windows: the canvas converts to the ICC profile of the monitor the window is on, read again when the window
  moves to another screen, screens are added or removed, or the monitor's profile changes (5 s poll); the proxy,
  the 100 % patch, the composite preview and the thumbnails follow, and nothing converted for the previous profile
  is shown. MinIsWhite scans are no longer shown as negatives at full resolution. The About dialog names the
  display profile. Outputs never depend on the display. Notes: docs/release-notes/v0.7.0-alpha.8.md.

## 0.7.0-alpha.7 — U017: one memory budget for the caches (FK-005)
- Scan proxies, decoded strips of the full-resolution view and composite previews share one byte budget sized
  by the device's RAM (8 GB → 512 MB, 16 GB → 1 GB); what is not on screen is given back least recently used
  first and rebuilt from the files when shown again; thumbnails and canvas images are counted. Jobs reserve
  their estimated working memory before they start; OS memory pressure drops every rebuildable cache (output
  unchanged). Before, every scan visited kept its decoded strips (up to 96 MB each) and every composite preview
  stayed for the whole session. Notes: docs/release-notes/v0.7.0-alpha.7.md.

## 0.7.0-alpha.6 — U016: drag to adjust, double-click to type (UX-09)
- The mask width can be adjusted by dragging left/right on the value (past a 4 px threshold; Shift ×10, Alt ×0.1)
  and typed after a double-click (Enter / focus loss commits, Esc cancels, invalid characters refused); one drag is
  one undo step; at most one composite preview per print runs while dragging. Also fixed: on 2× displays the canvas
  showed the preview proxy at 100 %. Notes: docs/release-notes/v0.7.0-alpha.6.md.

## 0.7.0-alpha.5 — U015: natural-blend mask by default, saved per print (UX-08)
- New prints composite with a natural blend: a 0.05 mm feather inside the image area (converted with the scan's
  DPI; px without one), chosen with the new `fkcli maskeval` on real scans (worst-case seam at 50 %: 6.61 → 3.64
  ΔE00). Preserve edge 0.02 mm and hard edge 0 px (archival exact) remain. The mask is saved per print; applying it
  to other prints is explicit; older projects keep their setting. Unchanged graded files stay bit-exact; preview and
  final use the same mask; strong edge changes are flagged. Notes: docs/release-notes/v0.7.0-alpha.5.md.

## 0.7.0-alpha.4 — U014: rotate the whole scan (UX-04)
- Print ▸ Rotate Whole Scan 90° Counterclockwise / Clockwise (also in the left rail and the scan list menu): every
  print, its boundaries, the canvas, scan thumbnails, hits and edits follow; the scan file is never rewritten (a
  view saved with the project). One undo restores the whole scan; four turns are the start. Exports keep each
  print's own upright (never doubled) or, with uprights off, follow the turned view losslessly; files exported
  before a turn relink bit-exact after it. The per-print command is now "Turn This Print Upright". Notes:
  docs/release-notes/v0.7.0-alpha.4.md.

## 0.7.0-alpha.3 — U013: Shift switches Outer/Inner, constrained rectangle and circle editing (UX-03)
- A lone Shift tap switches the edited boundary (Outer ↔ Inner); the canvas always says which one is edited.
  Holding Shift constrains a drag instead: rectangle corners keep right angles, side ratio and rotation about the
  opposite corner; moves lock to an axis; circles always stay circles. Shift+click / arrows / wheel / letters,
  typing, dialogs, menus and focus loss never switch; tool buttons, O / I and the Print menu do the same. One drag
  = one undo step; an interrupted drag changes nothing. Also fixed: with a handle selected, Shift+← / → stepped
  scans instead of nudging; an arrow nudge on a circle's radius turned it into an ellipse. Notes:
  docs/release-notes/v0.7.0-alpha.3.md.

## 0.7.0-alpha.2 — U012: outer edges from the paper side, exact straightening, real-scan GT tools (UX-02)
- The outer edge is found from the paper side: on a white lid the shadow band is no longer included (owner's
  scan: 0.9–1.9 mm → 0.1–0.5 mm), a single-print scan's side that leaves the scan becomes the scan border instead of
  cutting paper; "straightened" maps a quad image area exactly onto the output (no frame slivers from scanner
  shear). New `fkcli realeval` and `tools/gt/draft-gt.py`. Notes: docs/release-notes/v0.7.0-alpha.2.md.

## 0.7.0-alpha.1 — U011: automatic print count (UX-01)
- Scans are counted automatically by default (no count warning made from a default number); each scan can switch to
  a manual expected count, kept while automatic; the count warning follows the setting at once, without detecting
  again. Automatic mode still reports low confidence, possibly missed prints (`possible_missed`), blank scans
  (`no_prints_found`) and the 12-object limit. Also: a new project clears the canvas; stored settings keep their
  types. Notes: docs/release-notes/v0.7.0-alpha.1.md.

## 0.6.0-alpha.10 — U010: export output plan, labelled straightening, round image areas (UX-02 export, FK-010)
- Every export prompt is worded from the core's plan of the real output (no "bit-identical" for 16 → 8 bits);
  16 → 8 bits needs an explicit yes; image areas are straightened by default and say so; round image areas are
  transparent (PNG/TIFF) or filled white (JPEG). Notes: docs/release-notes/v0.6.0-alpha.10.md.

## 0.6.0-alpha.9 — U009: privacy presets on every channel (FK-004)
- "Remove location and identity" no longer lets the source XMP creator through; one filter feeds every writer
  (TIFF/DNG tags, XMP in TIFF/PNG/JPEG/DNG); the Export tab states exactly which fields an export carries.
  Notes: docs/release-notes/v0.6.0-alpha.9.md.

## 0.6.0-alpha.8 — U008: data protection (FK-001, FK-008)
- Every way of replacing the open project asks Save / Don't save / Cancel (also for dropped project files and with
  running tasks); a failed save keeps everything. Recovery snapshots of unsaved work (never the project file),
  restore of a damaged project from its backup (the damaged file is kept), a damaged file never replaces a good
  backup, output parameters saved with the project. Manual save stays (RFC-0017).
  Notes: docs/release-notes/v0.6.0-alpha.8.md.

## 0.6.0-alpha.7 — U007: source identity (FK-007)
- Scans are hashed whole at import and verified (streamed, off the UI thread) before every preview, detection and
  output; a replaced or edited scan is refused with the reason and can be relocated by content; outputs made while a
  file changed are discarded; a graded file saved over is used only if size, picture and profile are unchanged.
  Notes: docs/release-notes/v0.6.0-alpha.7.md.

## 0.6.0-alpha.6 — U006: graded files without a usable colour profile (FK-003)
- A missing or unreadable profile is never guessed: the export's profile (identified by hash), the scan's, sRGB,
  Adobe RGB or a profile file is chosen and confirmed when relinking, stored with the project (`iccProfiles`) and
  converted once when compositing. Notes: docs/release-notes/v0.6.0-alpha.6.md.

## 0.6.0-alpha.5 — U005: confirmed relink transform (FK-002)
- The turn a graded file was given in the editor is limited by its size, decided by the picture or chosen by the
  user, confirmed by ticking, stored with the relink and used as stored by preview and output (fixes 180° files
  composited upside down). Notes: docs/release-notes/v0.6.0-alpha.5.md.

## 0.6.0-alpha.4 — U004: target-first relink, evidence model (UX-07)
- Relink weighs file hash, embedded IDs, file name and picture together; conflicts and ambiguities are shown, never
  resolved silently; "Relink to this print…" (target first); "Assign to …" re-checks a file against a chosen print.
- New alignment for prints without an export in this project (`origin: "manual_alignment"` record); `_n` editor
  suffix; window drop sends graded TIFFs to relink. Notes: docs/release-notes/v0.6.0-alpha.4.md.

## 0.6.0-alpha.3 — U003: export records in the project, portable sidecar optional (UX-07)
- docs/release-notes/v0.6.0-alpha.3.md

## 0.6.0-alpha.2 — U002: background task safety (FK-006)
- docs/release-notes/v0.6.0-alpha.2.md

## 0.6.0-alpha.1 — U001: traceable builds, reproductions, known-issue tests (UX-12, FK-013, FK-014)
- docs/release-notes/v0.6.0-alpha.1.md

## 0.5.0 — first public build (pre-release)

- Detection of instant prints in flatbed TIFF scans (Polaroid 600 / i-Type / SX-70, Polaroid round frame,
  Instax Mini / Square / Wide), with confidence classes and review flags.
- Boundary editing, templates, undo/redo, review queue.
- Image-area export (TIFF, PNG, JPEG, Linear DNG), relink of graded files, compositing into the real scanned
  frame, synthetic frames.
- Projects, colour management (ICC keep/convert), metadata privacy, English and Simplified Chinese UI.
- macOS universal DMG, Windows installer and portable ZIP (unsigned).
