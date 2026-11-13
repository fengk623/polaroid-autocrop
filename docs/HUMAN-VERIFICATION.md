# Human verification

Items that cannot be proven in CI or in the cloud development sandbox. Each stays **Pending** until evidence
(screenshot path, measurement, or the verifier's note with date) is added. Release Notes list every
Pending item as "not yet verified".

| ID | What | How | Expected | Evidence | Status |
| --- | --- | --- | --- | --- | --- |
| HV-01 | Detection on real Epson V750 scans (800/1200/1600 DPI, 4-up) | Put scans in a folder, run `fkcli detect <scan> --overlay out.png` or open in the app; check every print | no print cut; tight fit; low-confidence flags where unsure | 2026-10-28, agent on the owner's 5 scans (not in the repo): 4-up 800 DPI white frames on a white lid — 4/4 found, no cut, outer ≈ 0.3–1.4 mm outside the paper (soft lid glow), inner on the picture edges, effective 826 DPI detected (tag 800); 3 single prints cropped in SilverFast/Epson Scan (1200 DPI, 8/16-bit, Adobe RGB / Epson RGB) — outer = scan border with thin dark slivers trimmed, inner on the picture edges. 679 MB 4-up scan not tested (transfer limit). | Partly done |
| HV-02 | macOS real-window captures (main, Outer edit, Inner edit, compare, Composite, 100 %, grid, export, settings, error) | On a Mac: `FrameKeeper.app/Contents/MacOS/FrameKeeper --self-test ~/fk-live --scan <a scan.tif> --live` (real window, Metal; writes snapshots + report.json, uses its own settings, never changes the scan), then the remaining views by hand | canvas draws, overlays aligned, menus native | — | Pending |
| HV-03 | Windows real-window captures (same list) | `FrameKeeper.exe --self-test %USERPROFILE%\fk-live --scan <a scan.tif> --live` (real window, Direct3D), then the remaining views by hand | as above; Windows title bar/menus/shortcuts | — | Pending |
| HV-04 | Display colour accuracy | open `tests/data/color-patches.tif` (generated), compare with a colour-managed viewer | patches match | — | Pending |
| HV-05 | 8 GB device run, 1600 DPI A4 16-bit scan: detect, export all, composite all | measure peak memory (Activity Monitor / Task Manager + app diagnostics) | < 2 GB peak | — | Pending |
| HV-06 | Graded round trip through Photoshop and Lightroom (XMP kept/dropped) | export → edit → save → relink | correct item matched; frame untouched | — | Pending |
| HV-07 | High-DPI: 150 %/200 % Windows scaling, Retina | visual check; FK-009 (U024): also Chinese, dark and light, at 1100×700 and full screen: no boxes instead of characters, no cut labels or buttons, top bar, edge strips and workflow steps uncovered (the offscreen checks in CI cover the layout rules, not the real renderer or the system fonts) | crisp UI, 100 % = 1 image px per device px; text readable | — | Pending |
| HV-08 | Installer/uninstaller on clean Windows | install, run, uninstall | projects kept after uninstall | — | Pending |
| HV-09 | Gatekeeper on unsigned macOS build | open per README instructions | app opens after approval | — | Pending |
