#include "fk/geometry.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace fk {

namespace {
constexpr double kPi = std::numbers::pi;

double distPointSegment(Vec2 q, Vec2 a, Vec2 b) {
    const Vec2 ab = b - a;
    const double len2 = dot(ab, ab);
    double t = len2 > 0 ? dot(q - a, ab) / len2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    return norm(q - (a + ab * t));
}

Vec2 ellipseLocal(const Ellipse& e, Vec2 p) {
    const double c = std::cos(-e.angleRad), s = std::sin(-e.angleRad);
    const Vec2 d = p - e.c;
    return {c * d.x - s * d.y, s * d.x + c * d.y};
}

Mat3 uprightMatrix(int k, double w, double h) {
    switch (k & 3) {
    case 1:
        return Mat3{{0, -1, h, 1, 0, 0, 0, 0, 1}};
    case 2:
        return Mat3{{-1, 0, w, 0, -1, h, 0, 0, 1}};
    case 3:
        return Mat3{{0, 1, 0, -1, 0, w, 0, 0, 1}};
    default:
        return Mat3::identity();
    }
}
} // namespace

Mat3 Mat3::operator*(const Mat3& o) const {
    Mat3 r;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            double s = 0;
            for (int k = 0; k < 3; ++k)
                s += m[i * 3 + k] * o.m[k * 3 + j];
            r.m[i * 3 + j] = s;
        }
    return r;
}

Vec2 Mat3::apply(Vec2 v) const {
    const double x = m[0] * v.x + m[1] * v.y + m[2];
    const double y = m[3] * v.x + m[4] * v.y + m[5];
    const double w = m[6] * v.x + m[7] * v.y + m[8];
    if (w != 1.0 && w != 0.0) return {x / w, y / w};
    return {x, y};
}

std::optional<Mat3> Mat3::inverse() const {
    const auto& a = m;
    const double det = a[0] * (a[4] * a[8] - a[5] * a[7]) - a[1] * (a[3] * a[8] - a[5] * a[6]) +
                       a[2] * (a[3] * a[7] - a[4] * a[6]);
    if (std::abs(det) < 1e-300) return std::nullopt;
    const double id = 1.0 / det;
    Mat3 r;
    r.m = {
        (a[4] * a[8] - a[5] * a[7]) * id, (a[2] * a[7] - a[1] * a[8]) * id, (a[1] * a[5] - a[2] * a[4]) * id,
        (a[5] * a[6] - a[3] * a[8]) * id, (a[0] * a[8] - a[2] * a[6]) * id, (a[2] * a[3] - a[0] * a[5]) * id,
        (a[3] * a[7] - a[4] * a[6]) * id, (a[1] * a[6] - a[0] * a[7]) * id, (a[0] * a[4] - a[1] * a[3]) * id};
    return r;
}

Quad normalizeQuad(const std::array<Vec2, 4>& corners) {
    Vec2 c{0, 0};
    for (auto& p : corners)
        c = c + p;
    c = c / 4.0;
    std::array<Vec2, 4> s = corners;
    std::sort(s.begin(), s.end(), [&](Vec2 a, Vec2 b) {
        return std::atan2(a.y - c.y, a.x - c.x) < std::atan2(b.y - c.y, b.x - c.x);
    });
    int start = 0;
    for (int i = 1; i < 4; ++i)
        if (s[i].x + s[i].y < s[start].x + s[start].y) start = i;
    Quad q;
    for (int i = 0; i < 4; ++i)
        q.p[i] = s[(start + i) % 4];
    return q;
}

bool isConvex(const Quad& q) {
    for (int i = 0; i < 4; ++i) {
        const Vec2 e0 = q.p[(i + 1) % 4] - q.p[i];
        const Vec2 e1 = q.p[(i + 2) % 4] - q.p[(i + 1) % 4];
        if (cross(e0, e1) <= 0) return false;
    }
    return true;
}

bool isValid(const Shape& s) {
    if (s.kind == ShapeKind::quad) {
        for (auto& p : s.quad.p)
            if (!std::isfinite(p.x) || !std::isfinite(p.y)) return false;
        return isConvex(s.quad) && area(s) > 1.0;
    }
    return std::isfinite(s.ellipse.c.x) && std::isfinite(s.ellipse.c.y) && s.ellipse.rx > 0.5 &&
           s.ellipse.ry > 0.5;
}

