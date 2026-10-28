# F03 — Inner (image area) detection

RFC: `rfc/0004-detection.md` · Tests: `tests/core/test_detection_inner.cpp`, `fkcli eval`

## Problem
Find the picture inside each print so it can be graded alone and later put back.

## Scope
- Shapes: `quad` (square/rectangular image areas) and `ellipse` (round-frame film; a circle is an ellipse with equal axes).
- Uses the film-type library (`core/detection/film_types.json`) only as a prior; measured edges win.
- Handles frames that are white, cream, gold, black or coloured; handwriting and stains on the frame;
  images that are almost black or almost white at the edge (falls back to the prior with low confidence).
- Detects print orientation (which side has the widest border) so exports can be turned upright losslessly.
- Full-resolution refinement of each inner edge (same banded reader as F02).

## Acceptance
- A1 Synthetic corpus: inner edge absolute error median ≤ 3 px, P95 ≤ 10 px at full resolution (fixed after first measurement).
- A2 Round-frame fixtures produce an `ellipse` shape with centre error ≤ 3 px and radius error ≤ 3 px.
- A3 Near-white and near-black image fixtures fall back to the prior and report confidence < 0.6.
- A4 Orientation correct on ≥ 99 % of corpus prints with a wide bottom border.
