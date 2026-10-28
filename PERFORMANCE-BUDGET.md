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
| — | — | — | — | — | — | (no measurements yet) | — |

## 4. Time targets
Set after the phase-2 baseline; recorded here with the measurement that justified them.
