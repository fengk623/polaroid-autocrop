# UI reference: SpektraLab and PhotoFrame patterns, and the FrameKeeper layout spec

Status: design reference for v1 (PRD F15, RFC-0011) · Date: 2026-10-28 · Author: automated agent session

Related reading: `PRD/F15-ui.md`, `PRD/F04-review-editing.md`, `PRD/F08-synthetic-frame.md`, `PRD/F10-batch.md`, `CONTRACT-ui-core.md` §3–§4 (normative for badge texts and overlay colours), `rfc/0011-ui-architecture.md`, `rfc/0010-jobs.md`, `docs/reference-audit.md` (provenance, licences, do-not-copy list).

## 1. How to read this

**What it is.** Section 2 lists every interface element and pattern recorded by the reference audits:

- SpektraLab's macOS app (`notes/spektralab-ui-api.md` §2);
- SpektraLab's Windows Qt Quick front end (§3);
- the do-not-copy items (§6);
- PhotoFrame/Encadré (`notes/photoframe-observations.md`).

Each element gets a decision and FrameKeeper's concrete design. Section 3 lists the platform differences. Section 4 is the **FrameKeeper layout spec**, the numbers that `app/qml/Theme.qml` implements.

**Decisions**

| Decision | Meaning |
|---|---|
| **Adopt** | Same idea, same or near-same value, re-implemented independently. |
| **Adapt for Windows** | The Windows-specific variant of a pattern. |
| **Adapt for our product** | Same idea, with values or content changed for FrameKeeper. |
| **Do not adopt** | Not used. The reason is given. |

**Reference abbreviations**

Paths below are relative to the SpektraLab repository at commit `fd891c6`.

| Abbreviation | Path |
|---|---|
| `Theme.swift`, `EditorWindow.swift`, `TopBar.swift`, `LeftPanel.swift`, `RightPanel.swift`, `Filmstrip.swift`, `CropMaskedThumbnail.swift`, `FrameFraming.swift`, `CompareOverlay.swift`, `ViewportState.swift`, `RenderScheduler.swift`, `PanelResize.swift`, `NavigatorSection.swift`, `SectionHeader.swift`, `CollapseTab.swift`, `MetalCanvasView.swift`, `Renderer.swift`, `Shaders.metal`, `Session.swift`, `EngineMessage.swift`, `SpektrafilmApp.swift` | Files under `modern_UI/Spektrafilm/Spektrafilm/` |
| `mUI-README` | `modern_UI/Spektrafilm/README.md` |
| `Main.qml`, `Theme.qml`, `Section.qml`, `ActionButton.qml` | Files under `windows_UI/qml/` |
| `FrameCanvas.cpp`, `PreviewController.cpp`, `EditStore.cpp`, `FrameOutput.cpp`, `main.cpp` | Files under `windows_UI/src/` |
| `PF` | PhotoFrame (`README.md`, `docs/index.html`, screenshots) at commit `02807a0` |

**Units.** px are device-independent pixels at 100 % system scale. On macOS, 1 px = 1 pt. On Windows they scale with per-monitor DPI.

**Normative values.** Values already fixed by `CONTRACT-ui-core.md` are quoted, not redefined: the badge texts and overlay colours (§4) and the item statuses (§3). Any change to them follows the interface-change rule (AGENTS.md §3.5).

**What we take from SpektraLab.** Its structure and interaction ideas. **What we do not take** (`docs/reference-audit.md` §8):

- its warm accent `#E1A95F`;
- its exact greys;
- all-bold SF Pro type;
- white-capsule list selection;
- accent-outlined action capsules;
- any icon, string or asset.

## 2. Element table

### 2.1 Window and regions

| Element | Reference (file:line) | Decision | Our design | Reason |
|---|---|---|---|---|
| Three flush regions: left rail, centre column (top bar, canvas, filmstrip), right rail | `Theme.swift:253-261`; `EditorWindow.swift:4-11`; `Main.qml:197-511` | Adopt | Left rail · centre column (top bar 40, canvas, filmstrip 148) · right rail. The filmstrip spans the centre column only. | Matches F15's region list. It keeps "what/which" on the left and "how" on the right. |
| Separation by hairlines, with no gutters, margins or corner radii between regions | `Theme.swift:53-62, 152-164, 329`; `EditorWindow.swift:13-15` | Adopt | 1 px hairlines in two tiers: `lineRegion` between regions, `lineSection` between sections inside a rail. No radii on region edges. | This is the "flush rails with 1 px hairlines" structure; it maximises canvas area. |
| Vertical dividers run the full window height, drawn as overlays that do not shift column widths | `Theme.swift:1189-1203`; `EditorWindow.swift:19-29, 141-145` | Adopt | Rail│centre dividers are overlay items spanning the full height. | Widths stay exact integers, which simplifies the layout test (audit row for rule 5). |
| Top bar closed below by a region hairline; filmstrip opened above by one | `EditorWindow.swift:176-201` | Adopt | Same, with `lineRegion`. | Consistency with the two-tier rule. |
| Left rail 254 wide, resizable 232–380 | `Theme.swift:269, 289-291` | Adopt | 254 default, resizable 232–380. | Fits the scan list with file names and the project tree. |
| Right rail 288 wide, resizable 280–364 | `Theme.swift:271, 308-310` | Adapt for our product | 300 default, resizable 280–420. | Our rail holds numeric boundary fields (x, y, angle, mm/px) and export summaries that need more width. |
| Filmstrip 132 tall, thumbnails 110 | `Theme.swift:276, 709` | Adapt for our product | 148 tall: padding 8 + thumbnail box 108 + gap 4 + label line 20 + padding 8. | Our cells carry a label (`P01`, scan name) and status badges (CONTRACT §3). |
| Top bar 38 tall, equal to the rail-header row, flush, radius 0 | `Theme.swift:33-36, 317, 342-349` | Adapt for our product | 40 tall, equal to the rail-header row (40), flush. | 40 = 28 px hit target + 2 × 6 padding at our 12–13 px type. |
| Rail header row 38; leading inset 10, trailing 9 | `Theme.swift:317-322` | Adapt for our product | 40; leading inset 12, trailing 10. | Matches our top bar and type ramp. |
| Minimum window 1100×700; default 1920×1080 | `Theme.swift:720`; `SpektrafilmApp.swift:51-55` | Adapt for our product | Minimum 1100×700. Default 1440×900, or 90 % of the screen's available area, whichever is smaller. | The F15 A1 snapshot sizes (1280×800, 1920×1080) fit, and laptops open without overflow. |
| Hidden title bar with traffic lights placed on the rail-header row | `SpektrafilmApp.swift:53-54`; `Theme.swift:395-408` | Do not adopt | Standard title bar on macOS; native title bar on Windows (row in §2.11). | Not in v1: Qt 6.8 has no portable full-size-content API. A native NSWindow tweak would need real-window verification first. Possible later; noted in docs/HUMAN-VERIFICATION.md when attempted. |
| Rail headers and the empty top-bar middle act as window drag handles | `TopBar.swift:105-107, 135-137`; `LeftPanel.swift:76-78`; `EditorWindow.swift:388-402` | Do not adopt | — | Not in v1: only needed with a hidden title bar. |
| Forced dark appearance | `EditorWindow.swift:80`; `SpektrafilmApp.swift:386` | Adapt for our product | Dark by default, light theme available, and "Follow system" as an option (F15). | F15 requires both themes. |
| Rail fold toggles permanently in the top bar (◧ ◨) | `TopBar.swift:3-9, 66, 129-130`; `LeftPanel.swift:82-123` | Adopt | ◧ at the top bar's leading edge, ◨ at its trailing edge, always visible. | A folded rail can always be brought back. |
| A folded rail leaves the layout and the canvas grows into its space | `mUI-README:74-79`; `EditorWindow.swift:127-164` | Adopt | Same. | Maximises canvas area. |
| Filmstrip folds with a hover-revealed pill on the canvas bottom edge (band 28, pill 14×41.5) | `CollapseTab.swift:1-30, 100-104`; `Theme.swift:706-707` | Adapt for our product | Folded from the View menu (⌥⌘3 / Ctrl+Alt+3) or by a tab on the canvas bottom edge: hover band 24, tab 44×14, also reachable by keyboard focus. | Keyboard reachability (F15). |
| Rail widths are the user's and persist; 6 px grip with a 1 px accent line on hover; drag measured from the start width; double-click resets | `PanelResize.swift:40-57, 83-131, 141-185` | Adopt | 6 px grip, 1 px `accent` line on hover. Widths persist under `ui/v1/rail/<left or right>/width` in FK_SETTINGS_DIR. Double-click resets the default. | A familiar, measured behaviour. Our key namespace differs. |
| Settings ▸ Reset All Layout | `PanelResize.swift:175-184`; `RightPanel.swift:17-18` | Adopt | View ▸ Reset Layout: default rail widths and every panel shown (U022). | Recovery from odd layouts. |

### 2.2 Rails, sections and controls

