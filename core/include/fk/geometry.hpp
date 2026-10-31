// FrameKeeper core — geometry (RFC-0002). Single source of truth for every boundary,
// mapping and mask used by canvas overlays, thumbnails, export and compositing.
//
// Coordinates: pixel (x,y) covers [x,x+1)×[y,y+1); its centre is (x+0.5, y+0.5); y points down.
#pragma once

#include "fk/common.hpp"

#include <array>
#include <cmath>
#include <optional>
#include <vector>

namespace fk {

struct Vec2 {
    double x = 0, y = 0;
    Vec2() = default;
    constexpr Vec2(double x_, double y_) : x(x_), y(y_) {}
    Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(double s) const { return {x * s, y * s}; }
    Vec2 operator/(double s) const { return {x / s, y / s}; }
    friend bool operator==(const Vec2&, const Vec2&) = default;
};
inline double dot(Vec2 a, Vec2 b) {
    return a.x * b.x + a.y * b.y;
}
inline double cross(Vec2 a, Vec2 b) {
    return a.x * b.y - a.y * b.x;
}
inline double norm(Vec2 a) {
    return std::sqrt(dot(a, a));
}
inline Vec2 normalized(Vec2 a) {
    const double n = norm(a);
    return n > 0 ? a / n : Vec2{0, 0};
}
inline Vec2 perp(Vec2 a) {
    return {-a.y, a.x};
} // rotate +90° (clockwise on screen, y down)

/// Corner order: clockwise on screen (y down), starting at the corner with the smallest x+y.
struct Quad {
    std::array<Vec2, 4> p{};
    friend bool operator==(const Quad&, const Quad&) = default;
};

struct Ellipse {
    Vec2 c{};
    double rx = 0, ry = 0;
    double angleRad = 0; ///< rotation of the rx axis, clockwise on screen
    friend bool operator==(const Ellipse&, const Ellipse&) = default;
};

enum class ShapeKind : int { quad = 0, ellipse = 1 };

struct Shape {
    ShapeKind kind = ShapeKind::quad;
    Quad quad{};
    Ellipse ellipse{};
    static Shape fromQuad(const Quad& q) { return Shape{ShapeKind::quad, q, {}}; }
    static Shape fromEllipse(const Ellipse& e) { return Shape{ShapeKind::ellipse, {}, e}; }
    friend bool operator==(const Shape&, const Shape&) = default;
};

struct RectD {
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    double width() const { return x1 - x0; }
    double height() const { return y1 - y0; }
};

/// Row-major 3×3 homogeneous matrix.
struct Mat3 {
    std::array<double, 9> m{1, 0, 0, 0, 1, 0, 0, 0, 1};
    static Mat3 identity() { return {}; }
    static Mat3 translate(double tx, double ty) { return Mat3{{1, 0, tx, 0, 1, ty, 0, 0, 1}}; }
    static Mat3 scale(double sx, double sy) { return Mat3{{sx, 0, 0, 0, sy, 0, 0, 0, 1}}; }
    /// Rotation by `a` radians, clockwise on screen (y down).
    static Mat3 rotate(double a) {
        const double c = std::cos(a), s = std::sin(a);
        return Mat3{{c, -s, 0, s, c, 0, 0, 0, 1}};
    }
    Mat3 operator*(const Mat3& o) const;
    Vec2 apply(Vec2 v) const;
    std::optional<Mat3> inverse() const;
    bool isAffine() const { return m[6] == 0 && m[7] == 0 && m[8] == 1; }
    friend bool operator==(const Mat3&, const Mat3&) = default;
};

// ---- shape helpers -------------------------------------------------------------------------
Quad normalizeQuad(const std::array<Vec2, 4>& corners);
bool isValid(const Shape& s);
bool isConvex(const Quad& q);
RectD boundsOf(const Shape& s);
/// Smallest integer rectangle containing the shape (floor/ceil), optionally clamped to `clampTo`.
RectI boundingRect(const Shape& s, std::optional<SizeI> clampTo = std::nullopt);
bool contains(const Shape& s, Vec2 p);
/// Signed distance in px, positive inside (exact for quads and circles, first-order for ellipses).
double signedDistance(const Shape& s, Vec2 p);
/// Moves every edge outward by `px` (negative = inward).
Shape expandShape(const Shape& s, double px);
/// Affine maps only. A circle stays an exact circle under maps that keep it round (rotation, uniform scale).
Shape transformShape(const Shape& s, const Mat3& m);
double area(const Shape& s);
Vec2 centroid(const Shape& s);
/// Tilt of the frame in radians, clockwise positive, in (-π/4, π/4].
double frameAngle(const Quad& q);
/// Average side lengths: {width (p0→p1, p3→p2), height (p1→p2, p0→p3)}.
std::array<double, 2> quadSideLengths(const Quad& q);
Quad rectQuad(double x0, double y0, double x1, double y1);

// ---- boundary editing (RFC-0002 §Editing, UX-03) ------------------------------------------
/// Handles the canvas drags. Quads: corner i (p[i]), edge i (p[i]→p[i+1]), move. Ellipses: centre, the end of
/// the rotated rx axis (radiusX), of the ry axis (radiusY), move.
enum class EditHandle : int { corner = 0, edge = 1, move = 2, center = 3, radiusX = 4, radiusY = 5 };
struct EditInput {
    EditHandle handle = EditHandle::move;
    int index = 0;          ///< corner / edge index of a quad
    Vec2 delta{};           ///< pointer movement since the press, scan px
    bool constrain = false; ///< Shift held: a corner keeps the rectangle's aspect ratio, right angles and
                            ///< rotation about the opposite corner; move/centre lock to the dominant axis;
                            ///< an ellipse's radii scale together
};
/// Smallest side or radius an edit leaves (scan px).
inline constexpr double kMinEditSize = 4.0;
/// The shape after dragging a handle of `start` by `in.delta`, or nullopt when the result would be invalid
/// (a quad that is not convex). Without `constrain` a corner moves freely and an edge moves along its normal.
/// Circles (rx = ry) stay circles; radius handles act along the rotated axes.
std::optional<Shape> editShape(const Shape& start, const EditInput& in);
/// Position of an ellipse's radius handle: the end of the rotated rx (`xAxis`) or ry axis.
Vec2 ellipseHandle(const Ellipse& e, bool xAxis);
/// rx = ry up to floating-point noise (relative 1e-9): edited as a circle, kept a circle by transforms.
bool isCircle(const Ellipse& e);

// ---- lines ---------------------------------------------------------------------------------
struct Line {
    Vec2 p{}; ///< a point on the line
    Vec2 d{}; ///< unit direction
};
std::optional<Vec2> intersect(const Line& a, const Line& b);
double signedDistance(const Line& l, Vec2 q); ///< positive on the side of perp(d)

// ---- export geometry -----------------------------------------------------------------------
enum class GeometryMode : int { pixel_crop = 0, straightened = 1, standard_size = 2 };
const char* geometryModeName(GeometryMode m);

struct ExportGeometry {
    SizeI outSize{};
    Mat3 outToScan{};
    Mat3 scanToOut{};
    RectI sourceRect{}; ///< scan pixels that must be read (includes kernel support when resampled)
    bool resampled = false;
    int upright = 0; ///< 90° clockwise turns applied to the output
    GeometryMode mode = GeometryMode::pixel_crop;
    double angleRad = 0;
    double scale = 1;
};

/// Kernel support (pixels) added around resampled source rectangles (Lanczos-3).
inline constexpr int kResampleSupport = 4;

ExportGeometry computeExportGeometry(const Shape& region, const Quad& frame, GeometryMode mode, int upright,
                                     std::optional<SizeI> standard, SizeI scan);

// ---- masks ---------------------------------------------------------------------------------
enum class MaskMode : int { hard_edge = 0, feather = 1, preserve_edge = 2 };
const char* maskModeName(MaskMode m);

struct MaskParams {
    MaskMode mode = MaskMode::hard_edge;
    double featherPx = 0;      ///< ramp width inside the shape (0–5)
    double preserveEdgePx = 0; ///< shrink before masking
};

/// Weight in [0,1] with which the graded image replaces the scan at scan position (x,y).
/// Never > 0 outside the shape: frame pixels are never touched.
double maskWeight(const Shape& s, const MaskParams& p, double x, double y);

} // namespace fk
