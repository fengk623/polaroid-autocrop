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

## 4. Detection thresholds (enforced in CI on the seed-1 corpus)
Initial values (to be tightened after measurements; changes require a commit explaining the measured numbers):
| Metric | Threshold |
| --- | --- |
| inner_cut_violations | 0 |
| outer slack median / P95 / max | ≤ 3 px / ≤ 8 px / ≤ 20 px |
| inner error median / P95 | ≤ 3 px / ≤ 10 px |
| count accuracy | 100 % on the CI corpus |
| orientation accuracy | ≥ 99 % |
| low-confidence recall | ≥ 90 % |

## 5. Round-trip thresholds
| Case | Requirement |
| --- | --- |
| Pixel crop, same ICC/depth | bit-identical |
| Straightened (Lanczos-3), unmodified edit re-imported | frame bit-identical; inner region max abs error ≤ 1 LSB at the output depth (no resampling of the edit back: the composite samples the edit at the exact inverse) — measured value recorded |
| Standard size (scaled) | inner PSNR ≥ 45 dB vs. straightened reference |

## 6. Coverage map
`docs/acceptance-matrix.md` maps every PRD acceptance item to its test.
