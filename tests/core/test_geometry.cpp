// RFC-0002 geometry tests.
#include <gtest/gtest.h>

#include "fk/geometry.hpp"

#include <cmath>
#include <numbers>

using namespace fk;

namespace {
constexpr double kPi = std::numbers::pi;

Quad rotatedRect(Vec2 c, double w, double h, double angle) {
    const Mat3 m = Mat3::translate(c.x, c.y) * Mat3::rotate(angle);
    std::array<Vec2, 4> p = {m.apply({-w / 2, -h / 2}), m.apply({w / 2, -h / 2}), m.apply({w / 2, h / 2}),
                             m.apply({-w / 2, h / 2})};
    return normalizeQuad(p);
}
} // namespace

TEST(Geometry, NormalizeQuadOrdersClockwiseFromTopLeft) {
    const Quad q = normalizeQuad({Vec2{10, 50}, Vec2{50, 10}, Vec2{10, 10}, Vec2{50, 50}});
    EXPECT_EQ(q.p[0], (Vec2{10, 10}));
    EXPECT_EQ(q.p[1], (Vec2{50, 10}));
    EXPECT_EQ(q.p[2], (Vec2{50, 50}));
    EXPECT_EQ(q.p[3], (Vec2{10, 50}));
    EXPECT_TRUE(isConvex(q));
}

TEST(Geometry, ContainsAndSignedDistanceQuad) {
    const Shape s = Shape::fromQuad(rectQuad(0, 0, 10, 20));
    EXPECT_TRUE(contains(s, {5, 5}));
    EXPECT_FALSE(contains(s, {-0.1, 5}));
    EXPECT_NEAR(signedDistance(s, {5, 5}), 5.0, 1e-12);
    EXPECT_NEAR(signedDistance(s, {-3, 5}), -3.0, 1e-12);
    EXPECT_NEAR(signedDistance(s, {13, 24}), -5.0, 1e-12);
}

TEST(Geometry, EllipseCircleDistance) {
    const Shape c = Shape::fromEllipse({{100, 100}, 40, 40, 0.3});
    EXPECT_TRUE(contains(c, {100, 139.9}));
    EXPECT_FALSE(contains(c, {100, 140.1}));
    EXPECT_NEAR(signedDistance(c, {100, 130}), 10.0, 1e-9);
    const RectD b = boundsOf(c);
    EXPECT_NEAR(b.x0, 60, 1e-9);
    EXPECT_NEAR(b.y1, 140, 1e-9);
}

TEST(Geometry, ExpandShapeMovesEdgesOutward) {
    const Shape s = Shape::fromQuad(rectQuad(10, 10, 20, 30));
    const Shape e = expandShape(s, 2);
    EXPECT_NEAR(e.quad.p[0].x, 8, 1e-9);
    EXPECT_NEAR(e.quad.p[0].y, 8, 1e-9);
    EXPECT_NEAR(e.quad.p[2].x, 22, 1e-9);
    EXPECT_NEAR(e.quad.p[2].y, 32, 1e-9);
    const Shape r = Shape::fromQuad(rotatedRect({50, 50}, 20, 30, 0.2));
    const Shape re = expandShape(r, 1.5);
    for (int i = 0; i < 4; ++i)
        EXPECT_NEAR(signedDistance(r, re.quad.p[i]), -1.5 * std::sqrt(2.0), 1e-6);
}

TEST(Geometry, FrameAngleRecoversTiltAndIsQuarterTurnInvariant) {
    for (double deg : {-30.0, -5.0, -0.5, 0.0, 2.0, 12.0, 40.0}) {
        const double a = deg * kPi / 180;
        EXPECT_NEAR(frameAngle(rotatedRect({500, 400}, 300, 380, a)), a, 1e-9) << deg;
        EXPECT_NEAR(frameAngle(rotatedRect({500, 400}, 300, 380, a + kPi / 2)), a, 1e-9) << deg;
    }
}

