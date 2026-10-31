# TEST-PLAN

## 1. Layers
| Layer | Tool | Location | Runs in |
| --- | --- | --- | --- |
| Unit (core) | GoogleTest | `tests/core/` | every CI job (Linux, macOS, Windows) |
| Contract | GoogleTest: compile-time API signatures and enum values, stable names (`ApiContract.*`); JSON via the `Project.*` / `ExportRecords.*` round trips | `tests/core/test_api_contract.cpp` (U027) | every CI job |
| Claims | `tools/check-claims.py`: tests, files, CI jobs and API names the specifications mention exist (or are marked planned) | — | CI docs job |
| C ABI | C11 test | not built (the C ABI is planned, ADR-0007) | every CI job |
| Integration | `fkcli` scripts | `tests/eval_corpus.cmake`, `tests/realeval_fixture.cmake` (CTest) | every CI job |
| Detection eval | `fkfixture` + `fkcli eval` | CTest label `eval` | every CI job (small corpus), nightly (large) |
| UI | `--self-test` (real controllers, injected input, offscreen; scenarios `relink`, `count`, `upright`, `miniswhite`, `memory`, `text`) | `app/src/SelfTest.cpp` (no Qt Quick Test suite is built) | Linux + macOS + Windows CI |
| Compatibility | GoogleTest on files written by earlier releases' own code, and a rollback round trip through the newest one (`Compat.*`, FK-014) | `tests/core/test_compat.cpp`, `tests/data/compat/` (`tools/compat/make-fixtures.sh`) | every CI job |
| Packages | the packages as the user receives them: contents, licences, BUILDINFO, version (`tools/release/check-package.py`); DMG mounted, app copied and run, upgrade/rollback by replacing it (`tools/release/test-macos-dmg.sh`); ZIP unpacked and run; installer install/upgrade from every published release/rollback/uninstall with the user's data checked (`tools/release/test-windows-install.sh`) | `tools/release/` | `release.yml` on every PR into `main` or a unit branch |
| Real windows | manual / computer-use capture | `docs/HUMAN-VERIFICATION.md` | before release |
| Performance | `fkcli perf` | `fkcli perf`, self-test `--scenario memory`, `tools/memory-summary.py` | nightly + before release |

CTest labels in use: `core`, `geometry`, `io`, `color`, `metadata`, `export`, `detection`, `composite`,
`relink`, `project`, `eval` (from `tests/CMakeLists.txt`; `cache`, `jobs`, `contract`, `app` and `perf` labels
are not built — the contract test runs under `core`, the app is tested by its self-test).

## 2. Gates
- While editing module X: `ctest -L X` (plus `contract` if an interface changed).
- Before merging to `main`: `tools/run-tests.sh` (all labels except `perf`) on Linux, and CI on all OSes.
- Bug fix: regression test committed red first (or shown red in the commit message with the command output), then fix.
- Outcomes are counted apart and nothing but a pass counts as a pass (FK-013): `tools/run-tests.sh`
  (`tools/test_report.py`) prints passed / failed / skipped / not started / blocked and exits non-zero on a
  failure, on a test CTest could not start (missing program, missing permission — the review environment's "4 not
  started for lack of permission" would fail here), and on any skip not listed in `tests/allowed-skips.txt`
  (empty by default; an entry whose reason says "blocked:" is reported as blocked, never as passed). The app
  self-test ends with "self-test results: N passed, N failed, N skipped, N known issues (N no longer reproduce),
  N not applicable" and writes the same `summary` into `report.json`.

## 3. Fixtures
- Tiny committed fixtures: none for images (planned: ≤ 64 KB each, hand-made edge cases); tests generate their
  images. Committed text fixtures: `tests/data/compat/` (FK-014) — projects, export records and templates written
  by releases 0.5.0 and 0.6.0-alpha.1 (synthetic content, invented paths, no pixels), regenerated only with
  `tools/compat/make-fixtures.sh`.
- Generated fixtures: `fkfixture` writes into a per-test temp dir; seeds fixed in tests; nothing generated is committed.
- Real scans: optional via `FK_PRIVATE_SCANS_DIR`, read-only; tests that need them are labelled `private`
  and never run in CI.
- Tests copy any input they might touch into `fk::test::TempDir`; settings live in a temp `FK_SETTINGS_DIR`.

## 4. Detection thresholds (CTest `eval_corpus`, label `eval`; seed-1 corpus of 12 scans, RFC-0016)
| Metric | Threshold | Measured (24-scan corpus, 2026-10-28) |
| --- | --- | --- |
| cuts_on_detected (any confidence) | 0 | 0 |
| missed_visible | 0 | 0 (10 low-visibility prints missed, all with `count_mismatch`) |
| outer slack median / P95 / max (visible) | ≤ 3 px / ≤ 8 px / ≤ 20 px | 1.18 / 1.77 / 8.0 px |
| inner error median / P95 (visible) | ≤ 3 px / ≤ 10 px | 0.08 / 0.45 px |
| count accuracy (scans with only visible prints) | 100 % | 100 % |
| orientation accuracy (visible) | ≥ 99 % | 100 % (film type 100 %) |
| low-confidence recall | ≥ 90 % | 100 % |
| detection time per 300–400 DPI A4 scan (median, 2 cores) | informational | 1.1 s |

Low visibility = frame within ΔE 4 of the background (black on black). Such prints must never be cut when
detected (conservative expansion, flagged); missing them is allowed only with the scan-level count warning.
Thresholds change only in a commit that explains the measured numbers.

## 5. Round-trip thresholds
| Case | Requirement |
| --- | --- |
| Pixel crop, same ICC/depth | bit-identical |
| Straightened (Lanczos-3), unmodified edit re-imported | frame bit-identical; inner region max abs error ≤ 1 LSB at the output depth (no resampling of the edit back: the composite samples the edit at the exact inverse) — measured value recorded |
| Standard size (scaled) | inner PSNR ≥ 45 dB vs. straightened reference |

## 6. Coverage map
`docs/acceptance-matrix.md` maps every PRD acceptance item to its test.
