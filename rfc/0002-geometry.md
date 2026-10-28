# RFC-0002 — Geometry

Status: Accepted · Module: `core/geometry` · Tests: `tests/core/test_geometry.cpp`

## Conventions (normative)
- Scan coordinates: continuous, origin at the top-left corner of pixel (0,0); pixel (x,y) covers
  [x, x+1) × [y, y+1); its centre is (x+0.5, y+0.5). Double precision.
- Quad corner order: as seen in the scan, starting at the corner nearest the scan's top-left, clockwise
  (y down): p0, p1, p2, p3. Degenerate or self-intersecting quads are invalid.
- Shapes: `Quad`, `Ellipse{centre, rx, ry, angle}` (circle when rx = ry), wrapped in `Shape{kind,…}`.
- Matrices: `Mat3` row-major 3×3 homogeneous; affine has last row (0,0,1).
- Orientation: `upright ∈ {0,1,2,3}` = number of 90° clockwise turns that put the widest border at the bottom.

## Export geometry
`ExportGeometry computeExportGeometry(const Shape& region, const Quad& frameQuad, GeometryMode, int upright, std::optional<SizeI> standard, const SizeI& scanSize)`
returns output size, `outToScan` and `scanToOut` matrices, the integer source rectangle that must be read,
and `resampled`.

| Mode | Source rect | Mapping |
| --- | --- | --- |
| PixelCrop | floor/ceil bounding box of the shape, clamped to the scan | translation (+ 90° permutation if upright ≠ 0) — exact |
| Straightened | bounding box of the rotated region | rotation by the frame angle (+ upright) — resampled |
| StandardSize | as Straightened | rotation + uniform scale to `standard` — resampled |

Frame angle = mean direction of the frame quad's long edges (robust to small non-parallelism).
Perspective correction is not offered in v1 (flatbed scans are affine; ADR-0014).

## Containment
`maskWeight(shape, mode, x, y)` = 1/0 by pixel-centre containment (hard), or a linear ramp of width
`feather` on the inside distance; `Preserve edge` shrinks the shape by N px. Used by compositing,
overlays and tests.

## Single source of truth
Canvas overlays, navigator, thumbnails, export preview, export and composite call these functions
(no re-implementation in QML or app code). `tests/core/test_geometry_consistency.cpp` renders an
overlay rasterisation and an export of the same shape and compares extents.
