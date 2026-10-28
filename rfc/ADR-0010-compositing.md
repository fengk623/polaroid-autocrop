# ADR-0010 — Frame/image compositing

Status: Accepted · Date: 2026-10-28

## Decision
Composite in the scan's own space and bit depth (16-bit integer path for 16-bit sources). For every output
pixel: compute its scan position (output→scan matrix); if the Inner mask weight w > 0, sample the graded
image at scan→edit(position) and blend `out = w·edit + (1−w)·scan`, else copy the scan pixel. Hard-edge
mask: w ∈ {0,1} by pixel-centre containment. In crop mode the scan→edit map is an integer translation (plus
optional 90° permutation), so sampling is an exact copy and the unmodified round trip is bit-exact.
The frame is always read from the original scan, never from caches or previews.

## Consequences
No resampling happens in crop mode; straightened mode samples the graded image once (Lanczos-3).