TEST(Geometry, PixelCropIsIntegerTranslation) {
    const Shape s = Shape::fromQuad(rectQuad(10.4, 20.6, 50.2, 80.0));
    const auto g = computeExportGeometry(s, s.quad, GeometryMode::pixel_crop, 0, std::nullopt, {200, 200});
    EXPECT_FALSE(g.resampled);
    EXPECT_EQ(g.sourceRect, (RectI{10, 20, 41, 60}));
    EXPECT_EQ(g.outSize, (SizeI{41, 60}));
    const Vec2 o = g.scanToOut.apply({10.5, 20.5}); // centre of scan pixel (10,20)
    EXPECT_DOUBLE_EQ(o.x, 0.5);
    EXPECT_DOUBLE_EQ(o.y, 0.5);
}

TEST(Geometry, PixelCropClampsToScan) {
    const Shape s = Shape::fromQuad(rectQuad(-5, -5, 30, 30));
    const auto g = computeExportGeometry(s, s.quad, GeometryMode::pixel_crop, 0, std::nullopt, {20, 25});
    EXPECT_EQ(g.sourceRect, (RectI{0, 0, 20, 25}));
}

TEST(Geometry, UprightTurnsArePixelPermutations) {
    const Shape s = Shape::fromQuad(rectQuad(0, 0, 7, 5));
    for (int k = 0; k < 4; ++k) {
        const auto g = computeExportGeometry(s, s.quad, GeometryMode::pixel_crop, k, std::nullopt, {7, 5});
        EXPECT_EQ(g.outSize, (k & 1) ? (SizeI{5, 7}) : (SizeI{7, 5}));
        std::vector<int> hit(35, 0);
        for (int y = 0; y < 5; ++y)
            for (int x = 0; x < 7; ++x) {
                const Vec2 o = g.scanToOut.apply({x + 0.5, y + 0.5});
                const double fx = o.x - 0.5, fy = o.y - 0.5;
                ASSERT_NEAR(fx, std::round(fx), 1e-12);
                ASSERT_NEAR(fy, std::round(fy), 1e-12);
                const int ox = static_cast<int>(std::lround(fx)), oy = static_cast<int>(std::lround(fy));
                ASSERT_GE(ox, 0);
                ASSERT_LT(ox, g.outSize.w);
                ASSERT_GE(oy, 0);
                ASSERT_LT(oy, g.outSize.h);
                hit[oy * g.outSize.w + ox]++;
            }
        for (int v : hit)
            EXPECT_EQ(v, 1) << "k=" << k;
    }
    // 90° clockwise: the scan's top-left pixel lands in the output's top-right corner.
    const auto g1 = computeExportGeometry(s, s.quad, GeometryMode::pixel_crop, 1, std::nullopt, {7, 5});
    const Vec2 o = g1.scanToOut.apply({0.5, 0.5});
    EXPECT_DOUBLE_EQ(o.x, 4.5);
    EXPECT_DOUBLE_EQ(o.y, 0.5);
}

TEST(Geometry, StraightenedMapsFrameToAxisAlignedBox) {
    const Quad q = rotatedRect({600, 500}, 400, 300, 3 * kPi / 180);
    const Shape s = Shape::fromQuad(q);
    const auto g = computeExportGeometry(s, q, GeometryMode::straightened, 0, std::nullopt, {2000, 2000});
    EXPECT_TRUE(g.resampled);
    EXPECT_EQ(g.outSize, (SizeI{400, 300}));
    const Vec2 a = g.scanToOut.apply(q.p[0]);
    const Vec2 c = g.scanToOut.apply(q.p[2]);
    EXPECT_NEAR(a.x, 0, 1e-6);
    EXPECT_NEAR(a.y, 0, 1e-6);
    EXPECT_NEAR(c.x, 400, 1e-6);
    EXPECT_NEAR(c.y, 300, 1e-6);
    const Vec2 back = g.outToScan.apply(g.scanToOut.apply({612.3, 487.9}));
    EXPECT_NEAR(back.x, 612.3, 1e-9);
    EXPECT_NEAR(back.y, 487.9, 1e-9);
}