RectD boundsOf(const Shape& s) {
    if (s.kind == ShapeKind::quad) {
        RectD r{std::numeric_limits<double>::max(), std::numeric_limits<double>::max(),
                std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest()};
        for (auto& p : s.quad.p) {
            r.x0 = std::min(r.x0, p.x);
            r.y0 = std::min(r.y0, p.y);
            r.x1 = std::max(r.x1, p.x);
            r.y1 = std::max(r.y1, p.y);
        }
        return r;
    }
    const auto& e = s.ellipse;
    const double c = std::cos(e.angleRad), sn = std::sin(e.angleRad);
    const double hx = std::sqrt(e.rx * e.rx * c * c + e.ry * e.ry * sn * sn);
    const double hy = std::sqrt(e.rx * e.rx * sn * sn + e.ry * e.ry * c * c);
    return {e.c.x - hx, e.c.y - hy, e.c.x + hx, e.c.y + hy};
}

RectI boundingRect(const Shape& s, std::optional<SizeI> clampTo) {
    const RectD b = boundsOf(s);
    int64_t x0 = static_cast<int64_t>(std::floor(b.x0 + 1e-9));
    int64_t y0 = static_cast<int64_t>(std::floor(b.y0 + 1e-9));
    int64_t x1 = static_cast<int64_t>(std::ceil(b.x1 - 1e-9));
    int64_t y1 = static_cast<int64_t>(std::ceil(b.y1 - 1e-9));
    if (clampTo) {
        x0 = std::clamp<int64_t>(x0, 0, clampTo->w);
        y0 = std::clamp<int64_t>(y0, 0, clampTo->h);
        x1 = std::clamp<int64_t>(x1, 0, clampTo->w);
        y1 = std::clamp<int64_t>(y1, 0, clampTo->h);
    }
    return {x0, y0, std::max<int64_t>(0, x1 - x0), std::max<int64_t>(0, y1 - y0)};
}

bool contains(const Shape& s, Vec2 p) {
    if (s.kind == ShapeKind::quad) {
        for (int i = 0; i < 4; ++i) {
            const Vec2 a = s.quad.p[i], b = s.quad.p[(i + 1) % 4];
            if (cross(b - a, p - a) < 0) return false;
        }
        return true;
    }
    const Vec2 l = ellipseLocal(s.ellipse, p);
    const double f =
        (l.x * l.x) / (s.ellipse.rx * s.ellipse.rx) + (l.y * l.y) / (s.ellipse.ry * s.ellipse.ry);
    return f <= 1.0;
}

double signedDistance(const Shape& s, Vec2 p) {
    if (s.kind == ShapeKind::quad) {
        bool inside = true;
        double dmin = std::numeric_limits<double>::max();
        for (int i = 0; i < 4; ++i) {
            const Vec2 a = s.quad.p[i], b = s.quad.p[(i + 1) % 4];
            if (cross(b - a, p - a) < 0) inside = false;
            dmin = std::min(dmin, distPointSegment(p, a, b));
        }
        return inside ? dmin : -dmin;
    }
    const auto& e = s.ellipse;
    const Vec2 l = ellipseLocal(e, p);
    if (std::abs(e.rx - e.ry) < 1e-9) return e.rx - norm(l);
    const double g = std::sqrt((l.x * l.x) / (e.rx * e.rx) + (l.y * l.y) / (e.ry * e.ry));
    if (g < 1e-12) return std::min(e.rx, e.ry);
    const Vec2 grad{l.x / (e.rx * e.rx * g), l.y / (e.ry * e.ry * g)};
    const double gn = norm(grad);
    return gn > 0 ? (1.0 - g) / gn : 0.0;
}

std::optional<Vec2> intersect(const Line& a, const Line& b) {
    const double den = cross(a.d, b.d);
    if (std::abs(den) < 1e-12) return std::nullopt;
    const double t = cross(b.p - a.p, b.d) / den;
    return a.p + a.d * t;
}

double signedDistance(const Line& l, Vec2 q) {
    return dot(q - l.p, perp(l.d));
}

