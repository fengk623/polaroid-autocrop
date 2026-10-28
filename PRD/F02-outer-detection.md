# F02 — Outer (paper) detection

RFC: `rfc/0004-detection.md`, `rfc/0002-geometry.md` · Tests: `tests/core/test_detection_*.cpp`, `fkcli eval`

## Problem
Find every print in a scan and its outermost physical edge — frame, wide bottom border, texture,
ageing, stains, handwriting, paper edge — never just the picture.

## Rules (priority order)
1. Never cut into the paper (hard constraint).
2. Subject to 1, fit the real edge as tightly as possible so the default export contains almost no
   scanner background.
3. When unsure, expand outward; flag low confidence.

## Scope
- Expected count 1–12 (default 4); actual count reported; mismatch is a warning, never forced.
- Mixed film types in one scan (Polaroid 600/SX-70/i-Type, round frame, Instax Mini/Square/Wide, white,
  cream, gold, black and coloured frames).
- Two stages: proxy detection (long side ≈3000 px) then full-resolution refinement of each edge in narrow
  bands read directly from the TIFF.
- Output per print: quad in full-resolution scan coordinates, corner radius (Instax), film-type guess,
  confidence 0–1, reasons, refinement offsets.
- Ordering: reading order (top-left → top-right → bottom-left → bottom-right) with tolerance for tilt; user can reorder.
- Low-contrast case (black frame on black background): low confidence + advice to rescan on a light background.

## Acceptance
- A1 Synthetic corpus (≥ 200 prints, 300–1600 DPI, all film types and frame colours, ±5° tilt, dust,
  scratches, glare, shadows, uneven background): `inner_cut_violations == 0`. (`fkcli eval`, CTest label `eval`, run by every CI core job)
- A2 Same corpus: outward slack median ≤ 3 px and P95 ≤ 8 px at full resolution (threshold fixed after first measurement; see TEST-PLAN.md).
- A3 Count accuracy ≥ 99 % on the corpus; every miscount is flagged in warnings.
- A4 Black-frame-on-black fixture yields confidence < 0.6 and the rescan advice.
- A5 Detection of a 1600 DPI A4 16-bit scan runs with peak memory < 1 GB. (`PERFORMANCE-BUDGET.md`)
- A6 Real Epson V750 scans: pending human verification (`docs/HUMAN-VERIFICATION.md`).