TEST(Geometry, StraightenedImageAreaFillsTheOutputExactly) {
    // UX-02 (U012): a window that is not a perfect rectangle in the scan (scanner shear 0.3°, measured on the
    // owner's scans) and whose angle differs from the frame's: its four edges become the output's edges, so
    // an "image only" export holds no frame and loses no picture. Rotation + bounding box left 6–17 px of
    // frame.
    const Quad frame =
        rotatedRect({1000, 1000}, 1800, 1900, 0.0); // a print cropped tightly: frame axis-aligned
    Quad q;
    q.p = {Vec2{215.4, 290.4}, Vec2{1854.4, 276.3}, Vec2{1871.7, 1997.3}, Vec2{227.5, 2011.6}};
    const Shape s = Shape::fromQuad(q);
    const auto g = computeExportGeometry(s, frame, GeometryMode::straightened, 0, std::nullopt, {4000, 4000});
    ASSERT_TRUE(g.resampled);
    const double w = static_cast<double>(g.outSize.w), h = static_cast<double>(g.outSize.h);
    const Vec2 corners[4] = {{0, 0}, {w, 0}, {w, h}, {0, h}};
    for (int k = 0; k < 4; ++k) {
        const Vec2 p = g.outToScan.apply(corners[k]);
        EXPECT_NEAR(p.x, q.p[k].x, 1e-6);
        EXPECT_NEAR(p.y, q.p[k].y, 1e-6);
    }
    // every point of every output edge lies on the corresponding window edge
    for (int k = 0; k <= 20; ++k) {
        const double t = k / 20.0;
        for (const Vec2 o : {Vec2{t * w, 0}, Vec2{w, t * h}, Vec2{t * w, h}, Vec2{0, t * h}})
            EXPECT_NEAR(signedDistance(s, g.outToScan.apply(o)), 0.0, 1e-6);
    }
    // the label's angle: the window's own (≈ −0.49°), not the frame's (0°)
    EXPECT_NEAR(g.angleRad * 180 / kPi, -0.49, 0.03);
    // a rotated rectangle keeps its size and maps exactly as before
    const Quad r = rotatedRect({600, 500}, 400, 300, -0.7 * kPi / 180);
    const auto gr = computeExportGeometry(Shape::fromQuad(r), frame, GeometryMode::straightened, 0,
                                          std::nullopt, {2000, 2000});
    EXPECT_EQ(gr.outSize, (SizeI{400, 300}));
    EXPECT_NEAR(gr.scanToOut.apply(r.p[2]).x, 400, 1e-6);
    EXPECT_NEAR(gr.scanToOut.apply(r.p[2]).y, 300, 1e-6);
}

TEST(Geometry, StandardSizeScalesUniformly) {
    const Quad q = rotatedRect({600, 500}, 400, 300, 0.0);
    const Shape s = Shape::fromQuad(q);
    const auto g = computeExportGeometry(s, q, GeometryMode::standard_size, 0, SizeI{200, 200}, {2000, 2000});
    EXPECT_EQ(g.outSize, (SizeI{200, 150}));
    EXPECT_NEAR(g.scale, 0.5, 1e-12);
    const auto gu =
        computeExportGeometry(s, q, GeometryMode::standard_size, 1, SizeI{150, 200}, {2000, 2000});
    EXPECT_EQ(gu.outSize, (SizeI{150, 200}));
}

