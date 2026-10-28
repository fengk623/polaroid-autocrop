# AGENTS.md — conventions, rules and traps

This file is the single source of agent/contributor conventions. `CLAUDE.md` only points here.
Read it, `docs/PROGRESS.md` and `ARCHITECTURE.md` before changing anything; after a context reset, also
read the last 20 entries of `git log`.

## 1. Orientation
| Path | What |
| --- | --- |
| `core/` | C++20 library `fkcore` — no Qt, no OS UI APIs. All product logic. |
| `core/include/fk/` | Public API (normative spec: `API-SPEC-core.md`). |
| `app/` | Qt 6 Qt Quick desktop app (`FrameKeeper`). Calls only the core API. |
| `platform/` | Per-OS adapters used by `app/` (display ICC, memory pressure, reveal in Finder/Explorer). |
| `tools/cli` | `fkcli` — same core, no UI; integration tests and measurements. |
| `tools/fixtures` | `fkfixture` — deterministic synthetic scans + ground truth. |
| `tests/` | GoogleTest (`tests/core`), contract tests (`tests/contract`), QML tests (`tests/app`). |
| `PRD/`, `rfc/`, `docs/` | Requirements (one per feature), RFC per module + ADRs, other specs. |
| `cmake/deps` | Pinned dependency superbuild. `cmake/ProductInfo.cmake` = names, ids, version source. |

## 2. Build and test
```
cmake -S cmake/deps -B build/deps -G Ninja -DFK_DEPS_PREFIX=$PWD/build/deps-install && cmake --build build/deps
cmake -S . -B build/dev -G Ninja -DCMAKE_PREFIX_PATH="$PWD/build/deps-install;<Qt>/lib/cmake" -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/dev
ctest --test-dir build/dev --output-on-failure            # full suite
ctest --test-dir build/dev -L geometry                    # minimal gate example
tools/run-tests.sh build/dev                               # full suite + executed/skipped/duration report
```
- Every worktree uses its own `build-<name>/` directory. Never share a build dir between worktrees.
- `FK_BUILD_APP=OFF` builds core + CLI + tests without Qt.

## 3. Process (non-negotiable)
1. **New feature → PRD first** (`PRD/Fxx-*.md`) with testable acceptance criteria.
2. **New module → RFC first** (`rfc/NNNN-*.md`, status Accepted before code). Hard-to-reverse choice → ADR.
3. **Automated tests with the code**: every acceptance criterion maps to a test (`docs/acceptance-matrix.md`).
4. **CI green on macOS, Windows and Linux before merging to `main`.** Red CI → fix first, no new feature.
5. **Interface change** → API-SPEC, CONTRACT, serialisation, compatibility test, migration note, RFC in the *same commit*.
6. **Bug** → regression test that fails first (show it in the commit message), then the fix, then green.
7. **Gate record**: commit messages state the behaviour change, how it was verified and which gate ran.
8. **Claims need evidence**: every speed/memory/quality statement is measured (device, OS, CPU, RAM, input) and
   written to `PERFORMANCE-BUDGET.md` or a test. Untested devices are never "verified".
9. ARCHITECTURE.md describes what *is* implemented; planned work is marked as planned.
10. Update `docs/PROGRESS.md` after every coherent unit of work.

### Definition of Done
PRD updated · RFC updated · tests added (red→green for fixes) · CI green on all OSes · specs in sync ·
PROGRESS.md updated · no visible control without an implementation.

## 4. Rules that must never break
- Source scans are opened read-only and are byte-identical after any operation (`test_source_untouched`).
- The outer boundary never cuts into the paper; refinement enforces containment by construction.
- The real scanned frame is never replaced by generated pixels in archival mode.
- 16-bit never silently becomes 8-bit; ICC never silently assigned or dropped; one conversion at most.
- Preview, canvas, thumbnails, export and composite use `core/geometry` only.
- Exports never overwrite; writes are temp → flush → rename.
- No network code. No telemetry. Logs carry no pixels.
- No Qt in `core/`.