Shape expandShape(const Shape& s, double px) {
    if (s.kind == ShapeKind::ellipse) {
        Shape r = s;
        r.ellipse.rx = std::max(0.0, s.ellipse.rx + px);
        r.ellipse.ry = std::max(0.0, s.ellipse.ry + px);
        return r;
    }
    std::array<Line, 4> lines;
    for (int i = 0; i < 4; ++i) {
        const Vec2 a = s.quad.p[i], b = s.quad.p[(i + 1) % 4];
        const Vec2 d = normalized(b - a);
        lines[i] = Line{a - perp(d) * px, d}; // perp(d) points inside for clockwise-on-screen quads
    }
    Quad q;
    for (int i = 0; i < 4; ++i) {
        auto v = intersect(lines[(i + 3) % 4], lines[i]);
        q.p[i] = v ? *v : s.quad.p[i];
    }
    return Shape::fromQuad(q);
}

Shape transformShape(const Shape& s, const Mat3& m) {
    if (s.kind == ShapeKind::quad) {
        std::array<Vec2, 4> c;
        for (int i = 0; i < 4; ++i)
            c[i] = m.apply(s.quad.p[i]);
        return Shape::fromQuad(normalizeQuad(c));
    }
    const auto& e = s.ellipse;
    const Vec2 o = m.apply({0, 0});
    const double ca = std::cos(e.angleRad), sa = std::sin(e.angleRad);
    const Vec2 ax = m.apply(Vec2{ca * e.rx, sa * e.rx}) - o;
    const Vec2 ay = m.apply(Vec2{-sa * e.ry, ca * e.ry}) - o;
    Ellipse r;
    r.c = m.apply(e.c);
    r.rx = norm(ax);
    r.ry = norm(ay);
    r.angleRad = std::atan2(ax.y, ax.x);
    // the two radii go through different arithmetic: a circle mapped by a rotation would drift apart by
    // rounding (UX-03: "a circle must not turn into an ellipse")
    if (isCircle(e) && isCircle(r)) r.ry = r.rx;
    return Shape::fromEllipse(r);
}

bool isCircle(const Ellipse& e) {
    return std::abs(e.rx - e.ry) <= 1e-9 * std::max(std::abs(e.rx), std::abs(e.ry));
}

Vec2 ellipseHandle(const Ellipse& e, bool xAxis) {
    const Vec2 ax{std::cos(e.angleRad), std::sin(e.angleRad)};
    return xAxis ? e.c + ax * e.rx : e.c + perp(ax) * e.ry;
}

namespace {

Vec2 axisLocked(Vec2 d) {
    return std::abs(d.x) >= std::abs(d.y) ? Vec2{d.x, 0} : Vec2{0, d.y};
}

/// Shift-drag of corner i: the result is an exact rectangle with the start's mean rotation and side ratio,
/// scaled about the fixed opposite corner so the dragged corner follows the pointer's projection on the
/// diagonal.
Quad constrainedCorner(const Quad& q, int i, Vec2 delta) {
    const int f = (i + 2) % 4;
    const Vec2 u = normalized((q.p[1] - q.p[0]) + (q.p[2] - q.p[3]));
    Vec2 v = perp(u);
    if (dot(v, (q.p[3] - q.p[0]) + (q.p[2] - q.p[1])) < 0) v = v * -1.0;
    const auto [w0, h0] = quadSideLengths(q);
    const std::array<Vec2, 4> local{Vec2{0, 0}, Vec2{w0, 0}, Vec2{w0, h0}, Vec2{0, h0}};
    auto toScan = [&](int k, double s) {
        const Vec2 l = local[k] - local[f];
        return q.p[f] + (u * l.x + v * l.y) * s;
    };
    const Vec2 diag = toScan(i, 1.0) - q.p[f];
    const Vec2 pointer = q.p[i] + delta;
    double s = dot(pointer - q.p[f], diag) / dot(diag, diag);
    s = std::max(s, kMinEditSize / std::max(1e-9, std::min(w0, h0)));
    Quad r;
    for (int k = 0; k < 4; ++k)
        r.p[k] = k == f ? q.p[f] : toScan(k, s);
    return r;
}

} // namespace

