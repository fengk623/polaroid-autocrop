# ADR-0014 — Resampling kernel

Status: Accepted · Date: 2026-10-28

## Decision
Straightening and scaling use a 2-D Lanczos-3 kernel (6×6 taps, weights normalised per pixel, result
clamped to the sample range, computed in float, rounded to the output depth). Downscaling by factor s > 1
widens the kernel by s (anti-aliasing). Proxy generation (detection, display) uses an exact box filter.
Pixel crop and 90° rotations never resample.

## Consequences
Slight ringing at hard edges is possible (bounded by clamping); straightening is always labelled "resampled".