TEST(Geometry, MaskWeightNeverTouchesOutside) {
    const Shape s = Shape::fromQuad(rotatedRect({50, 50}, 40, 30, 0.1));
    const Shape e = Shape::fromEllipse({{50, 50}, 20, 20, 0});
    for (const Shape& sh : {s, e})
        for (MaskMode m : {MaskMode::hard_edge, MaskMode::feather, MaskMode::preserve_edge}) {
            MaskParams p{m, 3.0, m == MaskMode::preserve_edge ? 2.0 : 0.0};
            for (int y = 0; y < 100; ++y)
                for (int x = 0; x < 100; ++x) {
                    const double w = maskWeight(sh, p, x + 0.5, y + 0.5);
                    EXPECT_GE(w, 0.0);
                    EXPECT_LE(w, 1.0);
                    if (!contains(sh, {x + 0.5, y + 0.5})) {
                        EXPECT_EQ(w, 0.0) << x << "," << y;
                    }
                }
        }
}

TEST(Geometry, Mat3InverseRoundTrip) {
    const Mat3 m = Mat3::translate(12, -7) * Mat3::rotate(0.7) * Mat3::scale(2, 3);
    const Mat3 i = *m.inverse();
    const Vec2 p = (i * m).apply({3.25, -9.5});
    EXPECT_NEAR(p.x, 3.25, 1e-12);
    EXPECT_NEAR(p.y, -9.5, 1e-12);
}

// ---- boundary editing (UX-03, RFC-0002 §Editing) --------------------------------------------

namespace {
double rightAngleError(const Quad& q) {
    double worst = 0;
    for (int i = 0; i < 4; ++i) {
        const Vec2 a = normalized(q.p[(i + 1) % 4] - q.p[i]);
        const Vec2 b = normalized(q.p[(i + 3) % 4] - q.p[i]);
        worst = std::max(worst, std::abs(dot(a, b)));
    }
    return worst; // |cos| of the worst corner angle
}
double aspect(const Quad& q) {
    const auto s = quadSideLengths(q);
    return s[0] / s[1];
}
} // namespace

TEST(Edit, FreeCornerMovesOnlyThatCorner) {
    const Shape start = Shape::fromQuad(rotatedRect({500, 400}, 300, 200, 0.12));
    const auto r = editShape(start, {EditHandle::corner, 2, {17, -9}, false});
    ASSERT_TRUE(r);
    for (int k = 0; k < 4; ++k)
        EXPECT_EQ(r->quad.p[k], (k == 2 ? start.quad.p[2] + Vec2{17, -9} : start.quad.p[k])) << k;
}

TEST(Edit, ShiftCornerKeepsRectangleAspectRotationAndOppositeCorner) {
    // a tilted rectangle: every corner, dragged in any direction, stays a rectangle of the same proportions
    // and rotation scaled about the opposite corner; the opposite corner does not move at all
    const double angle = 7.0 * kPi / 180;
    const Shape start = Shape::fromQuad(rotatedRect({800, 600}, 420, 300, angle));
    const double a0 = aspect(start.quad), t0 = frameAngle(start.quad);
    for (int i = 0; i < 4; ++i) {
        for (Vec2 d : {Vec2{40, 25}, Vec2{-60, 10}, Vec2{5, -80}, Vec2{-30, -30}}) {
            const auto r = editShape(start, {EditHandle::corner, i, d, true});
            ASSERT_TRUE(r) << i;
            const Quad& q = r->quad;
            EXPECT_EQ(q.p[(i + 2) % 4], start.quad.p[(i + 2) % 4]) << i;
            EXPECT_LT(rightAngleError(q), 1e-9) << i;
            EXPECT_NEAR(aspect(q), a0, 1e-9) << i;
            EXPECT_NEAR(frameAngle(q), t0, 1e-9) << i;
            // the dragged corner stays on the diagonal through the fixed corner
            const Vec2 diag0 = start.quad.p[i] - start.quad.p[(i + 2) % 4];
            EXPECT_NEAR(cross(normalized(diag0), q.p[i] - q.p[(i + 2) % 4]), 0.0, 1e-7) << i;
            EXPECT_TRUE(isConvex(q));
        }
    }
    // no movement: the same rectangle (a drag back to the start is a no-op)
    const auto same = editShape(start, {EditHandle::corner, 1, {0, 0}, true});
    for (int k = 0; k < 4; ++k)
        EXPECT_NEAR(norm(same->quad.p[k] - start.quad.p[k]), 0.0, 1e-9);
}

