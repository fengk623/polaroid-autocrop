# Acceptance matrix

Each release acceptance item → PRD → RFC → code → test → CI job → evidence. Status: ☐ open · ◐ partly · ☑ done
(evidence linked) · HV = needs human verification. Updated 2026-10-31 (U027, FK-013): every test named here
exists (`tools/check-claims.py` checks the backticked names in CI); something not built says so. The per-ticket
view of the v1.0 work is `docs/v1/TRACEABILITY.md`.

| # | Acceptance item | PRD | RFC/ADR | Code | Test | CI job | Status |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | Runs on macOS and Windows | F15 | RFC-0011, ADR-0013 | `app/` | packaged self-test (`release.yml` smoke), app self-test on all three CI platforms | `ci.yml`, `release.yml` | ◐ CI builds and self-tests run on Windows, macOS (arm64; universal package) and Linux; real installs: HV-02, HV-03, HV-08, HV-09 |
| 2 | Handles 300–1600 DPI TIFF > 1 GB, BigTIFF, 8/16-bit, grey, compressions | F01 | RFC-0003 | `core/src/tiff_source.cpp` | `test_tiff_io` (`TiffRead.BigTiff`, `TiffRead.SingleStripCompressedRandomRowOrder`, `TiffRead.MinIsWhiteIsInvertedOnRead`), PERFORMANCE-BUDGET §3 | `ci.yml` | ◐ > 1 GB scans measured on generated files; the owner's 679 MB 4-up scan not run (HV-01) |
| 3 | Finds 1–12 mixed prints per scan | F02 | RFC-0004 | `core/src/detection/` | `test_detection`, `eval_corpus`, `realeval_fixture` | `ci.yml` | ◐ generated corpus and the owner's scans (REAL-SCAN-GT); 1200/1600 DPI real scans pending (HV-01) |
| 4 | Real frame fully kept; inner_cut_violations = 0 | F02 | RFC-0004 | `core/src/detection/` | `eval_corpus` (cuts_on_detected = 0), `Detection.FindsAndContainsTwoTiltedPrints`, `Detection.WhitePrintOnWhiteLidExcludesTheShadowBand` | `ci.yml` | ◐ measured (TEST-PLAN §4); real-scan GT of the owner's scans (U012) |
| 5 | Inner detected incl. round frame | F03 | RFC-0004 | `core/src/detection/` | `eval_corpus` (inner error), `Detection.FindsAndContainsTwoTiltedPrints`, `realeval_fixture` | `ci.yml` | ◐ |
| 6 | Manual Outer/Inner editing | F04 | RFC-0002, RFC-0011 | `core/src/geometry.cpp` (`editShape`), `app/src/ScanCanvas.cpp`, `app/src/ShiftTap.cpp` | `test_geometry` (`Edit.*`), self-test `runShapeEditing` | `ci.yml` | ☑ automated; real-window feel HV-02/03 |
| 7 | JPEG/PNG/TIFF/valid Linear DNG export | F05 | RFC-0005, ADR-0004 | `core/src/export.cpp`, `core/src/tiff_sink.cpp`, `core/src/image_file.cpp` | `test_export` (`Export.LinearDngStructure`, `Export.PngSixteenBitExactAndXmp`), `test_png_jpeg`, LibRaw decode `tools/dng-interop.py` | `ci.yml` | ◐ DNG opened in Adobe/Capture One: HV-06 |
| 8 | Relink graded files to the right print + content check | F06 | RFC-0006 | `core/src/relink.cpp`, `core/src/project.cpp` | `test_relink`, `test_project` (`RelinkV2.*`, `ExportRecords.*`), self-test relink scenario and checks | `ci.yml` | ☑ automated; Photoshop/Lightroom round trip HV-06 |
| 9 | Inverse mapping back into the image area; frame untouched | F07 | RFC-0007 | `core/src/composite.cpp` | `Composite.GradedPixelsOnlyInsideTheImageArea`, `Composite.StraightenedRoundTripKeepsFrameExact` | `ci.yml` | ☑ |
| 10 | Lossless final export; bit-exact under §8.1 conditions | F07 | ADR-0010 | `core/src/composite.cpp` | `Composite.UnchangedEditRoundTripIsBitExact` (4 uprights), `NaturalBlend.UnchangedEditStaysBitExactWithEveryMask`, `fkcli roundtrip`, self-test "round trip bit-exact" | `ci.yml` | ☑ |
| 11 | Depth/ICC/DPI preserved; no silent 16→8 | F05, F12 | RFC-0012 | `core/src/color.cpp`, `core/src/output_stage.cpp` | `Export.DepthReductionNeedsConsent`, `Export.IccPoliciesKeepAssignConvertUntagged`, `Export.DpiPreservedAndScaledForStandardSize`, `Composite.LowerDepthNeedsConsent` | `ci.yml` | ☑ |
| 12 | Source scans byte-identical | F01 | RFC-0003 | core | `Export.SourceFileUntouched`, `Composite.NeverOverwritesAndSourceUntouched` | `ci.yml` | ◐ export and composite; one end-to-end hash test over every step not built |
| 13 | Jig templates; batch | F09, F10 | RFC-0004, RFC-0010 | `core/src/project.cpp`, `app/src/Jobs.cpp` | `Template.SaveLoadRoundTrip`, `Detection.TemplateReproducesItems`, self-test `runTaskSafety` | `ci.yml` | ◐ cross-DPI template and batch-continues tests not built |
| 14 | Projects save/restore/migrate/relocate | F11 | RFC-0009, RFC-0017 | `core/src/project.cpp` | `test_project` (`Project.*`, `DataProtection.*`), self-test `runDataProtection` | `ci.yml` | ◐ library migration (RFC-0018) waits for the owner |
| 15 | Preview = export geometry | F04 | RFC-0002 | `core/src/geometry.cpp`, `app/src/ScanCanvas.cpp` | `Geometry.*`; self-test clicks on prints after folding and resizing (`runLayout`), canvas checks in `runShapeEditing` | `ci.yml` | ◐ a pixel consistency test with known quads is not built |
| 16 | Memory independent of batch size; 8 GB tier works | F10, F14 | RFC-0014, RFC-0015 | `core/src/cache.cpp`, `app/` | `CacheLedger.*`; self-test `runMemoryBudget`; `--scenario memory` batches (PERFORMANCE-BUDGET §3) | `ci.yml` | ◐ real 8 GB device: HV-05 |
| 17 | Cache/temp bounded and clearable | F14 | RFC-0014 | `core/src/cache.cpp` | `CacheLedger.DropsLeastRecentlyUsedFirst`, `CacheLedger.ShrinkForMemoryPressureAndUpdateSizes`; temp files: `TiffWrite.NeverOverwritesAndAbortRemovesTemp` | `ci.yml` | ◐ in-memory caches; a disk cache is not built (ADR-0006) |
| 18 | Offline | F14 | SECURITY-PRIVACY | — | `tools/check-no-network.py` | `ci.yml` | ☑ |
| 19 | UI learns from SpektraLab, platform conventions, real-window captures | F15 | RFC-0011 | `app/` | self-test `runLayout`, `runToolbar`, `runTextLayout` (FK-009); HV-02, HV-03, HV-07 | `ci.yml` | ◐ automated layout and text checks; real windows HV |
| 20 | PhotoFrame-style synthetic frame clearly separated | F08 | RFC-0008 | `core/src/composite.cpp` | `SyntheticFrame.*`, self-test "synthetic frame exports" | `ci.yml` | ☑ |
| 21 | PRD per feature, RFC per module, automated tests, CI green on all OSes | all | — | — | `tools/check-claims.py` (claims have code), `tools/check-docs.py` | `ci.yml` | ◐ |
| 22 | Specs, packaging and release scripts; SpektraLab compliance matrix | — | ADR-0013 | `tools/release/` | release dry run, packaged self-test | `release.yml` | ◐ (U028) |
| 23 | All performance claims measured; no fake buttons/TODO core flows/fake results | — | — | — | `tools/check-claims.py`; review | `ci.yml` | ◐ |
| 24 | Licence ADR done; neutral product name | — | ADR-0011, ADR-0012 | `cmake/ProductInfo.cmake` | `tools/check-licenses.py` | `ci.yml` | ◐ |

Outcomes are reported apart (FK-013): CTest `tools/test_report.py` — passed, failed, skipped, not started, blocked;
the app self-test — passed, failed, skipped, known issues, not applicable. Nothing skipped, not started or
blocked counts as passed.
