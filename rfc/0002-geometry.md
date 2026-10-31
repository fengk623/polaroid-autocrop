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
| Straightened | quad: bounding box of the quad (+ support); ellipse: bounding box of the rotated region | quad: projective map of the quad's corners onto the output corners (U012, RFC-0005 §Exact straightening); ellipse: rotation by the frame angle (+ upright) — resampled |
| StandardSize | as Straightened | as Straightened, scaled to `standard` — resampled |

Frame angle = mean direction of the frame quad's long edges (robust to small non-parallelism).
No user-facing perspective correction in v1 (flatbed scans are affine; ADR-0014). The only projective map is the
quad image area's exact straightening, which absorbs the window's small shear (≈ 0.3°) so no frame remains;
`outToScan`/`scanToOut` are therefore general 3×3 matrices (stored as such in export records).

## Editing (UX-03, U013)
`std::optional<Shape> editShape(const Shape& start, const EditInput&)` is the only boundary-edit geometry: the
canvas previews its result during a drag and commits the same value; arrow-key nudges call it too. `EditInput`
= handle (`corner`, `edge`, `move`, `center`, `radiusX`, `radiusY`), quad index, pointer delta (scan px) and
`constrain` (Shift held).
- Corner, free: only that corner moves. Corner, constrained: the result is an exact rectangle with the start's
  rotation (mean of the p0→p1 and p3→p2 directions) and side ratio (`quadSideLengths`), scaled about the fixed
  opposite corner (unchanged bit for bit) so the dragged corner follows the pointer's projection on the diagonal;
  never smaller than `kMinEditSize` (4 px) on the short side and never flipped.
- Edge: both ends move along the edge's normal. Move/centre: translation; constrained = the dominant axis only.
- Ellipse radius handles sit at the ends of the rotated axes (`ellipseHandle`); the radius is the pointer's
  projection on that axis (≥ 4 px). A circle (`isCircle`: rx = ry within 1e-9 relative) changes only its radius
  (rx = ry exactly); a constrained ellipse scales both radii by the same factor.
- Invalid results (non-convex quad) return nullopt; the caller keeps the last valid shape.
- `transformShape` keeps a circle exactly round under maps that keep it round (rotation, uniform scale): the
  radii otherwise drift apart in the last bits after a translation + rotation.
Tests: `Edit.*` in `tests/core/test_geometry.cpp`.

## Quarter turns (UX-04, U014)
`quarterTurnMatrix(k, w, h)` maps a w×h image to its `k` clockwise quarter turns (`quarterTurnSize` swaps w and h
for odd k): integer pixel corners map to integer corners, four turns are the identity, the inverse is exact. It is
the export upright permutation and the canvas's whole-scan view mapping (scan → display, applied once, before
scale and offset). Tests: `Geometry.QuarterTurnsAreExactPermutations`.

## Containment
`maskWeight(shape, mode, x, y)` = 1/0 by pixel-centre containment (hard), or a linear ramp of width
`feather` on the inside distance; `Preserve edge` shrinks the shape by N px. Used by compositing,
overlays and tests.

## Single source of truth
Canvas overlays, navigator, thumbnails, export preview, export and composite call these functions
(no re-implementation in QML or app code). `tests/core/test_geometry_consistency.cpp` renders an
overlay rasterisation and an export of the same shape and compares extents.