| Element | Reference (file:line) | Decision | Our design | Reason |
|---|---|---|---|---|
| "Which rail a control sits on says what moving it costs" | `ARCHITECTURE.md:272-273` (SpektraLab) | Adapt for our product | The left rail answers *what and which*; the right rail answers *how*, for the current item. CONTRACT-ui-core lists each control's cost class (display only / proxy recompute / full-resolution job / export job). | Our costs follow the workflow stage, not a render layer. |
| Left rail content: Navigator, Film, Print, Crop, Enlarger… ("Film and Print") | `LeftPanel.swift:1-41` | Adapt for our product | Title **Project**. Sections in order: Project & Files · Scans (list) · Templates · Detection (count prints automatically — default on, greys out the number and shows the prints found; expected count 1–12 when off, per scan; mode: auto/template) · Batch (selection, include/exclude, run) · Jobs. | F15 left-rail list. The SpektraLab sections are domain-specific. |
| Right rail: "Parameters" with a Pre-Dev/Post-Dev segmented switch under the title, persisted, with a mark when Post-Dev is not neutral | `RightPanel.swift:3-10, 32-33, 40-58, 82-117` | Adapt for our product | Title **Item**. A three-segment switch under it: **Boundaries** (Outer, Inner, Geometry, Safety margin) · **Export** (Export image area, Colour, Metadata & privacy) · **Return** (Re-import, Composite, Mask, Final output incl. Synthetic frame). The choice persists (`ui/v1/rightRail/tab`). A 6 px accent dot on a segment means that segment has per-item overrides. | Groups the eleven F15 groups by workflow stage, so the rail does not scroll endlessly. The mark-when-not-default idea is kept. |
| Sections are independent views; header = disclosure, title, optional note, optional checkbox, optional reset arrow (whose tooltip says what it resets), "•••" menu | `SectionHeader.swift:1-22, 46-60, 62-140`; `mUI-README:350-368` | Adopt | Header 28 tall: chevron 10 · title · note in `textTertiary` · optional reset icon (tooltip "Reset <section> to detected/default") · "•••" menu. Each section is its own QML component that takes only `AppController`. | A clean structure that is easy to test. |
| Draggable section dividers | `LeftPanel.swift:13-15`; `RightPanel.swift:17-18` | Adapt for our product | Only the Scans list in the left rail has a draggable splitter (min 120, default 240). Other sections collapse. | Simpler keyboard access; the scan list is the only long list. |
| Navigator in the left rail: cropped print in a 183-tall `#1A1A19` well; outside the visible part dimmed with black 45 %; visible part framed by a 1 pt text-colour line; click or drag re-centres; "Fit" pill | `NavigatorSection.swift:1-14, 31-51, 95-153` | Adapt for our product | A canvas overlay at the bottom right: 200×150 well (`surfaceWell`), inset 12, shown automatically when zoomed past fit (toggle: View ▸ Navigator). It shows the whole current canvas content (scan, print, image area or composite). The outside is dimmed black 50 %, and the visible rectangle has a 1 px `textPrimary` frame. Click or drag re-centres; a "Fit" button sits below. | F15 places the navigator in the canvas. Our rails are full of item controls. |
| One accent meaning only "selected / look here"; rail-fold toggles do not tint | `Theme.swift:175-178`; `TopBar.swift:44-49, 111-122, 140-164, 174-184`; `LeftPanel.swift:90-96` | Adapt for our product | One accent token, `accent`. Used for: active tool glyph, focus ring, checked checkbox fill, current filmstrip frame, resize-grip hover line, primary button fill. Never used on the canvas, so it cannot be confused with overlays. Fold toggles do not tint. | The rule is generic and valuable. The value is ours (§4.2). |
| Disabled control = whole row at opacity 0.38, no hit testing, reason in a tooltip, through one modifier | `Theme.swift:1205-1231`; `mUI-README:405` | Adopt | One `RowEnabled` attached property: `enabled` plus a `reason` string. It applies opacity 0.38 to the whole row, disables hits, and shows the reason as a tooltip and as the accessible description. | Honest disabled state with a reason (F15: status never by colour alone). |
| No literal colours or sizes in views (pinned by LayoutTests) | `Theme.swift:21-23`; `mUI-README:403` | Adopt | Every colour and size comes from `app/qml/Theme.qml`. A test fails on literals elsewhere (audit row for M12). | Prevents drift. |
| 28 pt pointer target independent of ink | `Theme.swift:653-677` | Adopt | Every compact control has a hit area of at least 28×28 px, whatever its drawn size. | Accessibility and trackpad precision. |
| Interface scale 100/115/130 % scaling the type tokens; label columns scale with the type | `Theme.swift:74-88, 487-493` | Adapt for our product | No in-app scale in v1: we follow the system scale and per-monitor DPI (F15). Label columns are computed from font metrics. | F15 scope. Platform scaling already covers it. |

### 2.3 Visual tokens