## 5. Traps (keep this list current; add every trap that cost real time)
| # | Trap | Correct approach |
| --- | --- | --- |
| T1 | A green test run that silently skipped fixture-dependent cases. | `tools/run-tests.sh` prints executed/skipped/duration and fails on unexpected skips. Read the counts, not just "0 failures". |
| T2 | Offscreen snapshot looks right but the real window draws nothing (RHI backend, surface format). | Real-window captures are separate evidence (`docs/HUMAN-VERIFICATION.md`). |
| T3 | Assign vs convert ICC confusion. | API has two functions; logs name the operation. |
| T4 | libtiff `TIFFReadScanline` going backwards restarts decoding of a huge strip. | Read rows in increasing order; batch edge bands in one pass. |
| T5 | Qt host build: `-DFEATURE_opengl=OFF` is not enough without GL headers. | Use `-DINPUT_opengl=no`. |
| T6 | apt/pip/npm are blocked in the cloud dev sandbox; GitHub git clone and release downloads work. | Build deps from pinned GitHub tags (superbuild). |
| T7 | `zlib` CMake builds a shared lib as well as static; static link may pick `.so`. | Link `ZLIB::ZLIB` via `ZLIB_USE_STATIC_LIBS=ON` (CMake ≥ 3.24) or the explicit static path. |
| T8 | Tests that write next to fixtures pollute them. | Tests copy fixtures into a fresh temp dir (`fk::test::TempDir`). |
| T9 | Shared app settings leak between tests and the user's real settings. | Tests set `FK_SETTINGS_DIR` to a temp dir; the app honours it. |
| T10 | libtiff cannot seek forward inside a compressed strip ("does not support random access"): a single-strip LZW/Deflate scan read out of order fails. | `TiffSource` decodes sequentially from the strip start (scanline path) and counts restarts; regression test `TiffRead.SingleStripCompressedRandomRowOrder`. |
| T11 | LittleCMS optimised 16-bit transforms are off by thousands of codes near black. | `cmsFLAGS_NOOPTIMIZE` for ≥ 16-bit transforms (color.cpp). |
| T12 | Loose card-size tolerances accept two touching prints as one bigger film type (2 × mini ≈ wide). | Card test uses ≤ 2.5 mm + 1 % when the DPI is known (RFC-0004 §A7); test `Detection.TouchingPrintsAreSplitContainedAndFlagged`. |
| T13 | Scanner shadows (~1 mm) along print edges and dust on the glass pull edges outward; windows centred on an inflated proxy edge never reach real paper. | Outer profiles reach 2 mm further inside; crossings start after the last background run and must be paper coloured (RFC-0004 stage B). |
| T14 | Test scenes that overlap prints are physically impossible and make detection look broken. | Place test cards with explicit mm spacing (`test_detection.cpp`); keep files with `FK_KEEP_TEST_FILES=1` to inspect a failing scene. |
| T15 | Real scans differ from synthetic ones: prints cropped tightly (no background at all), output scaled 103 % with the nominal DPI tag, soft lid glow on white backgrounds, real borders ±2 mm from nominal. | Single-print path, effective-DPI estimate, wide inner search (RFC-0004 A9–A10); tests `Detection.SinglePrintCroppedTightlyInTheScanner`, `Detection.ScaledOutputWithNominalDpiTag`. Re-check on real scans (HV-01) after any detection change. |

## 6. Platform differences
- Windows paths: always `std::filesystem::path` + wide APIs; never `fopen(path.string())`.
- macOS display: Qt windows are sRGB-tagged; convert to sRGB. Windows: convert to the monitor profile.
- Line endings: `.gitattributes` keeps LF for sources; Windows CI checks out with `core.autocrlf=false`.

## 7. Verification that can lie
- Single-strip test TIFFs exercise only one reader path; fixtures cover tiles, small strips and huge strips.
- A detection eval on prints that are all axis-aligned hides rotation bugs; the corpus includes ±5°.
- Measuring RSS alone hides mapped-file pages and allocator caches; record accounted buffer bytes too.