TEST(Edit, ShiftCornerDraggedPastTheOppositeCornerKeepsAMinimalRectangle) {
    const Shape start = Shape::fromQuad(rectQuad(100, 100, 400, 300));
    const auto r = editShape(start, {EditHandle::corner, 0, {500, 400}, true});
    ASSERT_TRUE(r);
    const auto s = quadSideLengths(r->quad);
    EXPECT_NEAR(std::min(s[0], s[1]), kMinEditSize, 1e-9);
    EXPECT_NEAR(s[0] / s[1], 1.5, 1e-9);
    EXPECT_EQ(r->quad.p[2], (Vec2{400, 300}));
    EXPECT_TRUE(isConvex(r->quad)); // never flipped
}

TEST(Edit, ShiftMoveLocksToTheDominantAxis) {
    const Shape start = Shape::fromQuad(rectQuad(10, 10, 110, 60));
    const auto r = editShape(start, {EditHandle::move, 0, {30, 8}, true});
    for (int k = 0; k < 4; ++k)
        EXPECT_EQ(r->quad.p[k], (start.quad.p[k] + Vec2{30, 0}));
    const Shape c = Shape::fromEllipse({{50, 50}, 20, 20, 0});
    EXPECT_EQ(editShape(c, {EditHandle::center, 0, {-3, -9}, true})->ellipse.c, (Vec2{50, 41}));
    EXPECT_EQ(editShape(c, {EditHandle::center, 0, {-3, -9}, false})->ellipse.c, (Vec2{47, 41}));
}

TEST(Edit, EdgeMovesAlongItsNormalAndNonConvexResultsAreRejected) {
    const Shape start = Shape::fromQuad(rectQuad(0, 0, 100, 50));
    const auto r = editShape(start, {EditHandle::edge, 1, {12, 33}, false}); // right edge: only x moves
    EXPECT_EQ(r->quad.p[1], (Vec2{112, 0}));
    EXPECT_EQ(r->quad.p[2], (Vec2{112, 50}));
    EXPECT_FALSE(editShape(start, {EditHandle::corner, 0, {150, 70}, false})); // folds over
}

TEST(Edit, CircleStaysACircle) {
    // UX-03: a round image area keeps rx = ry exactly whatever handle is dragged, in whatever direction, with
    // or without Shift, and nudged by arrow keys (delta along either axis)
    const Shape c = Shape::fromEllipse({{300, 300}, 120, 120, 0.3});
    for (EditHandle h : {EditHandle::radiusX, EditHandle::radiusY})
        for (Vec2 d : {Vec2{13, 40}, Vec2{-25, 3}, Vec2{1, 0}, Vec2{0, -1}})
            for (bool shift : {false, true}) {
                const auto r = editShape(c, {h, 0, d, shift});
                ASSERT_TRUE(r);
                EXPECT_EQ(r->ellipse.rx, r->ellipse.ry);
                EXPECT_EQ(r->ellipse.c, c.ellipse.c);
                EXPECT_EQ(r->ellipse.angleRad, c.ellipse.angleRad);
            }
    // the radius follows the handle along its axis
    const Vec2 ax{std::cos(0.3), std::sin(0.3)};
    EXPECT_NEAR(editShape(c, {EditHandle::radiusX, 0, ax * 10.0, false})->ellipse.rx, 130.0, 1e-9);
    // never below the minimum
    EXPECT_EQ(editShape(c, {EditHandle::radiusX, 0, ax * -500.0, false})->ellipse.rx, kMinEditSize);
}

