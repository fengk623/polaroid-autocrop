# TEST-PLAN

## 1. Layers
| Layer | Tool | Location | Runs in |
| --- | --- | --- | --- |
| Unit (core) | GoogleTest | `tests/core/` | every CI job (Linux, macOS, Windows) |
| Contract | GoogleTest + JSON fixtures | `tests/contract/` | every CI job |
| C ABI | C11 test | `tests/core/test_c_abi.c` | every CI job |
| Integration | `fkcli` scripts | `tests/integration/*.cmake` (CTest) | every CI job |
| Detection eval | `fkfixture` + `fkcli eval` | CTest label `eval` | every CI job (small corpus), nightly (large) |
| UI | Qt Quick Test, offscreen snapshots, `--self-test` | `tests/app/` | Linux + macOS + Windows CI |
| Real windows | manual / computer-use capture | `docs/HUMAN-VERIFICATION.md` | before release |
| Performance | `fkcli perf` | `tools/perf/` | nightly + before release |

CTest labels = minimal gates: `geometry`, `io`, `color`, `detect`, `export`, `relink`, `composite`,
`project`, `cache`, `jobs`, `contract`, `metadata`, `eval`, `app`, `perf`.

## 2. Gates
- While editing module X: `ctest -L X` (plus `contract` if an interface changed).
- Before merging to `main`: `tools/run-tests.sh` (all labels except `perf`) on Linux, and CI on all OSes.
- Bug fix: regression test committed red first (or shown red in the commit message with the command output), then fix.
- `tools/run-tests.sh` prints executed / skipped / failed / duration per label and exits non-zero on any
  skip not listed in `tests/allowed-skips.txt` (empty by default).

## 3. Fixtures
- Tiny committed fixtures: `tests/data/tiny/` (≤ 64 KB each; hand-made edge cases).
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