| Element | Reference (file:line) | Decision | Our design | Reason |
|---|---|---|---|---|
| `ground` `#5F5F5F` used as canvas surround *and* for wells, pills and tracks ("one grey, four jobs") | `Theme.swift:123-127` | Adapt for our product | Two tokens: `surfaceCanvas` `#5B5C5F` (dark) / `#8A8B8E` (light) for the surround only, and `controlFill` `#3B3C40` / `#FFFFFF` for controls. | A neutral mid-grey surround is right for judging prints. Separating control fill keeps light-theme fields legible. |
| `card` `#2D2D2C` for rails, top bar and filmstrip | `Theme.swift:128-134` | Adapt for our product | `surfacePanel` `#2A2B2E` (dark) / `#ECEDF0` (light). | Close in value, slightly cool to suit our teal accent, and not identical. |
| `rule` `#3A3B39` and `ruleRegion` `#1A1A19` (two hairline tiers) | `Theme.swift:152-164` | Adapt for our product | `lineSection` `#38393D` / `#D2D4D9`; `lineRegion` `#17181A` / `#B4B7BD`. | Keeps the two-tier structure with our own values. |
| Text inks `#F9F7F3` / `#DAD8D4` / `#898989` | `Theme.swift:166-171` | Adapt for our product | `textPrimary` `#F0F1F3` / `#1A1B1E`; `textSecondary` `#C6C8CD` / `#42454B`; `textTertiary` `#9B9EA4` / `#63666D`. | Our values meet WCAG AA on panels (§4.2). SpektraLab's `#898989` on `#2D2D2C` is about 4.2:1. |
| `plot` / `plotGrid`; histogram channel colours; latitude-plot colours | `Theme.swift:173-174, 231-245` | Do not adopt | — | We have no histogram or plots in v1 (not an editor; F15). |
| `accent` `#E1A95F` (code), with stale `#eca650` in a comment and README and `AccentColor` ≈ `#EE8A2B` | `Theme.swift:175-178`; SpektraLab `README.md:64-66`; `Theme.qml:9` | Do not adopt | `accent` `#35B5A8` (dark) / `#0B6F66` (light); `onAccent` `#0B1A19` / `#FFFFFF`. | Brand signature (audit §8.2). Teal is distinct from SpektraLab's amber and from every overlay colour. |
| Selected list row = white capsule with inverted text | `Theme.swift:179-190, 611-622` | Do not adopt | `selectionFill` `#2C4949` / `#CCDBDD` across the row, plus a 3 px `accent` bar at the leading edge. Text stays `textPrimary`; tertiary text becomes `textSecondary` on selected rows. | Signature element. The accent bar keeps selection visible without relying on hue alone. |
| `selectionFrame` `#FAF8F4` (filmstrip frame; compare line); `knob` `#FBF8F3` | `Theme.swift:223-228` | Adapt for our product | The compare line and slider knob use `textPrimary`. The current filmstrip cell uses an `accent` frame (§2.9). | Fewer tokens; accent marks "current". |
| `navigatorWell` `#1A1A19` | `Theme.swift:239-241` | Adapt for our product | `surfaceWell` `#1B1C1F` / `#DCDEE2` (navigator, empty thumbnails, colour swatches' backdrop). | One well token. |
| `disabledOpacity` 0.38 | `Theme.swift:199-205` | Adopt | 0.38. | A generic value that works in both themes. |
| Windows port tokens: ground, card, text, muted, accent, line, well, wellDark, selection, selectionText, hover `#41413e`, crop mask `#99000000`, thirds `#70ffffff`, error `#ffb39f` | `Theme.qml:1-19`; `Main.qml` literals | Adapt for our product | The same roles, mapped onto our tokens (§4.2). Hover = `textPrimary` at 6 %, pressed = 12 %. Outside-boundary dim = black 50 %. No thirds grid. `error` `#FF8E80` / `#B3261E`. | One token set for both platforms. Thirds are a composition aid we do not need. |

### 2.4 Type and metrics

| Element | Reference (file:line) | Decision | Our design | Reason |
|---|---|---|---|---|
| All SF Pro **Bold**, one weight; hierarchy from size, ink and position | `Theme.swift:961-1022, 1041-1108` | Do not adopt | System UI font in two weights, Regular 400 and Semibold 600 (§4.3). | Signature element, and long-form reading comfort. |
| Type ramp: rail title 12; section titles 12 / 10.5; body 10.5; small 9; meta 9; CINE 5.71 | `Theme.swift:961-1108` | Adapt for our product | Rail title 13 semibold · section title 12 semibold · body and labels 12 · values 12 (tabular) · captions and units 11 · badges and chips 11 semibold · dialog title 15 semibold. | SpektraLab's sizes come from a 2× drawing and are small. Ours stay readable on 100 % Windows displays. |
| Monospaced digits for changing values | `Theme.swift:1080-1082` | Adopt | Numeric fields and readouts use tabular figures (`font.features: { "tnum": 1 }`). | No reflow while values change. |
| Section header 26 tall, 10 under content | `Theme.swift:420-433` | Adapt for our product | 28 tall, 10 under content. | Matches our type and 28 px targets. |
| Disclosure triangle 12×7, insets 7/7/10 | `Theme.swift:434-443` | Adapt for our product | Chevron glyph 10×10, leading inset 8, gap to title 6. | Our own glyph set. |
| Rows: inset 16 from both rail edges, line box 22, spacing 5, sub-row 16 | `Theme.swift:452-480` | Adapt for our product | Inset 14; control height 24; row pitch 30 (24 + 6); sub-row 18. | Room for numeric fields with units. |
| Label column 84 / 96 × scale / 70 | `Theme.swift:486, 493, 524` | Adapt for our product | Computed per section as the widest label + 8, clamped to 84–132. | Works for English and Chinese without truncation (audit row for rule 217). |
| Slider value pill 43.73 with a 15.06 gap; pickers 107; field radius 4.25; menus as capsules | `Theme.swift:532-538, 572` | Adapt for our product | Numeric field 64×24 with a unit suffix; gap 10; combo boxes min 120; radius 4 for fields, menus and buttons (no capsules). | Our controls are mostly numeric entry. Capsules belong to the signature. |
| Stock list row 18 pitch; selection capsule 15.27 tall | `Theme.swift:610-622` | Adapt for our product | List rows 26 tall (the Windows port uses 27, `Main.qml:164-195`), with the selection style from §2.3. | Comfortable mouse and touchpad targets on Windows. |
| CINE pill; Process/Original action capsules 82.86×18.52, accent-outlined | `Theme.swift:628-649`; `TopBar.swift:140-164` | Do not adopt | Buttons 28 tall, radius 4. The single primary action in a dialog or section is filled `accent` with `onAccent` text; others use `controlFill`. | Signature element; not our domain. |
| Slider track 1.36, knob 6.13×5.07, checkbox 10 | `Theme.swift:653-677` | Adapt for our product | Track 2, knob 12, checkbox 14, each with a 28 px hit area. | Visibility on non-Retina Windows displays. |
| Glyph sizes: tool 16, rail header 18, reset 9, sidebar toggle 15 | `Theme.swift:679-697` | Adapt for our product | Tool 18, rail header 16, reset 12, fold toggles 16, filmstrip status 14. Our own icon set. | Our icons, sized for 100 % displays. |
| Top-bar paddings and gaps (32 / 33 / 23 / 40 / 46; v4 clusters 2 / 20 / 60) | `Theme.swift:356-379, 513-518` | Adapt for our product | Padding 6 (12 less the icon buttons' own inset); 4 between items in a cluster; 16 between clusters; the centre status area takes the remaining space. | Simpler; fits 1100 px windows with the narrow-column rules of §4.1 (U021 — before them the bar overflowed at 1100 px with both rails). |
| Zoom pill: ground-coloured capsule, minimum 41.29×20.69, "N %" only | `Theme.swift:712-718`; `TopBar.swift:204-238` | Adapt for our product | A zoom field 56×24 (`controlFill`, radius 4) showing "N %" with tabular digits. Clicking opens a menu: Fit, 25, 50, 100, 200, 400, 800 %. | Same idea in our style. |

### 2.5 Interaction patterns

| Element | Reference (file:line) | Decision | Our design | Reason |
|---|---|---|---|---|
| Rule: every surface that shows the frame shows the canvas's frame | `ARCHITECTURE.md:389-403` (SpektraLab); `AGENTS.md:978` | Adopt | Canvas overlays, navigator, filmstrip and grid thumbnails, export preview and exports all use core geometry (RFC-0002; AGENTS.md §4; `Geometry.*` and the self-test clicks on prints; a dedicated consistency test is not built). | The single most valuable rule for this product (F04 A4). |
| Crop-masked thumbnails: whole photo, outside the crop under black 82 %; the navigator shows the cropped frame only | `CropMaskedThumbnail.swift:1-20` | Adapt for our product | Scan cells show the whole scan with each print's Outer drawn in overlay colours. Print cells show the Full Print, with the area outside Outer under black 82 %. Image-area cells (F10 grid, Image Area mode) show the inner region only. | Users must see the real border (the product's promise) while browsing. |
| Batch export locks the editor (look-only) | `EditorWindow.swift:66-69`; `ARCHITECTURE.md:414-417` | Adapt for our product | Only items in a running job are locked: their boundary handles are disabled with the reason "Exporting…". Other items stay editable. A job indicator sits in the top bar. | Batches over many scans are long. Jobs snapshot item state (RFC-0010; audit row for rule 90). |
| Drop files anywhere; one handler | `EditorWindow.swift:81-91` | Adopt | A window-wide DropArea feeds the single import command: TIFFs import as scans; graded files offer Relink (F06). | One door (audit row for rule 155). |
| Slider edits coalesced: sent vs wanted, debounce 40/220 ms, generation counter | `RenderScheduler.swift:1-11, 93-143` | Adapt for our product | Drags update the overlay at once from proxy geometry. Recomputation (full-resolution refinement, composite preview) waits for a 150 ms idle debounce (RFC-0010), and results carry generations (CONTRACT §2). | Our edits are geometric; the debounce value comes from RFC-0010. |
| Undo ⌘Z only on the Mac, no redo | `SpektrafilmApp.swift:127-129`; `desktop/PARITY.md:18` | Do not adopt | Undo and redo on both platforms (§4.6), per item, with 100 entries persisted (RFC-0009). | F04 requires undo/redo. |
| Gesture = one undo step; a drag back to the start is not a step | `PreviewController.cpp:342-357, 395-440` | Adopt | One drag = one step, and a no-op is not recorded. | Predictable history. |
| Status line shows elapsed seconds instead of a progress bar, because the engine cannot report progress | `TopBar.swift:166-172`; `mUI-README:433-435` | Adapt for our product | Our jobs report progress (rows processed, throttled to ≥ 50 ms; RFC-0010), so the top bar shows a determinate bar + "n / N" + elapsed time, with an estimate after 10 %. Only operations that cannot measure progress show elapsed time alone. | Honest status: a bar only where it can be measured. |
| A refusal is a visible event, not a log line; a blocking centred error card with the reason and an action | `EngineMessage.swift:51-70`; `EditorWindow.swift:287-311` | Adapt for our product | Command failures are non-modal notifications, and `needs_confirmation`/`needs_review` open a modal confirmation (CONTRACT §2). A scan that cannot be read shows a centred card with the reason and the actions Relocate… and Remove from project. | Honest failure without a blank canvas. |
| Single-key shortcuts must not steal typing (TypingKeyGuard) | `AGENTS.md:945-962`; `SpektrafilmApp.swift:376-381` | Adopt | Single-key shortcuts are disabled while any text or number field has `activeFocus`; tested in `tests/app/tst_typing_key_guard.qml` (planned). | A known trap on both platforms. |
| Space held = show original decode; Space wins over the split | `MetalCanvasView.swift:689-723`; `Renderer.swift:199-224` | Adapt for our product | **Space held = temporary Hand (pan)**, the Photoshop convention. **`\` toggles Original ↔ Composite** in Composite mode, and wins over the split. | Our users grade in Photoshop or Lightroom, where Space pans and `\` toggles before/after. |

### 2.6 Canvas: zoom, pan and sampling

| Element | Reference (file:line) | Decision | Our design | Reason |
|---|---|---|---|---|
| Viewport: fit and centre; no panning below fit; above fit, an image edge never comes inside the viewport ("one clamp") | `ViewportState.swift:1-4, 124-132`; `FrameCanvas.cpp:28-42` | Adapt for our product | Fit and centre; no panning below fit. Above fit, the image may overscroll by at most 48 px past any edge. | Boundary handles on the scan edge must stay grabbable. |
| 100 % = one image pixel per device pixel; origin snapped to the pixel grid at 100 % | `ViewportState.swift:17-30`; `FrameCanvas.cpp:19-24, 33-35` | Adopt | 100 % = one scan pixel per physical device pixel (CONTRACT §4), using the screen's device pixel ratio; at 100 % the origin snaps to device pixels. | Required to judge the paper edge. |
| Zoom range 5–800 %, steps 5…800 %, pill menu Fit/25/50/100/200/400 | `ViewportState.swift:20, 139-146`; `TopBar.swift:204-210` | Adapt for our product | Range: min(fit, 2 %) to 800 %. Steps and menu in §4.5. | A 1600 DPI A4 scan fits at about 4–5 %, so steps below 5 % are needed. |
| Gestures: two-finger scroll pans; pinch, ⌘-scroll or ⌥-scroll zoom about the cursor; double-click toggles fit ↔ 100 %; zoom and pan locked in the crop tool | `MetalCanvasView.swift:3-9, 254-304`; `FrameCanvas.cpp:66-72` | Adapt for our product | macOS: two-finger pan, pinch or ⌘-scroll to zoom about the cursor. Windows: wheel zooms about the cursor (F15); Shift+wheel pans horizontally; a precision touchpad pans with two fingers. Double-click on empty canvas toggles fit ↔ 100 % about the click. Zoom and pan stay **unlocked** in edit tools. | Edge editing needs zoom and pan while editing. |
| Windows Qt: Fit or physical 100 % only; pan by drag at 100 % | `FrameCanvas.cpp:20-23, 66-72` | Do not adopt | The full zoom range on both platforms. | Feature parity (F15). |
| Sampling: nearest-neighbour at ≥ 1, linear below 1 | `Shaders.metal:514-516`; `FrameCanvas.cpp:56` | Adopt | Nearest at ≥ 100 %, linear below (the proxy is already box-filtered; ADR-0014). | Pixels stay pixels when inspecting edges. |
| Redraw on demand, coalesced (`needsDisplay` once shipped a blank canvas) | `MetalCanvasView.swift:180-204`; `mUI-README:330` | Adapt for our product | `ScanCanvasItem` calls `update()`, and the scene graph coalesces per frame. A real-window capture is the proof (AGENTS.md T2). | The Qt equivalent of the same trap. |
| Drawable size rounded up to avoid a black sliver on the right and bottom edges | `MetalCanvasView.swift:145-166` | Adopt | Tile coverage of the viewport is rounded up, and a test checks the right and bottom edges at fractional scales. | Edge pixels matter here. |
| Canvas colour: layer tagged with the working space; the OS converts; the surround re-encoded | `MetalCanvasView.swift:749-777` | Adapt for our product | Image tiles are converted on the CPU, once, to the display profile: sRGB on macOS, the monitor ICC on Windows (ADR-0003). The surround and overlays are sRGB tokens that never pass through the image transform. | Our own display pipeline. |
| Canvas item draws one texture, re-created when the image revision changes | `FrameCanvas.cpp:8-11, 47-58` | Adapt for our product | `ScanCanvasItem`: a proxy texture plus 256 px full-resolution tiles (display-converted RGBA8) when zoom > 1/proxyScale. Uploads are gated by tile revision (RFC-0011). | Scans exceed the GPU's maximum texture size, so we tile. |

### 2.7 Preview/full-resolution state and badges

| Element | Reference (file:line) | Decision | Our design | Reason |
|---|---|---|---|---|
| Two states, not a ladder; native render 400 ms after edits; zoom selects neither | `ARCHITECTURE.md:323-334` (SpektraLab) | Adapt for our product | `Preview` while scale ≤ 1/proxyScale × 1.05; otherwise `Full…` while the visible tiles load, then `Full` (CONTRACT §4). Zoom selects the state, and tiles are requested after a 150 ms idle. | We show unmodified scan pixels, so no re-render is needed after edits (audit row for rule 137). |
| Badge stack top right: `original · decode`, `full resolution…`, `full`, `preview`, refusals, `low memory headroom` | `Session.swift:518-544` | Adapt for our product | The stack (§4.7) holds the resolution badge (`Preview`, `Full…` or `Full`, exactly as in CONTRACT §4), `Original` while showing the original in Composite mode, and refusal badges ("Not enough memory", "Cannot read scan"). | Honest state. The strings are ours. |
| Badge style: plain caption text with a 60 % black shadow, no capsule (older captures show capsules) | `EditorWindow.swift:321-327`; `screenshots/natural_halation.png` | Adapt for our product | Capsules: `surfacePanel` at 85 %, height 20, horizontal padding 8, radius 10, text 11 semibold `textPrimary` (contrast ≥ 7.7:1 over white, §4.7). | Our prints often have white frames, where shadowed text is hard to read. |
| Badge geometry: inset 8, spacing 4, line height 18; histogram 200×56 reserved | `CompareOverlay.swift:122-140` | Adapt for our product | Inset 10, spacing 4, capsule height 20. No histogram. | No histogram in v1. |
| On first open the decode appears at once with `preview`, cleared when the print lands | `mUI-README:160-178, 211-212` | Adopt | The cached thumbnail or proxy appears at once with `Preview`. Nothing is ever blank while loading. | The browsing surface appears at once (audit row for rule 199). |
| The log line and the badge report the same fact | `AGENTS.md:312-320` | Adopt | One state change emits both, and a test asserts agreement. | Prevents a lying badge. |
| Windows Qt: badge always "FULL" (or "cropping the original") in a box, radius 2, inset 26 | `Main.qml:402-406` | Do not adopt | Same badges as macOS. | That badge never reflects state. |

### 2.8 Compare (Composite mode)

| Element | Reference (file:line) | Decision | Our design | Reason |
|---|---|---|---|---|
| Split view: Before left, After right, anchored to the output picture (not the viewport); both halves through the same geometry | `CompareOverlay.swift:1-12`; `Shaders.metal:498-517` | Adopt | In Composite mode: left of the line is the **Original** (scan pixels), right is the **Composite** (graded image inside the real frame). The split is anchored to the print, and both halves use the same geometry (F07 preview). | Exactly what F07 needs: judge the graded image inside the untouched frame. |
| Furniture: 1 pt line with shadow; 30 pt grip disc, card at 55 %, ◂▸; Before/After capsules inset 34; After moves below the badges on collision | `CompareOverlay.swift:20, 36-58, 67-76, 83-105` | Adapt for our product | A 1 px `textPrimary` line over a 1 px black 60 % halo. A 28 px grip circle (`surfacePanel` at 70 %, 1 px `lineSection` stroke) with our own ‹ › glyph. "Original" and "Composite" capsules in the top corners, inset 12. The right-hand label moves below the badge stack on collision. | Same function; our own glyph (the BeforeAfterIcon is on the do-not-copy list). |
| Drag anywhere on the line; position 0…1, default 0.5, clamped | `CompareOverlay.swift:107-117`; `Session.swift:593-607` | Adopt | Same. The position persists per session, not per item. | Good ergonomics. |
| Compare unavailable until the decode exists; entering compare leaves the crop tool | `Session.swift:596, 608-614` | Adopt | Compare (`Y`) is enabled only in Composite mode when the item has a relinked graded file; otherwise it is disabled with the reason. Entering compare leaves the edit tools. | Honest availability. |
| Windows Qt: no split comparison | `windows_UI/README.md:161-163` | Do not adopt | Compare on both platforms. | Parity. |

### 2.9 Filmstrip and grid

| Element | Reference (file:line) | Decision | Our design | Reason |
|---|---|---|---|---|
| One cell per frame with ‹ › chevrons; click selects; ⌘-click adds to the picked set; drag reorders; context menu Reveal, Reset | `Filmstrip.swift:22-104` | Adapt for our product | Two levels: scan cells, each followed by its print cells (`P01`, `P02`, …). Click selects. ⌘/Ctrl-click and ⇧-click multi-select. Dragging a print reorders it within its scan (F04). Context menu: Reveal Scan in Finder / Show in Explorer, Reset to Detected, Mark Reviewed, Exclude/Include. Chevrons scroll by one page. | Scans contain several prints. Items are reorderable. |
| Two selection marks from one enum: open frame = white 1.5 pt frame; picked = 1 pt frame at 50 %; no other per-cell pips | `FrameFraming.swift:16-45`; `Filmstrip.swift:6-12` | Adapt for our product | The two marks come from one enum. The **current** item gets a 2 px `accent` frame; a **selected** item gets a 1 px `textPrimary` frame at 60 % plus a check glyph in the top right. Per-cell **status badges** *are* shown (§4.9). | CONTRACT §3 requires status badges (detection, confidence, exported, relinked…). |
| Windows Qt: strip 160 with a count row (Select All, Clear Selection, Sync Settings); 132-wide cells; active = 2 px accent border; per-cell checkbox; file name | `Theme.qml:18`; `Main.qml:413-450` | Adapt for Windows | The same 148 strip on both platforms. The count lives in the left-rail Batch section, and selection actions are in the Edit menu. No per-cell checkbox: multi-select plus the Exclude badge. The 2 px active border is adopted. | One layout for both OSes; fewer controls per cell. |
| PhotoFrame grid preview: each photo with its own settings; "excluded" badge; per-photo adjustment badge; "adjust only this one"; count status "n photos · m excluded"; double-click for a large view | PF `README.md:55`; `docs/index.html:396, 457-459`; `shot-grid.jpg` | Adopt | Grid view (`G`, F10): each print rendered with its own settings and status badges; Exclude/Include (`X`); a per-item override badge; status "n prints · m excluded · k need review" at the grid's foot; double-click or Return opens the item. Cell size slider 120–320, default 200. | Matches F10 exactly; observable behaviour only. |

### 2.10 Menus and shortcuts

| Element | Reference (file:line) | Decision | Our design | Reason |
|---|---|---|---|---|
| App menu: About; Settings… ⌘, | `SpektrafilmApp.swift:104-257` | Adopt | macOS app menu: About FrameKeeper; Settings… ⌘,. Windows: Help ▸ About; Edit ▸ Settings… Ctrl+, (§3). | Platform convention. |
| File: Open ⌘O; Export ⌘E | `SpektrafilmApp.swift:104-257` | Adapt for our product | Project and scan commands plus three exports (§4.6). | Our workflow has import, export, relink and final export. |
| Edit: Undo; Copy/Paste Settings ⇧⌘C/V; Select All ⌘A; Sync Settings | `SpektrafilmApp.swift:127-135` | Adapt for our product | Undo/Redo; Copy/Paste Item Settings ⇧⌘C/V (export, composite and synthetic-frame settings, never geometry); Select All Prints ⌘A. | Geometry is per print and must never be pasted blindly. |
| View: zoom in/out ⌘+ ⌘−; fit `,`; 100 % `.`; toggle panels ⌘B; filmstrip ⇧⌘F; bypass ⇧⌘B; before/after `Y`; restart service ⌥⌘R | `SpektrafilmApp.swift:143-157` | Adapt for our product | Zoom ⌘= ⌘−; Fit ⌘0; 100 % ⌘1; `Z` toggles fit ↔ 100 %; rails and filmstrip ⌥⌘0–3; Compare `Y`; Original `\`; Overlays ⇧⌘H; canvas modes `1`–`4`; Grid `G` (§4.6). | Photoshop/Lightroom conventions, which our users already know (PRD §2). |
| Frame menu: Previous ←, Next →; New Half-Frame Pair ⌘J; resets; Reveal | `SpektrafilmApp.swift:104-257` | Adapt for our product | **Print** menu: Previous/Next Print ←/→; Previous/Next Scan ⇧←/⇧→; Mark Reviewed and Next ⌘↩; Exclude/Include `X`; Rotate Whole Scan 90° Counterclockwise / Clockwise (UX-04); Turn This Print Upright Left / Right ⌘[ / ⌘]; Reset to Detected; Reveal Scan ⌥⌘R. | Half-frame pairs are domain-specific. |
| Crop menu: Rotate ⌥⌘[ ]; Flip H/V; Reset Crop | `SpektrafilmApp.swift:104-257` | Adapt for our product | Lossless upright rotation in the Print menu (⌘[ / ⌘], as in Lightroom). No flips. | A scanned print is never mirrored, and a flip would falsify the frame. |
| Tool menu: Select `V`, Hand `H`, Crop `C` | `SpektrafilmApp.swift:104-257` | Adapt for our product | Select `V`, Hand `H` (or hold Space), Edit Outer `O`, Edit Inner `I`. | Our tools edit two boundaries. |
| Mask menu hidden behind a feature flag | `SpektrafilmApp.swift:104-257` | Do not adopt | The mask brush is a control in the Return ▸ Mask group (F07). | No feature flags (F15). |
| Zoom items disabled, not ignored, while the view is locked | `SpektrafilmApp.swift:143-157` | Adopt | Any menu item that cannot act is disabled, with its reason in the status tip. | Honest UI. |
| Shortcuts follow Capture One; stale shortcut docs and tooltips | `SpektrafilmApp.swift:130-135`; `mUI-README:222-237`; `TopBar.swift:121`; `NavigatorSection.swift:48` | Adapt for our product | Photoshop/Lightroom conventions. Each shortcut is defined once on a QML `Action`; menus, tooltips and §4.6 derive from it, and a test compares them. | Prevents the staleness SpektraLab shows (audit §6.1 item 12). |

### 2.11 Windows-specific conventions (from SpektraLab's Windows ports)

| Element | Reference (file:line) | Decision | Our design | Reason |
|---|---|---|---|---|
| Native title bar on Windows (keeps Snap Layouts, resize borders, DPI, accessibility); window title "<file> — SpektraLab" | `desktop/NOTES.md:109-113`; `desktop/PARITY.md:20`; `Main.qml:10-14` | Adapt for Windows | Native title bar with the title "<project name> — FrameKeeper" (with "•" when unsaved), and an in-window menu bar under it (F15; ARCHITECTURE §9). | A platform convention F15 requires. |
| Ctrl replaces ⌘; redo Ctrl+Shift+Z / Ctrl+Y; full screen F11 | `desktop/PARITY.md:18`; `Main.qml:101-108` | Adapt for Windows | Same mapping (§4.6). | Platform convention. |
| Single-key shortcuts are not menu accelerators; the menu shows them as text | `desktop/NOTES.md:100-105` | Adapt for Windows | Single keys are handled by the canvas key handler behind the typing guard and shown as text in menus. Alt opens the menu bar with mnemonics. | A Win32 accelerator would fire inside text fields. |
| Settings, About and Export are in-window dialogs (one store, one session) | `desktop/NOTES.md:106-108` | Adapt for Windows | Windows: in-window dialogs for Settings, About, Export, Relink and Final output. macOS: Settings as a separate window (⌘,), the standard About panel, and in-window dialogs for Export, Relink and Final output. File pickers are native on both (F15). | Platform conventions for each OS. |
| System fonts (Segoe UI Variable / Noto Sans / Microsoft YaHei), nothing bundled; Qt app uses Segoe UI 12 px | `desktop/PARITY.md:25`; `Main.qml:15-16` | Adopt | System fonts only (§4.3). | Native look, and no font licensing. |
| Hard-coded Simplified Chinese strings, no i18n layer | `Main.qml` throughout | Do not adopt | `qsTr` everywhere with en and zh_CN `.ts` files; a missing translation fails CI (audit row for rule 213). | F15 requires both languages. |
| Rails fixed at 246 / 288 | `Theme.qml:15-16` | Do not adopt | Resizable rails on both OSes. | Parity. |
| Window 1540×980, min 1100×740 | `Main.qml:10-14` | Do not adopt | §4.1 values on both OSes. | One spec. |
| Separate 28 px status bar above the strip (size · "SDR sRGB / 8-bit preview" · dirty state) | `Main.qml:409-412` | Do not adopt | Status lives in the top bar (scan name, px size, depth, job progress) and the canvas chips (ICC, bit depth, DPI). The dirty state is in the title. | Saves 28 px of canvas height. |
| Top bar of text buttons (Import RAW, Export, Crop, Undo, Redo, zoom %, Fit, 100 %) | `Main.qml:332-346` | Do not adopt | The same icon top bar on both OSes, each icon with a tooltip (name + shortcut) and an accessible name. | One layout; accessibility handled through names. |
| Disabled stub sections for unported features, with a tooltip from capabilities | `desktop/NOTES.md:85-90` | Do not adopt | Unfinished features are absent (F15). Only capability-profile limits show disabled with a reason. | F15 rule. |
| Crop: drag a rectangle on the unrotated source; presets; straighten slider ±45° applied on release | `Main.qml:250-306, 350-394` | Adapt for our product | Outer and Inner are edited directly on the scan: corners, edges, rotation handle, numeric entry; ellipse centre and radii (F04). Straightening is an export geometry mode (F05), not an edit. Refinement runs on release. | Our boundaries are detected quads and ellipses, not free crops. |

### 2.12 Qt Quick technical patterns (from `windows_UI/`, re-implemented independently)

| Element | Reference (file:line) | Decision | Our design | Reason |
|---|---|---|---|---|
| Qt Quick Controls "Basic" style plus our own theme | `main.cpp:370` | Adopt | Basic style, `app/qml/Theme.qml` singleton (RFC-0011; ADR-0001). | Full control over look on both OSes. |
| Single QML module; native canvas registered from C++ | `windows_UI/CMakeLists.txt:31-34`; `main.cpp:397-400` | Adopt | One `qt_add_qml_module`, with `ScanCanvasItem` and `OverlayItem` registered via `QML_ELEMENT` (RFC-0011). | Standard Qt 6 practice. |
| One controller QObject exposed to QML, with grouped signals | `PreviewController.hpp:18-54, 128-138` | Adopt | `AppController` singleton owning `fk::Project`, list models and sub-controllers (RFC-0011; CONTRACT §2). | Declarative QML; all logic in the core. |
| Serial worker thread; jobs in a deque; queued replies tagged with generation and settings revision | `PreviewController.cpp:61, 72-106` | Adapt for our product | JobQueue lanes (export 1, detect 2, thumb 2, io-light 2), queued invocations, and generation tags (RFC-0010/0011). | Bounded parallelism under a memory budget. |
| Settings editable during a render; open and export single-flight | `PreviewController.cpp:205-214` | Adopt | Items not in a running job stay editable; at most one export runs at a time. | Responsiveness. |
| File dialogs pause pending updates | `Main.qml:36-38`; `PreviewController.cpp:187-192` | Adopt | UI-triggered recomputation is paused while a modal dialog is open. | Avoids surprise work behind dialogs. |
| Export holds the immutable displayed frame | `PreviewController.cpp:239-244` | Adapt for our product | Export jobs snapshot item geometry and settings at start (RFC-0010). | Same guarantee, different mechanism. |
| Batch export: snapshot, serial, cancellable between files, skip existing, case-insensitive name de-duplication | `PreviewController.cpp:473-517` | Adapt for our product | Never overwrite: `nextFreeVersionPath` (RFC-0005). Case-insensitive de-duplication is adopted (macOS and Windows file systems are case-insensitive by default). Cancellation works mid-file and deletes the temp file. | Skipping existing files would drop new versions. |
| A failed open or render keeps the displayed frame; a failed file shows its error on its cell | `PreviewController.cpp:104-106, 121, 132-133` | Adopt | Same: the error icon and tooltip appear on the scan cell. | Honest and non-destructive. |
| Closing during work waits for the current operation | `Main.qml:67-75` | Adapt for our product | Quit asks whether to cancel running jobs, then waits a bounded time; cancelled jobs delete their temp files. | Prompt quit without partial files. |
| Strict versioned edit store, source fingerprint, lock file, conflict detection, atomic save, 64 KiB cap | `EditStore.cpp:48-73, 159-188, 237-327` | Adapt for our product | The project file: `fk.project/1`, unknown keys preserved, fingerprint (RFC-0009), atomic save with `.bak`, concurrent-instance conflict refused (ADR-0005). No 64 KiB cap (projects are under 2 MB). | Projects are user documents. |
| Processing order crop → straighten → quarter turns → flips → downsize → depth | `FrameOutput.hpp:41-45`; `FrameOutput.cpp:134-154` | Adapt for our product | computeExportGeometry, then the RFC-0005 pipeline (read → geometry/upright → optional resample → optional colour conversion → depth → sink → verify). No flips. | Ours is specified in RFC-0002 and RFC-0005. |
| One geometry path for preview, navigator and export | `windows_UI/README.md:33-41` | Adopt | core/geometry only (AGENTS.md §4). | See §2.5. |
| Extension must match format; bundled sRGB ICC embedded | `FrameOutput.cpp:52-71` | Adapt for our product | Extension validated against the format. The **source ICC** is embedded unless a conversion was chosen (ADR-0003). | Archival fidelity. |
| Atomic, no-overwrite publication (temp in the destination folder, rename without replace) | `FrameOutput.cpp:72-83, 238-276` | Adopt | `<target>.fk-tmp-<random>`, then a rename that never replaces (RFC-0003/0005). | The filesystem arbitrates races. |
| `--self-test <new dir>`: offscreen QPA + software backend, named checks, `report.json`, captures; scope stated | `main.cpp:33-361`; `windows_UI/README.md:114-129, 156-164` | Adopt | `FrameKeeper --self-test <dir>` (RFC-0011). Named checks include physical 100 %, pan clamp, superseded result never published, and overlay = export geometry. The report states it does not prove monitor colour or real-window drawing. | The best automated UI evidence short of a real window. |
| Offscreen fonts taken from the system folder | `main.cpp:357-361` | Adapt for our product | Snapshots use the platform's system fonts; assertions are geometric (rects, truncation), not cross-OS pixel diffs. | Fonts differ by OS. |
| Pinned, hash-verified Qt bootstrap (`setup-qt.ps1`, `-VerifyOnly`) | `windows_UI/README.md:77-96` | Adapt for our product | Qt 6.8.3 pinned in CI (install-qt-action); locally any Qt ≥ 6.8 (ADR-0013). | Our CI provider handles Qt. |
| Qt dynamically linked under LGPL, with source and relink information | `windows_UI/THIRD_PARTY.md:3-5, 24-28` | Adopt | ADR-0011; `THIRD-PARTY-LICENSES.md`. | Licence compliance. |
| The sRGB tag does not prove the Windows swapchain applies the monitor ICC | `windows_UI/README.md:158-162` | Adapt for our product | We convert to the monitor ICC ourselves (`GetICMProfileW`; ADR-0003), verified by HV-04. | Accurate frame colour on Windows. |

### 2.13 PhotoFrame / Encadré (observable behaviour only; no licence)

| Element | Reference (file:line) | Decision | Our design | Reason |
|---|---|---|---|---|
| Three border modes: equal width (2/5/10 % presets), canvas ratio (photo centred, never cropped, 4 % minimum margin), custom per side | PF `README.md:22-27`; `docs/index.html:195, 426-427, 492` | Adopt | Return ▸ Final output ▸ Synthetic frame: a mode switch (Equal · Canvas ratio · Four sides), with presets and the 4 % minimum margin default (F08; RFC-0008). | Matches F08. |
| Instant-film preset 5/5/5/18 % of the long edge | PF `shot-light-polaroid.jpg` | Adopt | The default "Instant film" preset (F08). | Matches F08. |
| Sizes in % of the long edge or in px | PF `README.md:29` | Adopt | Unit switch (% of long edge / px). Rounding half-up, identical in preview and export (RFC-0008). | Consistency across mixed orientations. |
| Colour: eyedropper, 6 dominant colours, automatic per photo, per-photo override, swatches, system picker | PF `README.md:33-37` | Adapt for our product | The default is the scanned frame's median colour (RFC-0008). We keep the eyedropper (on the canvas), swatches, system colour dialog and per-item override. No dominant-colour palette. | F08 non-goal. |
| Status strip under the preview: output size, per-side px (↑ ← ↓ →), colour | PF `shot-light-polaroid.jpg` | Adopt | When a synthetic-frame preview is shown, the canvas foot shows "Output W × H px · ↑t ←l ↓b →r px · #RRGGBB". | Shows exactly what will be written. |
| "Export selected (n)" / "Export all (n)" buttons | PF `shot-light-polaroid.jpg`, `shot-grid.jpg` | Adopt | Export buttons carry their counts, e.g. "Export 3 selected…" and "Export all 12…"; low-confidence unreviewed items are listed before confirming (F04). | Clear scope before writing files. |
| Photo list on the left (thumbnail, name, px size) and parameter cards on the right | PF screenshots | Adapt for our product | The Scans list in the left rail shows thumbnail, name, px size, bit depth and print count; right-rail sections (§2.2). | Same idea in our layout. |
| Privacy tiers: keep all / remove location / also remove serial and owner | PF `README.md:57` | Adopt | Export ▸ Metadata & privacy: three presets (F13). Tags to be removed are listed before export. | Matches F13. |
| Platform-size export (4:5, 1:1, 9:16, 3:4): pad, then resize; several sizes at once | PF `README.md:54`; `docs/data/platform_presets.json` | Adopt | Synthetic frame ▸ Platform sizes, as multi-select checkboxes, with the note "platform limits change". | F08; our own preset file. |
| Downloads a camera database at launch | PF `README.md:49` | Do not adopt | — | Fully offline (F14). |
| Watermarks, camera/film logos, guide pages | PF `README.md:43-58` | Do not adopt | — | F08 non-goals. |

### 2.14 Do-not-copy items (visual identity)

| Element | Reference (file:line) | Decision | Our design | Reason |
|---|---|---|---|---|
| App icon layers, `AccentColor` asset, before/after glyph, film covers, DI LUTs, screenshots, design drawings | `SpektraLab.icon/`; `Assets.xcassets/AccentColor.colorset`; `BeforeAfterIcon.swift:1-7`; `Resources/` | Do not adopt | Our own icon and glyph set (`app/resources/`, planned). | GPL and owner assets (audit §8). |
| Product strings ("Film and Print", "Parameters", "Pre-Dev"/"Post-Dev", "Process"/"Original", "CINE", badge texts, Chinese labels) | `LeftPanel.swift`; `RightPanel.swift`; `TopBar.swift`; `Main.qml` | Do not adopt | Our vocabulary: Project, Item, Boundaries, Export, Return, Outer, Inner, Scan, Full Print, Image Area, Composite, Preview, Full. | Distinct product and domain. |
| Identity combination: warm amber accent on warm dark greys, all-bold type, white-capsule selection, accent-outlined capsules | `Theme.swift` (various) | Do not adopt | Teal accent, cool neutrals, two-weight system font, accent-bar selection, flat rectangular buttons (§4). | Our own identity. |

## 3. Platform differences (macOS vs Windows)

| Aspect | macOS | Windows |
|---|---|---|
| Title bar | Standard title bar; full-screen ⌃⌘F | Native title bar (Snap Layouts kept); full screen F11 |
| Menus | Native global menu bar (Qt `MenuBar` maps to it) | In-window menu bar under the title bar; Alt mnemonics |
| Modifier | ⌘ (Command); ⌥ (Option) | Ctrl; Alt |
| Redo | ⇧⌘Z | Ctrl+Shift+Z and Ctrl+Y |
| Settings | App menu ▸ Settings… ⌘, (separate small window) | Edit ▸ Settings… Ctrl+, (in-window dialog) |
| About | App menu ▸ About (standard panel) | Help ▸ About (in-window dialog) |
| Help | Help ▸ FrameKeeper Help ⌘? | Help ▸ Help F1 |
| Quit | ⌘Q | Alt+F4 (also Ctrl+Q) |
| Reveal file | "Reveal in Finder" ⌥⌘R | "Show in Explorer" Ctrl+Alt+R |
| Zoom input | Pinch; ⌘-scroll; two-finger scroll pans | Wheel zooms about the cursor (F15); Shift+wheel pans horizontally; touchpad two-finger pans |
| Single-key shortcuts | Menu items with key equivalents, behind the typing guard | Shown as text in menus only (not accelerators), behind the typing guard |
| Display colour | Tiles converted to sRGB; the window is colour-managed by the OS (ADR-0003) | Tiles converted to the monitor ICC (`GetICMProfileW`), recomputed when the window changes screen |
| DPI | Retina backing scale | Per-Monitor v2 manifest; scales 100–200 % (HV-07) |
| Fonts | System UI font (SF Pro); Chinese PingFang SC | Segoe UI Variable Text (fallback Segoe UI); Chinese Microsoft YaHei UI |
| Dialogs | Native file dialogs; Export, Relink and Final output in-window | Native file dialogs; all other dialogs in-window |
| Memory pressure | dispatch source, warn/critical (`app/src/MemoryPressure.cpp`, FK-005) | `CreateMemoryResourceNotification`, polled every 2 s (same file) |
| Unsigned dev build | Gatekeeper instructions (HV-09) | SmartScreen instructions |

## 4. FrameKeeper layout spec

These are the values for `app/qml/Theme.qml` (RFC-0011). Each token carries a comment naming its row here (audit row for M3), and a planned test compares Theme.qml with this section.

### 4.1 Window and regions (px at 100 % scale)

```
┌─┬──────── left rail ────────┬──────────── centre column ─────────────┬──────── right rail ────────┬─┐
│‹│ header 40 "Project"       │ top bar 40 (tools · mode · status · zoom) │ header 40 "Item"        │›│
│ ├───────────────────────────┤────────────────────────────────────────├────────────────────────────┤ │
│ │ Project & Files           │                                        │ [Boundaries|Export|Return] │ │
│ │ Scans (list, splitter)    │                canvas                  │ sections…                  │ │
│ │ Templates                 │   badges ↗           navigator ↘       │                            │ │
│ │ Detection                 │   chips ↙                              │                            │ │
│ │ Batch                     │                                        │                            │ │
│ │ Jobs                      │                                        │                            │ │
├─┴───────────────────────────┴────────────────────────────────────────┴────────────────────────────┴─┤
│ filmstrip 148 (the whole window width)                                                               │
├──────────────────────────────────────────────────── ⌄ ───────────────────────────────────────────────┤
└──────────────────────────────────────────────────────────────────────────────────────────────────────┘
 8   254 (232–380)                         flexible                           300 (280–420)          8
```
`‹ › ⌄` are the edge strips (UX-10, U022): 8 px bars on the window edge outside each panel.

| Item | Value |
|---|---|
| Window | Default 1440×900, or 90 % of the available screen, whichever is smaller. Minimum 1100×700. |
| Left rail | Width 254, resizable 232–380, foldable (left edge strip). Width and folded state persisted (settings `leftRailWidth`, `showLeftRail`). Ends above the filmstrip. |
| Right rail | Width 300, resizable 280–420, foldable (right edge strip). Width and folded state persisted (`rightRailWidth`, `showRightRail`). Ends above the filmstrip. Under its header: the workflow steps of the current print (UX-06): five 20 px numbered circles (done = filled with a check, next = ringed, connectors filled once done) and "Next: … (step n of 5)"; a click goes to the step (Import dialog, Boundaries / Export / Return tab, Return tab + Composite view). |
| Edge strips (UX-10) | 8 px on the window edge outside the left rail, the right rail and the filmstrip, the full length of that edge; they stay in place when the panel is folded. A chevron points where a click moves the panel (‹ fold left / › unfold …); tooltip "Hide/Show Left Rail (shortcut)"; keyboard focusable, Space/Enter toggles; surface `surfacePanel` (open) or `controlFill` (folded), hover overlay and accent chevron. Hit area 8 + 6 px over the neighbour's empty inset (left rail side, the canvas's fit padding, the filmstrip's bottom margin); not where a scroll bar is next to the strip (right strip while the right rail is open, bottom strip while the filmstrip is folded). The View menu items and the shortcuts below stay. |
| Rail header row | Height 40 (equal to the top bar); title 13 semibold; leading inset 12, trailing 10. |
| Top bar | Height 40, centre column only. Left to right: Import · Export · Relink │ tools V, H, O, I │ status text + job progress (flex) │ Compare · Overlays (eye) │ view switch segmented (Scan · Print · Image · Composite; UX-06: between the eye and the zoom field) │ zoom field 56×24 · − · +. Padding 6; 4 between items; 16 between clusters. Narrow column: the bar gives up, in this order, the file buttons (File menu, left rail), the segmented view switch for a menu button with the same four views (min 96 px), and − / + (zoom field menu, shortcuts); eye → view → zoom never changes, nothing leaves the bar. (The ◧ / ◨ fold buttons were replaced by the edge strips, U022.) |
| Canvas | Flexible, at least 480 wide (`centreMin`): in a narrow window both rails narrow towards their minimums, in proportion to what they can give, without changing their stored widths (UX-06). Surround `surfaceCanvas`. |
| Filmstrip | Height 148 (8 + 108 thumbnail + 4 + 20 label + 8), the whole window width under both rails (UX-10). Cell width = thumbnail aspect × 108, capped at 162; gap 8. ‹ › page chevrons 24 wide. Foldable (bottom edge strip, ⌥⌘3 / Ctrl+Alt+3). |
| Hairlines | 1 px logical: `lineRegion` between regions (full-height overlay dividers; under the top bar; above the filmstrip), `lineSection` between rail sections. |
| Resize grip | 6 px hit area on the rail│canvas divider; 1 px `accent` line on hover; double-click resets. |
| Canvas badges | Top right; inset 10; spacing 4 (§4.7). |
| Canvas chips | Bottom left; inset 10; gap 6; height 20; radius 4; `surfacePanel` at 85 %. Content: ICC description (truncated at 24 characters, full text in a tooltip) or "Untagged"; "8-bit"/"16-bit"; "1600 dpi" or "DPI unknown". |
| Navigator | Bottom right overlay, 200×150, inset 12, `surfaceWell`, 1 px `lineSection` border. Visible when zoom > fit (View ▸ Navigator overrides). |
| Grid view (F10) | Replaces the canvas when toggled (`G`). Cells 120–320, default 200, gap 12; foot status line 28. |
| Scans list (left rail) | Rows 26; draggable splitter; minimum 120, default 240. |

### 4.2 Theme tokens

All values are sRGB hex. Contrast ratios are WCAG 2.x, computed for this spec.

| Token | Dark | Light | Use |
|---|---|---|---|
| `surfacePanel` | `#2A2B2E` | `#ECEDF0` | Rails, top bar, filmstrip, menus, dialogs |
| `surfaceCanvas` | `#5B5C5F` | `#8A8B8E` | Canvas surround only |
| `surfaceWell` | `#1B1C1F` | `#DCDEE2` | Navigator, empty thumbnails, swatch backdrop |
| `controlFill` | `#3B3C40` | `#FFFFFF` | Fields, combo boxes, secondary buttons, slider track |
| `controlStroke` | none | `#C4C7CC` | 1 px field border (light theme) |
| `lineSection` | `#38393D` | `#D2D4D9` | Hairline between sections |
| `lineRegion` | `#17181A` | `#B4B7BD` | Hairline between regions |
| `textPrimary` | `#F0F1F3` | `#1A1B1E` | Primary text, glyphs, slider knob, compare line |
| `textSecondary` | `#C6C8CD` | `#42454B` | Labels, unselected rows |
| `textTertiary` | `#9B9EA4` | `#63666D` | Captions, units, notes (never on selected rows) |
| `accent` | `#35B5A8` | `#0B6F66` | Selected / look here: active tool, focus ring, checked fill, current cell frame, grip hover, primary button |
| `onAccent` | `#0B1A19` | `#FFFFFF` | Text and glyphs on `accent` fill |
| `selectionFill` | `#2C4949` | `#CCDBDD` | Selected list row background (with a 3 px `accent` leading bar) |
| `hoverOverlay` / `pressedOverlay` | `textPrimary` @ 6 % / 12 % | same rule | Hover and pressed states |
| `error` | `#FF8E80` | `#B3261E` | Error text and icons (always paired with an icon) |
| `badgeFill` | `surfacePanel` @ 85 % | `surfacePanel` @ 85 % | Canvas badges and chips |
| `outsideDim` | `#000000` @ 50 % | same | Area outside the edited boundary in Image Area editing |
| `focusRing` | `accent`, 2 px, 1 px outside | same | Keyboard focus only |
| `disabledOpacity` | 0.38 | 0.38 | Whole-row disabling |

**Measured contrast**

- Dark theme:
  - `textPrimary` on `surfacePanel`: 12.5:1
  - `textSecondary`: 8.5:1
  - `textTertiary`: 5.3:1
  - `accent` on panel: 5.6:1
  - `onAccent` on `accent`: 7.1:1
  - `textPrimary` on `selectionFill`: 8.6:1
  - `textPrimary` on `surfaceCanvas`: 5.9:1
- Light theme:
  - `textPrimary` on `surfacePanel`: 14.7:1
  - `textSecondary`: 8.2:1
  - `textTertiary`: 4.9:1
  - `accent` on panel: 5.2:1
  - white on `accent`: 6.0:1
  - `textPrimary` on `selectionFill`: 12.1:1
- Tertiary text on `selectionFill` would be only about 3.6:1, which is why selected rows switch it to `textSecondary`.

### 4.3 Typography

| Role | Size (px) | Weight | Notes |
|---|---:|---|---|
| Dialog title | 15 | Semibold | |
| Rail title | 13 | Semibold | |
| Section title | 12 | Semibold | |
| Body and labels | 12 | Regular | Label column = widest label + 8, clamped to 84–132 |
| Values | 12 | Regular | Tabular figures (`tnum`) |
| Captions and units | 11 | Regular | `textTertiary` |
| Badges and chips | 11 | Semibold | |
| Filmstrip label | 11 | Regular | `P01 · scan01` |

**Families.** macOS uses the system UI font (SF Pro) with PingFang SC for Chinese. Windows uses Segoe UI Variable Text, falling back to Segoe UI, with Microsoft YaHei UI for Chinese. Nothing is bundled.

**Scaling.** Follow the system scale and per-monitor DPI. There is no in-app type scale in v1.

### 4.4 Controls

| Control | Spec |
|---|---|
| Hit target | ≥ 28×28 for every interactive element |
| Section header | Height 28, chevron 10, gap 6, optional reset icon 12 and "•••" menu, 10 below content |
| Row | Inset 14 from both rail edges; control height 24; pitch 30; sub-row 18 |
| Numeric field | 64×24, radius 4, unit suffix (px, mm, °, %), arrow-key steps (⇧ ×10) |
| Combo box | Minimum 120×24, radius 4 |
| Button | Height 28, radius 4, padding 12. Primary: `accent` fill + `onAccent` text (one per dialog or section). Secondary: `controlFill`. |
| Checkbox | 14 (checked fill `accent`); label 12 |
| Slider | Track 2, knob 12 (`textPrimary`), value field to the right |
| Segmented switch | Height 26, radius 4, selected segment `controlFill` + `textPrimary`, others `textSecondary`; accent dot 6 for overrides |
| List row | Height 26; selected = `selectionFill` + 3 px `accent` leading bar |
| Tooltip | Delay 600 ms; for shortcuts, name + shortcut; for disabled rows, the reason |
| Notifications | Non-modal toasts at the canvas bottom centre, 6 s, with Details (CONTRACT §2) |

### 4.5 Zoom and canvas behaviour

- **Range.** min(fit, 2 %) to 800 %.
- **Steps (⌘= / ⌘−).** 2, 3, 5, 8, 12.5, 16.7, 25, 33.3, 50, 66.7, 100, 150, 200, 300, 400, 800 %.
- **Zoom-field menu.** Fit, 25 %, 50 %, 100 %, 200 %, 400 %, 800 %.
- **100 %.** One scan pixel per physical device pixel. At 100 % the image origin snaps to device pixels.
- **Fit and pan.** Fit = whole current content (scan, print, image area or composite) with 16 px padding. There is no panning at or below fit. Above fit, overscroll is at most 48 px past any image edge.
- **Zoom anchor.** Zoom is about the cursor (gestures and wheel) or the viewport centre (keys and menu). Double-click on empty canvas toggles fit ↔ 100 % about the click point. `Z` toggles fit ↔ 100 % about the cursor.
- **Sampling.** Nearest-neighbour at ≥ 100 %, linear below.
- **Resolution state.** `Preview` while scale × device pixel ratio ≤ (1 / proxyScale) × 1.05 (device pixels per scan pixel decide; fixed in U016 after a 2× run showed the proxy at 100 %). Above that, full-resolution 256 px tiles are requested after a 150 ms idle (RFC-0010/0011): `Full…` while loading, `Full` when every visible tile is from the original.
- **Edit tools stay zoomable.**
- **Arrow keys.** With a handle selected, arrows nudge it 1 scan px (⇧: 10 px), also ⇧← / ⇧→ (which otherwise step scans). With no handle selected, they navigate prints.
- **Outer / Inner (UX-03, U013).** A lone tap of ⇧ (pressed and released with nothing else in between, within 1 s) switches the edited boundary Outer ↔ Inner; from Select or Hand it goes to Inner. Holding ⇧ only constrains a drag: a quad corner keeps the rectangle (right angles, side ratio, rotation) about the opposite corner, a move or centre drag locks to the dominant axis, an ellipse's radii scale together; a circle always stays a circle. ⇧+click, ⇧+arrows, ⇧+wheel, ⇧+letter, shortcuts, a mouse button held, typing, an open dialog or menu, the window losing focus, and a drag in progress never switch. The canvas states the edited boundary at the top centre ("Editing Outer (paper edge) · tap Shift to switch"); Print ▸ Edit Outer / Edit Inner / Switch Outer ↔ Inner and the tool buttons do the same. Settings can turn the tap off. One complete drag is one undo step; a drag interrupted by focus loss or Esc changes nothing.

### 4.6 Keyboard shortcuts

Single-key shortcuts (marked †) are disabled while a text or number field has focus. On Windows they appear as text in menus, not as accelerators.

| Command | macOS | Windows |
|---|---|---|
| New Project | ⌘N | Ctrl+N |
| Open Project… | ⌘O | Ctrl+O |
| Import Scans… | ⌘I | Ctrl+I |
| Import Folder… | ⇧⌘I | Ctrl+Shift+I |
| Save / Save As… | ⌘S / ⇧⌘S | Ctrl+S / Ctrl+Shift+S |
| Export Image Areas… | ⌘E | Ctrl+E |
| Relink Graded Files… | ⌥⌘I | Ctrl+Alt+I |
| Export Final… | ⇧⌘E | Ctrl+Shift+E |
| Close Project | ⌘W | Ctrl+W |
| Quit | ⌘Q | Alt+F4 / Ctrl+Q |
| Undo / Redo | ⌘Z / ⇧⌘Z | Ctrl+Z / Ctrl+Shift+Z, Ctrl+Y |
| Select All Prints | ⌘A | Ctrl+A |
| Copy / Paste Item Settings | ⇧⌘C / ⇧⌘V | Ctrl+Shift+C / Ctrl+Shift+V |
| Settings… | ⌘, | Ctrl+, |
| Zoom In / Out | ⌘= / ⌘− | Ctrl+= (Ctrl++) / Ctrl+− |
| Fit / 100 % | ⌘0 / ⌘1 | Ctrl+0 / Ctrl+1 |
| Toggle Fit ↔ 100 % | Z † | Z † |
| Canvas mode: Scan / Full Print / Image Area / Composite | 1 / 2 / 3 / 4 † | 1 / 2 / 3 / 4 † |
| Grid ↔ Single view | G † | G † |
| Show/Hide Overlays | ⇧⌘H | Ctrl+Shift+H |
| Compare split (Composite mode) | Y † | Y † |
| Toggle Original ↔ Composite | \ † | \ † |
| Toggle left rail / right rail / filmstrip / all (also the edge strips, UX-10) | ⌥⌘1 / ⌥⌘2 / ⌥⌘3 / ⌥⌘0 | Ctrl+Alt+1 / 2 / 3 / 0 |
| Full Screen | ⌃⌘F | F11 |
| Previous / Next Print | ← / → † | ← / → † |
| Previous / Next Scan | ⇧← / ⇧→ † | Shift+← / Shift+→ † |
| Mark Reviewed and Next | ⌘↩ | Ctrl+Enter |
| Exclude / Include | X † | X † |
| Turn This Print Upright Left / Right (lossless, export orientation of one print) | ⌘[ / ⌘] | Ctrl+[ / Ctrl+] |
| Rotate Whole Scan 90° Counterclockwise / Clockwise (view of the whole scan, every print follows) | Print menu, left rail, scan list menu | Print menu, left rail, scan list menu |
| Reveal Scan | ⌥⌘R (Finder) | Ctrl+Alt+R (Explorer) |
| Tools: Select / Hand / Edit Outer / Edit Inner | V / H / O / I † | V / H / O / I † |
| Temporary Hand | hold Space † | hold Space † |
| Nudge selected handle 1 px / 10 px | arrows / ⇧ arrows † | arrows / Shift+arrows † |
| Value field (mask width, UX-09): adjust / type | drag left–right (⇧ ×10, ⌥ ×0.1) / double-click or ↩ | drag left–right (Shift ×10, Alt ×0.1) / double-click or Enter |
| Switch Outer ↔ Inner | tap ⇧ † | tap Shift † |
| Constrain a drag (rectangle corner, axis, uniform radii) | hold ⇧ while dragging | hold Shift while dragging |
| Commit numeric entry / cancel drag or leave tool | ↩ / Esc | Enter / Esc |
| Help | ⌘? | F1 |

### 4.7 Canvas badges (resolution and state)

The stack sits top right, inset 10, spacing 4. Each badge is a capsule: height 20, horizontal padding 8, radius 10, `badgeFill`, text 11 semibold `textPrimary`. Contrast is at least 7.7:1 whether the capsule sits over a white or a black frame.

| Badge | Shown when | Notes |
|---|---|---|
| `Preview` | The canvas shows proxy pixels: scale ≤ 1/proxyScale × 1.05, or full tiles not yet requested | Text fixed by CONTRACT-ui-core §4. Also on the composite preview computed on the proxy (ARCHITECTURE §7). |
| `Full…` | Full-resolution tiles are loading for the visible area | Text fixed by CONTRACT-ui-core §4. Replaces `Preview` without a flash; the proxy stays visible underneath. |
| `Full` | Every visible tile comes from the original scan (or from the full-resolution composite) | Text fixed by CONTRACT-ui-core §4. The log line and the badge are emitted from the same state change. |
| `Original` | Composite mode, while `\` shows the scan instead of the composite | Ours. |
| `Not enough memory` | A full-resolution request or job is waiting or refused for budget (`Errc::memory_budget`) | Ours. The tooltip gives the budget and the request size. |
| `Cannot read scan` | The source is missing or damaged | Ours. It goes with the centred card (Relocate… / Remove). |

### 4.8 Overlay styles

Colours and base line styles are fixed by CONTRACT-ui-core §4 and repeated here. Widths, dash lengths, halos and marks are this spec's refinements.

Shared rules:

- Every overlay stroke is drawn at 90 % opacity over a 1 px halo of black at 60 % on both sides, so it stays visible on white, cream, black and coloured frames.
- Overlays can be hidden (⇧⌘H / Ctrl+Shift+H) and never affect pixels.
- Nothing relies on colour alone (F04, F15): each state differs in dash pattern or carries an icon or mark.

| State | Colour (CONTRACT §4) | Line | Width | Icon / mark | Precedence |
|---|---|---|---|---|---|
| **Outer** (detected: auto or template) | `#3D8BFF` blue | Solid | 2 px | None | Base for the Outer boundary |
| **Inner** (detected) | `#F2C744` yellow | Solid | 1.5 px | **Inward corner ticks**: 8 px ticks at each quad corner pointing inward; on an ellipse, four 8 px radial ticks at 0°, 90°, 180° and 270° | Base for the Inner boundary. The ticks are proposed here; see the note below. |
| **Manual** (boundary edited by the user) | `#FF8A3D` orange | Dashed 6 on / 4 off | Role width (2 / 1.5) | Inner keeps its ticks | Overrides the role colour and line |
| **Low confidence** (unreviewed, confidence < 0.6) | `#FF4D4D` red | Dash-dot 8 / 3 / 2 / 3 | 2 px | Warning-triangle icon, 14 px, outside the top-left corner | Highest. Removed when marked reviewed. |
| **Relinked** (Inner has a matched graded file) | `#38C172` green | Solid | 1.5 px (Inner) | Link icon, 14 px, outside the top-right corner of Inner | Overrides Inner's colour only when no higher state applies. Icons accumulate. |

Precedence for line colour and pattern runs: low confidence > manual > relinked > role default. Icons always accumulate; a print can show both the warning icon and the link icon, for example.

**Selection and handles.** The selected boundary's stroke is 1 px wider.

- Corner handles: 9×9 squares, white fill, 1 px black border.
- Edge-midpoint handles: 7×7 squares.
- Rotation handle: a 10 px circle 24 px outside the top-edge midpoint.
- Ellipse: a centre handle plus four radius handles.
- Every handle has a 28 px hit target.
- In Image Area edit mode, the area outside the edited boundary is dimmed with `outsideDim`.

**Note: Inner ticks.** CONTRACT-ui-core §4 lists Outer and Inner as "solid". The inward ticks keep Inner solid while making it distinguishable from Outer without colour, as F04 requires. They should be recorded in CONTRACT-ui-core §4 at its next change (audit §6.2 item 20).

### 4.9 Filmstrip and grid status badges

Statuses are derived, never stored (CONTRACT-ui-core §3).

- **Placement.** Icons are 14 px, white, in a row at the cell's bottom left over a gradient scrim of black at 55 %. At most 4 are shown, then "+n".
- **Accessibility.** Each icon has a tooltip and an accessible name, and the shapes differ, so colour is never the only carrier.

| Status (CONTRACT §3) | Mark |
|---|---|
| detection `auto` / `template` / `manual` / `none` | none / grid icon / pencil icon / dashed-outline icon |
| confidence `low` (unreviewed) | Red ⚠ triangle; the cell label is shown in `error` |
| confidence `medium` | Hollow ⚠ outline |
| `reviewed` | Check icon |
| `excluded` | Cell dimmed to 40 % + slashed-circle icon |
| `exported` (latest edit version) | Arrow-out icon |
| `relinked` | Link icon |
| `sizeAnomaly` / `iccAnomaly` | Ruler with "!" / colour-wheel with "!" |
| `composited` | Layers icon |
| `finalExported` | Double-check icon |
| Current item | 2 px `accent` frame |
| Selected (multi-select) | 1 px `textPrimary` @ 60 % frame + check glyph in the top right |
| Read error | `error` triangle with the reason in a tooltip (the cell keeps its last thumbnail) |

### 4.10 Right-rail groups by tab

| Tab | Sections (F15 groups) |
|---|---|
| **Boundaries** | Outer (quad or rounded-corner fields, film type, confidence and reasons, Reset to detected) · Inner (quad or ellipse, prior/measured source) · Geometry (upright 0/90/180/270, geometry-mode preview) · Safety margin (px or mm) |
| **Export** | Export image area (format, depth, compression, geometry mode, naming, folder, "frame pixels stay bit-identical: yes/no + why") · Colour (keep / convert once / assign / keep untagged; intent; BPC) · Metadata & privacy (three presets + the tag list) |
| **Return** | Re-import (relink candidates, match method, content-check result, required confirmations) · Composite (preview, original/graded/composite, split) · Mask (per print, UX-08: natural blend = feather 0.05 mm by default / preserve edge 0.02 mm / hard edge 0 px (archival exact); width in mm with the px equivalent, px without a reliable DPI; "Natural default"; "Apply to N selected prints"; a warning when the grade changes the edge strongly; brush) · Final output (format, depth, colour; mode **Real scanned frame** (default) or **Synthetic frame**, with the F08 controls and the platform sizes) |