std::optional<Shape> editShape(const Shape& start, const EditInput& in) {
    Shape s = start;
    const Vec2 d = in.constrain && (in.handle == EditHandle::move || in.handle == EditHandle::center)
                       ? axisLocked(in.delta)
                       : in.delta;
    if (s.kind == ShapeKind::quad) {
        Quad& q = s.quad;
        const int i = ((in.index % 4) + 4) % 4;
        switch (in.handle) {
        case EditHandle::corner:
            if (in.constrain)
                q = constrainedCorner(start.quad, i, d);
            else
                q.p[i] = start.quad.p[i] + d;
            break;
        case EditHandle::edge: {
            const int j = (i + 1) % 4;
            const Vec2 n = perp(normalized(start.quad.p[j] - start.quad.p[i]));
            const double m = dot(d, n);
            q.p[i] = start.quad.p[i] + n * m;
            q.p[j] = start.quad.p[j] + n * m;
            break;
        }
        case EditHandle::move:
        case EditHandle::center:
            for (auto& p : q.p)
                p = p + d;
            break;
        default:
            return std::nullopt;
        }
        if (!isValid(s)) return std::nullopt;
        return s;
    }
    Ellipse& e = s.ellipse;
    const Ellipse& e0 = start.ellipse;
    switch (in.handle) {
    case EditHandle::move:
    case EditHandle::center:
        e.c = e0.c + d;
        break;
    case EditHandle::radiusX:
    case EditHandle::radiusY: {
        const bool xAxis = in.handle == EditHandle::radiusX;
        const Vec2 ax{std::cos(e0.angleRad), std::sin(e0.angleRad)};
        const Vec2 axis = xAxis ? ax : perp(ax);
        const double r0 = xAxis ? e0.rx : e0.ry;
        const double r = std::max(kMinEditSize, dot(ellipseHandle(e0, xAxis) + d - e0.c, axis));
        if (isCircle(e0)) {
            e.rx = e.ry = r; // a circle changes only its radius
        } else if (in.constrain) {
            const double k = r / r0;
            e.rx = std::max(kMinEditSize, e0.rx * k);
            e.ry = std::max(kMinEditSize, e0.ry * k);
        } else {
            (xAxis ? e.rx : e.ry) = r;
        }
        break;
    }
    default:
        return std::nullopt;
    }
    if (!isValid(s)) return std::nullopt;
    return s;
}

double area(const Shape& s) {
    if (s.kind == ShapeKind::ellipse) return kPi * s.ellipse.rx * s.ellipse.ry;
    double a = 0;
    for (int i = 0; i < 4; ++i)
        a += cross(s.quad.p[i], s.quad.p[(i + 1) % 4]);
    return std::abs(a) * 0.5;
}

Vec2 centroid(const Shape& s) {
    if (s.kind == ShapeKind::ellipse) return s.ellipse.c;
    Vec2 c{0, 0};
    for (auto& p : s.quad.p)
        c = c + p;
    return c / 4.0;
}

double frameAngle(const Quad& q) {
    const double a[4] = {
        std::atan2(q.p[1].y - q.p[0].y, q.p[1].x - q.p[0].x),
        std::atan2(q.p[2].y - q.p[3].y, q.p[2].x - q.p[3].x),
        std::atan2(q.p[2].y - q.p[1].y, q.p[2].x - q.p[1].x) - kPi / 2,
        std::atan2(q.p[3].y - q.p[0].y, q.p[3].x - q.p[0].x) - kPi / 2,
    };
    // Average on the circle of 4θ so that 90°-ambiguous edges agree, then unwrap to (-π/4, π/4].
    double sx = 0, sy = 0;
    for (double v : a) {
        sx += std::cos(4 * v);
        sy += std::sin(4 * v);
    }
    double th = std::atan2(sy, sx) / 4.0;
    if (th <= -kPi / 4) th += kPi / 2;
    if (th > kPi / 4) th -= kPi / 2;
    return th;
}

std::array<double, 2> quadSideLengths(const Quad& q) {
    const double w = 0.5 * (norm(q.p[1] - q.p[0]) + norm(q.p[2] - q.p[3]));
    const double h = 0.5 * (norm(q.p[2] - q.p[1]) + norm(q.p[3] - q.p[0]));
    return {w, h};
}

Quad rectQuad(double x0, double y0, double x1, double y1) {
    return Quad{{Vec2{x0, y0}, Vec2{x1, y0}, Vec2{x1, y1}, Vec2{x0, y1}}};
}

const char* geometryModeName(GeometryMode m) {
    switch (m) {
    case GeometryMode::pixel_crop:
        return "pixel_crop";
    case GeometryMode::straightened:
        return "straightened";
    case GeometryMode::standard_size:
        return "standard_size";
    }
    return "pixel_crop";
}

