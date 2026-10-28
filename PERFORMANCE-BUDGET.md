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

Not yet measured: macOS, Windows, 8 GB laptops, app idle/preview working set (Phase 3), batch drift (Phase 4).

## 4. Time targets
Set after the phase-2 baseline; recorded here with the measurement that justified them.
