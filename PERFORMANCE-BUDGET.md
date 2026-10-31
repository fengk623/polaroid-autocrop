# PERFORMANCE-BUDGET

Budgets are targets until a measurement row exists. Every measurement records device, OS, CPU, GPU,
RAM, input (size, depth, compression, strips/tiles), metric name, result, command and commit.
Only devices that appear in §3 are "measured"; nothing else is claimed.

## 1. Device tiers
| Tier | Example | Requirement |
| --- | --- | --- |
| Minimum | 8 GB RAM, Apple Silicon or Windows 10/11 x64, integrated GPU, SATA SSD | all workflows complete, incl. 1600 DPI A4 16-bit |
| Recommended | 16 GB RAM | time targets (set after baseline) |
| High | ≥ 32 GB | larger caches/concurrency; identical output |

## 2. Memory budgets (accounted buffer bytes and platform peak; both reported)
| Scenario | Budget |
| --- | --- |
| Idle after launch (app) | < 250 MB |
| Preview working set, one 1600 DPI scan open | < 1 GB |
| Detection, 1600 DPI A4 16-bit | < 1 GB |
| Export of one print, no rotation (streamed) | < 600 MB |
| Export with lossless 90° upright (buffered region) | < 1.2 GB |
| Largest single export, 16 GB device | < 4 GB |
| Largest single export, 8 GB device | < 2 GB |
| Batch item 100 vs item 1 (after completion) | within ±10 % |
| `mobile_lite` hard limit | 1.5 GB |

## 3. Measurements
| Date | Commit | Device / OS / CPU / RAM | Input | Scenario | Metric | Result | Command |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 2026-10-28 | phase-2-core | Cloud VM, Linux 6.18, 2 vCPU Intel Xeon 2.8 GHz, 7 GB RAM, virtio disk | 1200 DPI A4 16-bit RGB, LZW, 16-row strips, 10205×14031 (966 MB), 4 prints | detection (proxy + stage B) | time / peak RSS / accounted peak | 10.3 s / 344 MB / 197 MB | `fkcli perf <scan> --out-dir out --expected 4` |
| 2026-10-28 | phase-2-core | same | same | export full print, streamed pixel crop, 16-bit LZW TIFF (4 prints, 3484×4113 … 5256×4256) | time / accounted peak | 5.4–8.1 s / 278–325 MB | same |
| 2026-10-28 | phase-2-core | same | 1600 DPI A4 16-bit RGB, LZW, 256² tiles, 13606×18709 (1.68 GB), 4 prints | detection | time / peak RSS / accounted peak | 18.0 s / 322 MB / 192 MB | same |
| 2026-10-28 | phase-2-core | same | same | export full print, streamed (4 prints up to 7006×5674) | time / accounted peak / peak RSS | 7.0–12.0 s / 318–320 MB / 329 MB | same |
| 2026-10-28 | phase-2-core | same | same scene, every print a quarter turn | export full print with lossless upright (buffered region) | time / accounted peak / peak RSS | 9.2–15.7 s / 336–427 MB / 558 MB | `fkcli export <scan> --what print --upright` |
| 2026-10-28 | phase-2-core | same | detection corpus, 24 scans 300–400 DPI | detection per scan | median time | 1.1 s | `fkcli eval corpus` |

| 2026-10-31 | U017 (b96ab1e + a75d10a) | Cloud VM, Linux 6.18, 2 vCPU, 7.8 GB RAM (budget 502 MB) | fkfixture corpus, 300 DPI A4 8-bit RGB, deflate, 1/10/50/100 scans (3–4 prints each) | app batch: detect all → switch through all twice → 100 % view on each → composite previews of the prints of 10 scans → detection started and cancelled → steady | steady cache / app-held memory¹ / RSS / private | see §3.1 | `FK_BENCH_SCANS=N FrameKeeper --self-test DIR --scenario memory --scan CORPUS` |
| 2026-10-31 | U017 (a75d10a) | GitHub runners: macOS 15 arm64 (7 GB, budget 448 MB), Windows Server x64 (16 GB, budget 1024 MB) | same corpus kind, 5 and 20 scans | same batch | steady private bytes (macOS phys_footprint, Windows PrivateUsage) | macOS 95 MB (5) → 94 MB (20); Windows 383 → 930 MB with the cache still under its 1 GB budget (681 MB held) | CI step "Memory batch (FK-005)"; from the next run sized for 8 GB (`FK_ASSUME_RAM_GB=8`) |

¹ App-held memory = RSS after `malloc_trim(0)` (glibc only): what the app still holds once the allocator has
returned the pages it keeps after frees. RSS alone includes those pages (AGENTS §7, T22).

### 3.1 FK-005 batch, Linux VM (budget 502 MB)
| Scans | Prints | Steady cache | of it thumbnails | App-held memory¹ | Steady RSS | Peak RSS | Steady private | Accounted (steady / peak) | Evictions |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | 4 | 69 MB | 0.9 MB | 210 MB | 324 MB | 324 MB | 285 MB | 40 / 118 MB | 0 |
| 10 | 30 | 124 MB | 7.5 MB | 228 MB | 699 MB | 755 MB | 659 MB | 20 / 275 MB | 39 |
| 50 | 156 | 126 MB | 39.3 MB | 238 MB | 1078 MB | 1179 MB | 1038 MB | 9 / 186 MB | 282 |
| 100 | 311 | 126 MB | 78.4 MB | 290 MB | 875 MB | 1028 MB | 836 MB | 0 / 176 MB | 539 |
| 100, budget off (`FK_CACHE_BUDGET_MB=0`) | 311 | 2493 MB | 78.4 MB | 3388 MB | 3557 MB | 3557 MB | 3527 MB | 1422 / 1422 MB | 0 |
| 100, 16 GB budget (`FK_ASSUME_RAM_GB=16`, 1 GB; the VM has 8 GB) | 311 | 594 MB | 78.4 MB | 798 MB | 1651 MB | 2015 MB | 1612 MB | 223 / 452 MB | 481 |

Reading: the cache stays at its steady level whatever the number of scans (the scan on screen, a few recent proxies
and previews); the only part that grows with the batch is the thumbnails (≈ 0.8 MB per scan, kept for the scan list
and filmstrip until the library's disk cache, UX-05). glibc keeps freed pages, so the steady RSS follows the peak
of the run (≈ 1 GB here, at 50 scans as at 100), not the number of scans. Allocator figures per stage are in each
run's `scenario-memory.json` (`mallocInUseMB`, `mallocFreeMB`).

Not yet measured: real 8 GB / 16 GB laptops (HV), app idle/preview working set on macOS/Windows outside CI.

## 4. Time targets
Set after the phase-2 baseline; recorded here with the measurement that justified them.