const char* maskModeName(MaskMode m) {
    switch (m) {
    case MaskMode::hard_edge:
        return "hard_edge";
    case MaskMode::feather:
        return "feather";
    case MaskMode::preserve_edge:
        return "preserve_edge";
    }
    return "hard_edge";
}

namespace {
/// Projective map of the unit square onto `q` ((0,0)→p0, (1,0)→p1, (1,1)→p2, (0,1)→p3; Heckbert 1989).
std::optional<Mat3> squareToQuad(const Quad& q) {
    const double x0 = q.p[0].x, y0 = q.p[0].y, x1 = q.p[1].x, y1 = q.p[1].y;
    const double x2 = q.p[2].x, y2 = q.p[2].y, x3 = q.p[3].x, y3 = q.p[3].y;
    const double dx1 = x1 - x2, dx2 = x3 - x2, dx3 = x0 - x1 + x2 - x3;
    const double dy1 = y1 - y2, dy2 = y3 - y2, dy3 = y0 - y1 + y2 - y3;
    if (std::abs(dx3) < 1e-12 && std::abs(dy3) < 1e-12) // a parallelogram: affine
        return Mat3{{x1 - x0, x3 - x0, x0, y1 - y0, y3 - y0, y0, 0, 0, 1}};
    const double den = dx1 * dy2 - dx2 * dy1;
    if (std::abs(den) < 1e-12) return std::nullopt;
    const double g = (dx3 * dy2 - dx2 * dy3) / den, h = (dx1 * dy3 - dx3 * dy1) / den;
    return Mat3{{x1 - x0 + g * x1, x3 - x0 + h * x3, x0, y1 - y0 + g * y1, y3 - y0 + h * y3, y0, g, h, 1}};
}
} // namespace

