# ADR-0008 — GPU usage

Status: Accepted · Date: 2026-10-28

## Decision
v1 does all pixel processing on the CPU (the reference path). The GPU is used only by Qt Quick's scene
graph (RHI: Metal on macOS, D3D11 on Windows) to display already-converted 8-bit tiles. No compute
shaders. Any future GPU acceleration must ship with a CPU/GPU equivalence test and a CPU fallback.

## Consequences
Correctness never depends on drivers. GPU memory = display textures only, accounted per tile in diagnostics.