TEST(Edit, RotatedEllipseHandlesFollowTheAxes) {
    const Ellipse e{{200, 100}, 80, 40, kPi / 6};
    const Vec2 hx = ellipseHandle(e, true), hy = ellipseHandle(e, false);
    EXPECT_NEAR(norm(hx - e.c), 80, 1e-9);
    EXPECT_NEAR(norm(hy - e.c), 40, 1e-9);
    EXPECT_NEAR(signedDistance(Shape::fromEllipse(e), hx), 0.0, 1e-6);
    EXPECT_NEAR(signedDistance(Shape::fromEllipse(e), hy), 0.0, 1e-6);
    const Vec2 ax = normalized(hx - e.c);
    const auto free = editShape(Shape::fromEllipse(e), {EditHandle::radiusX, 0, ax * 20.0, false});
    EXPECT_NEAR(free->ellipse.rx, 100, 1e-9);
    EXPECT_EQ(free->ellipse.ry, 40);
    const auto shift = editShape(Shape::fromEllipse(e), {EditHandle::radiusX, 0, ax * 20.0, true});
    EXPECT_NEAR(shift->ellipse.rx, 100, 1e-9);
    EXPECT_NEAR(shift->ellipse.ry, 50, 1e-9); // proportions kept
}

TEST(Edit, TransformsKeepACircleExactlyRound) {
    // translation + rotation: the two radii go through different rounding (without the rule 9 in 10 of these
    // differ in the last bits)
    for (int k = 0; k < 50; ++k) {
        const double r = 333.3 + 0.01 * k;
        const Shape c = Shape::fromEllipse({{1234.567 + k, 987.25}, r, r, 0.01 * k});
        const Shape t = transformShape(c, Mat3::translate(4321.5 + k, -77.7) * Mat3::rotate(0.0137 * k));
        EXPECT_EQ(t.ellipse.rx, t.ellipse.ry) << k;
    }
    Shape c = Shape::fromEllipse({{1234.5, 987.25}, 333.3, 333.3, 0});
    for (int k = 0; k < 4; ++k)
        c = transformShape(c, Mat3::translate(5000, 0) * Mat3::rotate(kPi / 2));
    EXPECT_EQ(c.ellipse.rx, c.ellipse.ry);
    c = transformShape(c, Mat3::rotate(0.123) * Mat3::scale(1.7, 1.7));
    EXPECT_EQ(c.ellipse.rx, c.ellipse.ry);
    EXPECT_TRUE(isCircle(c.ellipse));
    // a non-uniform scale makes a real ellipse
    EXPECT_FALSE(isCircle(transformShape(c, Mat3::scale(2, 1)).ellipse));
}

TEST(Geometry, QuarterTurnsAreExactPermutations) {
    // UX-04: the whole-scan view rotation and export uprights: integer corners map to integer corners, four
    // turns are the identity, the inverse is exact, sizes swap for odd turns
    const double w = 1234, h = 567;
    for (int k = -4; k <= 4; ++k) {
        const Mat3 m = quarterTurnMatrix(k, w, h);
        const SizeI t = quarterTurnSize(k, {1234, 567});
        EXPECT_EQ(t.w, (k & 1) ? 567 : 1234) << k;
        for (Vec2 p : {Vec2{0, 0}, Vec2{w, 0}, Vec2{w, h}, Vec2{0, h}, Vec2{17, 33}}) {
            const Vec2 q = m.apply(p);
            EXPECT_EQ(q.x, std::round(q.x));
            EXPECT_GE(q.x, 0);
            EXPECT_LE(q.x, static_cast<double>(t.w));
            EXPECT_GE(q.y, 0);
            EXPECT_LE(q.y, static_cast<double>(t.h));
            EXPECT_EQ(m.inverse()->apply(q), p) << k;
        }
    }
    Mat3 four = Mat3::identity();
    double cw = w, ch = h;
    for (int k = 0; k < 4; ++k) {
        four = quarterTurnMatrix(1, cw, ch) * four;
        std::swap(cw, ch);
    }
    EXPECT_EQ(four, Mat3::identity());
    // one clockwise turn: the top-left corner goes to the top right
    EXPECT_EQ(quarterTurnMatrix(1, w, h).apply({0, 0}), (Vec2{h, 0}));
}