ExportGeometry computeExportGeometry(const Shape& region, const Quad& frame, GeometryMode mode, int upright,
                                     std::optional<SizeI> standard, SizeI scan) {
    ExportGeometry g;
    g.mode = mode;
    g.upright = ((upright % 4) + 4) % 4;
    const bool swap = (g.upright & 1) != 0;

    if (mode == GeometryMode::pixel_crop) {
        const RectI r = boundingRect(region, scan);
        const Mat3 toBox = Mat3::translate(static_cast<double>(-r.x), static_cast<double>(-r.y));
        const Mat3 up = uprightMatrix(g.upright, static_cast<double>(r.w), static_cast<double>(r.h));
        g.scanToOut = up * toBox;
        g.outToScan = *g.scanToOut.inverse();
        g.outSize = swap ? SizeI{r.h, r.w} : SizeI{r.w, r.h};
        g.sourceRect = r;
        g.resampled = false;
        g.angleRad = 0;
        g.scale = 1;
        return g;
    }

    // UX-02 (U012): a quad image area is mapped exactly onto the output rectangle — its four edges become the
    // output's edges — so "image only" holds no frame even when the window is not a perfect rectangle in the
    // scan (scanner shear, measured 0.3° on real scans) or its angle differs from the frame's. One
    // resampling, as before; earlier exports keep the matrices stored with them.
    if (region.kind == ShapeKind::quad) {
        const Quad q = normalizeQuad(region.quad.p);
        const double wd = (norm(q.p[1] - q.p[0]) + norm(q.p[2] - q.p[3])) / 2;
        const double hd = (norm(q.p[3] - q.p[0]) + norm(q.p[2] - q.p[1])) / 2;
        double scale = 1.0;
        if (mode == GeometryMode::standard_size && standard && standard->w > 0 && standard->h > 0) {
            const double tw = static_cast<double>(swap ? standard->h : standard->w);
            const double th = static_cast<double>(swap ? standard->w : standard->h);
            scale = std::min(tw / wd, th / hd);
        }
        const int64_t bw = std::max<int64_t>(1, static_cast<int64_t>(std::ceil(wd * scale - 1e-6)));
        const int64_t bh = std::max<int64_t>(1, static_cast<int64_t>(std::ceil(hd * scale - 1e-6)));
        if (auto sq = squareToQuad(q)) {
            const Mat3 boxToScan =
                *sq * Mat3::scale(1.0 / static_cast<double>(bw), 1.0 / static_cast<double>(bh));
            if (auto toBox = boxToScan.inverse()) {
                const Mat3 up = uprightMatrix(g.upright, static_cast<double>(bw), static_cast<double>(bh));
                g.scanToOut = up * *toBox;
                g.outToScan = *g.scanToOut.inverse();
                g.outSize = swap ? SizeI{bh, bw} : SizeI{bw, bh};
                RectI src = boundingRect(region);
                const int sup = static_cast<int>(std::ceil(kResampleSupport / std::min(1.0, scale)));
                const int64_t x0 = std::clamp<int64_t>(src.x - sup, 0, scan.w),
                              y0 = std::clamp<int64_t>(src.y - sup, 0, scan.h);
                const int64_t x1 = std::clamp<int64_t>(src.right() + sup, 0, scan.w),
                              y1 = std::clamp<int64_t>(src.bottom() + sup, 0, scan.h);
                g.sourceRect = {x0, y0, x1 - x0, y1 - y0};
                g.resampled = true;
                // the label's angle: mean direction of the top and bottom edges
                const Vec2 dt = q.p[1] - q.p[0], db = q.p[2] - q.p[3];
                g.angleRad = (std::atan2(dt.y, dt.x) + std::atan2(db.y, db.x)) / 2;
                g.scale = scale;
                return g;
            }
        }
    }

    const double theta = frameAngle(frame);
    const Mat3 unrotate = Mat3::rotate(-theta);
    const RectD rb = boundsOf(transformShape(region, unrotate));
    const double wd = rb.width(), hd = rb.height();
    const Vec2 rc{(rb.x0 + rb.x1) / 2, (rb.y0 + rb.y1) / 2};
    double scale = 1.0;
    int64_t bw, bh;
    if (mode == GeometryMode::standard_size && standard && standard->w > 0 && standard->h > 0) {
        const double tw = static_cast<double>(swap ? standard->h : standard->w);
        const double th = static_cast<double>(swap ? standard->w : standard->h);
        scale = std::min(tw / wd, th / hd);
        bw = std::max<int64_t>(1, std::llround(wd * scale));
        bh = std::max<int64_t>(1, std::llround(hd * scale));
    } else {
        bw = std::max<int64_t>(1, static_cast<int64_t>(std::ceil(wd - 1e-6)));
        bh = std::max<int64_t>(1, static_cast<int64_t>(std::ceil(hd - 1e-6)));
    }
    const Mat3 toBox = Mat3::translate(bw / 2.0, bh / 2.0) * Mat3::scale(scale, scale) *
                       Mat3::translate(-rc.x, -rc.y) * unrotate;
    const Mat3 up = uprightMatrix(g.upright, static_cast<double>(bw), static_cast<double>(bh));
    g.scanToOut = up * toBox;
    g.outToScan = *g.scanToOut.inverse();
    g.outSize = swap ? SizeI{bh, bw} : SizeI{bw, bh};
    RectI src = boundingRect(region);
    const int sup = static_cast<int>(std::ceil(kResampleSupport / std::min(1.0, scale)));
    src.x -= sup;
    src.y -= sup;
    src.w += 2 * sup;
    src.h += 2 * sup;
    const int64_t x0 = std::clamp<int64_t>(src.x, 0, scan.w), y0 = std::clamp<int64_t>(src.y, 0, scan.h);
    const int64_t x1 = std::clamp<int64_t>(src.right(), 0, scan.w),
                  y1 = std::clamp<int64_t>(src.bottom(), 0, scan.h);
    g.sourceRect = {x0, y0, x1 - x0, y1 - y0};
    g.resampled = true;
    g.angleRad = theta;
    g.scale = scale;
    return g;
}

double maskWeight(const Shape& s, const MaskParams& p, double x, double y) {
    const Vec2 q{x, y};
    double d = signedDistance(s, q);
    if (p.mode == MaskMode::preserve_edge || p.preserveEdgePx > 0) d -= std::max(0.0, p.preserveEdgePx);
    if (p.mode == MaskMode::feather && p.featherPx > 0) {
        if (d <= 0) return 0.0;
        return std::min(1.0, d / p.featherPx);
    }
    if (p.preserveEdgePx <= 0) return contains(s, q) ? 1.0 : 0.0; // exact pixel-centre containment
    return d >= 0 ? 1.0 : 0.0;
}

} // namespace fk
