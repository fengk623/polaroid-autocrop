# Acceptance matrix

Each release acceptance item → PRD → RFC → code → test → CI job → evidence. Status: ☐ open · ◐ partly · ☑ done (evidence linked) · HV = needs human verification.

| # | Acceptance item | PRD | RFC/ADR | Code | Test | CI job | Status |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | Runs on macOS and Windows | F15 | RFC-0011, ADR-0013 | app/ | release smoke `--self-test` | release.yml | ☐ |
| 2 | Handles 300–1600 DPI TIFF > 1 GB, BigTIFF, 8/16-bit, grey, compressions | F01 | RFC-0003 | core/image_io | test_tiff_io, perf | ci.yml, nightly.yml | ☐ |
| 3 | Finds 1–12 mixed prints per scan | F02 | RFC-0004 | core/detection | test_detection_*, eval | ci.yml | ☐ |
| 4 | Real frame fully kept; inner_cut_violations = 0 | F02 | RFC-0004 | core/detection | eval | ci.yml | ☐ |
| 5 | Inner detected incl. round frame | F03 | RFC-0004 | core/detection | test_detection_inner, eval | ci.yml | ☐ |
| 6 | Manual Outer/Inner editing | F04 | RFC-0002, RFC-0011 | app/canvas | tst_boundary_edit | ci.yml | ☐ |
| 7 | JPEG/PNG/TIFF/valid Linear DNG export | F05 | RFC-0005, ADR-0004 | core/export | test_export_*, test_dng | ci.yml | ☐ |
| 8 | Relink graded files to the right print + content check | F06 | RFC-0006 | core/relink | test_relink | ci.yml | ☐ |
| 9 | Inverse mapping back into the image area; frame untouched | F07 | RFC-0007 | core/compositing | test_composite | ci.yml | ☐ |
| 10 | Lossless final export; bit-exact under §8.1 conditions | F07 | ADR-0010 | core/compositing | test_roundtrip | ci.yml | ☐ |
| 11 | Depth/ICC/DPI preserved; no silent 16→8 | F05, F12 | RFC-0012 | core/color | test_export_depth, test_color | ci.yml | ☐ |
| 12 | Source scans byte-identical | F01 | RFC-0003 | core | test_source_untouched | ci.yml | ☐ |
| 13 | Jig templates; batch | F09, F10 | RFC-0004, RFC-0010 | core/project, core/jobs | test_templates, test_batch | ci.yml | ☐ |
| 14 | Projects save/restore/migrate/relocate | F11 | RFC-0009 | core/project | test_project | ci.yml | ☐ |
| 15 | Preview = export geometry | F04 | RFC-0002 | core/geometry | test_geometry_consistency | ci.yml | ☐ |
| 16 | Memory independent of batch size; 8 GB tier works | F10, F14 | RFC-0015 | core | perf batch test; HV-05 | nightly.yml | ☐ HV |
| 17 | Cache/temp bounded and clearable | F14 | RFC-0014 | core/cache | test_cache | ci.yml | ☐ |
| 18 | Offline | F14 | SECURITY-PRIVACY | — | check-no-network | ci.yml | ☐ |
| 19 | UI learns from SpektraLab, platform conventions, real-window captures | F15 | RFC-0011 | app/ | snapshots; HV-02, HV-03 | ci.yml | ☐ HV |
| 20 | PhotoFrame-style synthetic frame clearly separated | F08 | RFC-0008 | core/compositing | test_synthetic_frame | ci.yml | ☐ |
| 21 | PRD per feature, RFC per module, automated tests, CI green on all OSes | all | — | — | — | ci.yml | ◐ (PRDs/RFCs written) |
| 22 | Specs, packaging and release scripts; SpektraLab compliance matrix | — | ADR-0013 | tools/release | release dry run | release.yml | ◐ (matrix in progress) |
| 23 | All performance claims measured; no fake buttons/TODO core flows/fake results | — | — | — | review | — | ☐ |
| 24 | Licence ADR done; neutral product name | — | ADR-0011, ADR-0012 | cmake/ProductInfo.cmake | check-licenses | ci.yml | ◐ |
