// RFC-0004 / PRD F02–F03 detection tests. Scenes are rendered here with a small, independent renderer
// (not the fkfixture generator) so that the oracle does not share code with the corpus tool.
#include <gtest/gtest.h>

#include "fk/detection.hpp"
#include "fk/image_io.hpp"
#include "support/test_support.hpp"

#include "detection/cv.hpp"

#include <tiffio.h>

#include <cmath>
#include <numbers>

using namespace fk;
using fk::test::TempDir;

namespace {

constexpr double kPi = std::numbers::pi;

struct Card {
    double cx = 0, cy = 0;   ///< centre (scan px)
    double angleDeg = 0;     ///< clockwise on screen, includes quarter turns
    double w = 54, h = 86;   ///< card size (mm), upright
    double ix = 4, iy = 6;   ///< image area origin (mm), upright
    double iw = 46, ih = 62; ///< image area size (mm)
    std::array<double, 3> frame{0.95, 0.95, 0.93};
    bool darkImage = false;
};

struct Scene {
    double dpi = 300;
    double wMm = 150, hMm = 120;
    double bg = 0.08;
    std::vector<Card> cards;
};

double pxPerMm(const Scene& s) {
    return s.dpi / 25.4;
}

// card-local (mm, upright) of a scan position
Vec2 toCard(const Scene& s, const Card& c, double x, double y) {
    const double a = c.angleDeg * kPi / 180;
    const double dx = x - c.cx, dy = y - c.cy;
    const double lx = std::cos(a) * dx + std::sin(a) * dy, ly = -std::sin(a) * dx + std::cos(a) * dy;
    return {lx / pxPerMm(s) + c.w / 2, ly / pxPerMm(s) + c.h / 2};
}
Vec2 toScan(const Scene& s, const Card& c, double u, double v) {
    const double a = c.angleDeg * kPi / 180;
    const double lx = (u - c.w / 2) * pxPerMm(s), ly = (v - c.h / 2) * pxPerMm(s);
    return {c.cx + std::cos(a) * lx - std::sin(a) * ly, c.cy + std::sin(a) * lx + std::cos(a) * ly};
}

/// 8-bit RGB scan with 4×4 supersampled edges and deterministic noise.
ImageBuffer render(const Scene& s) {
    const int64_t W = std::llround(s.wMm * pxPerMm(s)), H = std::llround(s.hMm * pxPerMm(s));
    ImageBuffer img = std::move(ImageBuffer::create(W, H, 3, 8).value());
    uint32_t rng = 12345;
    auto noise = [&]() {
        rng = rng * 1664525u + 1013904223u;
        return ((rng >> 8) & 0xffff) / 65535.0 - 0.5;
    };
    for (int64_t y = 0; y < H; ++y)
        for (int64_t x = 0; x < W; ++x) {
            double acc[3] = {0, 0, 0};
            for (int sy = 0; sy < 4; ++sy)
                for (int sx = 0; sx < 4; ++sx) {
                    const double px = x + (sx + 0.5) / 4, py = y + (sy + 0.5) / 4;
                    double col[3] = {s.bg, s.bg, s.bg};
                    for (const Card& c : s.cards) {
                        const Vec2 q = toCard(s, c, px, py);
                        if (q.x < 0 || q.y < 0 || q.x >= c.w || q.y >= c.h) continue;
                        if (q.x >= c.ix && q.y >= c.iy && q.x < c.ix + c.iw && q.y < c.iy + c.ih) {
                            const double t = 0.5 + 0.35 * std::sin(q.x * 0.31) * std::cos(q.y * 0.17);
                            const double k = c.darkImage ? 0.25 : 1.0;
                            col[0] = k * t;
                            col[1] = k * (0.3 + 0.4 * t);
                            col[2] = k * (1.0 - t);
                        } else {
                            for (int i = 0; i < 3; ++i)
                                col[i] = c.frame[i];
                        }
                    }
                    for (int i = 0; i < 3; ++i)
                        acc[i] += col[i] / 16;
                }
            uint8_t* o = img.row(y) + x * 3;
            for (int i = 0; i < 3; ++i)
                o[i] =
                    static_cast<uint8_t>(std::clamp(std::lround((acc[i] + 0.006 * noise()) * 255), 0L, 255L));
        }
    return img;
}

struct Detected {
    std::unique_ptr<TiffSource> src;
    Proxy proxy;
    DetectionResult res;
};

Detected detect(const std::filesystem::path& p, int expected, const Template* tmpl = nullptr) {
    Detected d;
    auto s = openTiff(p);
    EXPECT_TRUE(s.ok());
    d.src = std::move(*s);
    CancelToken c;
    auto px = buildProxy(*d.src, 3000, c, {});
    EXPECT_TRUE(px.ok());
    d.proxy = std::move(*px);
    DetectParams params;
    params.expectedCount = expected;
    params.tmpl = tmpl;
    auto r = detectPrints(*d.src, d.proxy, params, c, {});
    EXPECT_TRUE(r.ok()) << (r.ok() ? "" : r.error().message);
    if (r.ok()) d.res = std::move(*r);
    return d;
}

std::filesystem::path writeScene(const TempDir& dir, const Scene& s, const std::string& name) {
    const auto p = dir / name;
    fk::test::TestTiffOptions o;
    o.dpi = s.dpi;
    o.compression = COMPRESSION_LZW;
    fk::test::writeTestTiff(p, render(s), o);
    return p;
}

/// The detected item nearest to the card centre.
const DetectedItem* nearest(const DetectionResult& r, const Card& c) {
    const DetectedItem* best = nullptr;
    double bd = 1e300;
    for (const auto& it : r.items) {
        const double d = norm(centroid(it.outer) - Vec2{c.cx, c.cy});
        if (d < bd) {
            bd = d;
            best = &it;
        }
    }
    return best;
}

/// Most negative signed distance of the card outline (sampled densely) to the detected outer quad.
double worstContainment(const Scene& s, const Card& c, const Shape& outer) {
    double worst = 1e300;
    for (int k = 0; k <= 200; ++k) {
        const double t = k / 200.0;
        for (const Vec2 uv : {Vec2{t * c.w, 0}, Vec2{c.w, t * c.h}, Vec2{t * c.w, c.h}, Vec2{0, t * c.h}})
            worst = std::min(worst, signedDistance(outer, toScan(s, c, uv.x, uv.y)));
    }
    return worst;
}

double maxCornerError(const Scene& s, const Card& c, const Shape& outer) {
    double worst = 0;
    for (const Vec2 uv : {Vec2{0, 0}, Vec2{c.w, 0}, Vec2{c.w, c.h}, Vec2{0, c.h}}) {
        const Vec2 g = toScan(s, c, uv.x, uv.y);
        double best = 1e300;
        for (const Vec2& p : outer.quad.p)
            best = std::min(best, norm(p - g));
        worst = std::max(worst, best);
    }
    return worst;
}

double maxInnerCornerError(const Scene& s, const Card& c, const Shape& inner) {
    double worst = 0;
    for (const Vec2 uv : {Vec2{c.ix, c.iy}, Vec2{c.ix + c.iw, c.iy}, Vec2{c.ix + c.iw, c.iy + c.ih},
                          Vec2{c.ix, c.iy + c.ih}}) {
        const Vec2 g = toScan(s, c, uv.x, uv.y);
        double best = 1e300;
        for (const Vec2& p : inner.quad.p)
            best = std::min(best, norm(p - g));
        worst = std::max(worst, best);
    }
    return worst;
}

Scene twoMinis() {
    Scene s;
    s.wMm = 165;
    Card a;
    a.cx = 40 * pxPerMm(s);
    a.cy = 60 * pxPerMm(s);
    a.angleDeg = 2.5;
    Card b = a;
    b.cx = 117 * pxPerMm(s);
    b.cy = 62 * pxPerMm(s);
    b.angleDeg = 90 - 1.5; // a quarter turn: upright = 3
    s.cards = {a, b};
    return s;
}

} // namespace

// ---- cv helpers --------------------------------------------------------------------------------------

TEST(DetectCv, MinAreaRectOfRotatedRectangle) {
    std::vector<Vec2> pts;
    const double a = 0.2;
    for (int i = 0; i <= 40; ++i)
        for (int j = 0; j <= 20; ++j) {
            const double x = i * 5.0 - 100, y = j * 5.0 - 50;
            pts.push_back({300 + std::cos(a) * x - std::sin(a) * y, 200 + std::sin(a) * x + std::cos(a) * y});
        }
    const auto r = cv::minAreaRect(cv::convexHull(pts));
    EXPECT_NEAR(std::max(r.w, r.h), 200, 1e-6);
    EXPECT_NEAR(std::min(r.w, r.h), 100, 1e-6);
    EXPECT_NEAR(r.c.x, 300, 1e-6);
    EXPECT_NEAR(r.c.y, 200, 1e-6);
    EXPECT_NEAR(std::fmod(r.angle + 2 * kPi, kPi / 2), 0.2, 1e-9);
}

TEST(DetectCv, RobustLineIgnoresOutliers) {
    std::vector<Vec2> pts;
    for (int i = 0; i < 100; ++i)
        pts.push_back({static_cast<double>(i), 0.5 * i + 3});
    for (int i = 0; i < 10; ++i)
        pts.push_back({i * 9.0, 200.0}); // 9 % gross outliers
    auto l = cv::fitLineRobust(pts);
    ASSERT_TRUE(l.has_value());
    for (int i = 0; i < 100; i += 10)
        EXPECT_NEAR(signedDistance(*l, {static_cast<double>(i), 0.5 * i + 3}), 0, 1e-3) << "x=" << i;
}

TEST(DetectCv, OtsuSplitsBimodal) {
    std::vector<float> v;
    for (int i = 0; i < 1000; ++i)
        v.push_back(10.f + (i % 7) * 0.5f);
    for (int i = 0; i < 1000; ++i)
        v.push_back(60.f + (i % 5) * 0.5f);
    const double t = cv::otsuThreshold(v, 100.0);
    EXPECT_GE(t, 13.0); // anything in [13, 60) separates the two modes
    EXPECT_LT(t, 60.0);
}

// ---- end-to-end detection ----------------------------------------------------------------------------

TEST(Detection, FindsAndContainsTwoTiltedPrints) {
    TempDir dir;
    const Scene s = twoMinis();
    const auto p = writeScene(dir, s, "two.tif");
    const auto d = detect(p, 2);
    ASSERT_EQ(d.res.items.size(), 2u);
    for (const Card& c : s.cards) {
        const DetectedItem* it = nearest(d.res, c);
        ASSERT_NE(it, nullptr);
        EXPECT_GE(worstContainment(s, c, it->outer), -0.5) << "rule: never cut the real frame";
        EXPECT_LE(maxCornerError(s, c, it->outer), 4.0);
        EXPECT_EQ(it->filmTypeId, "instax_mini");
        ASSERT_EQ(it->inner.kind, ShapeKind::quad);
        EXPECT_LE(maxInnerCornerError(s, c, it->inner), 3.0);
        EXPECT_GE(it->confidence, kHighConfidence);
    }
    EXPECT_EQ(nearest(d.res, s.cards[0])->upright, 0);
    EXPECT_EQ(nearest(d.res, s.cards[1])->upright, 3);
    // reading order: left to right in one row
    EXPECT_LT(centroid(d.res.items[0].outer).x, centroid(d.res.items[1].outer).x);
}

TEST(Detection, TemplateReproducesItems) {
    TempDir dir;
    const Scene s = twoMinis();
    const auto p = writeScene(dir, s, "two.tif");
    const auto d = detect(p, 2);
    ASSERT_EQ(d.res.items.size(), 2u);
    const Template t = makeTemplate(d.res.items, {d.src->info().width, d.src->info().height}, s.dpi, "jig");
    const auto d2 = detect(p, 2, &t);
    ASSERT_EQ(d2.res.items.size(), 2u);
    for (size_t i = 0; i < 2; ++i) {
        for (int k = 0; k < 4; ++k)
            EXPECT_LE(norm(d2.res.items[i].outer.quad.p[k] - d.res.items[i].outer.quad.p[k]), 2.0);
        const auto& r = d2.res.items[i].reasons;
        EXPECT_NE(std::find(r.begin(), r.end(), "template"), r.end());
    }
}

TEST(Detection, TouchingPrintsAreSplitContainedAndFlagged) {
    TempDir dir;
    Scene s;
    Card a;
    a.cx = 500;
    a.cy = 650;
    a.angleDeg = 1.0;
    Card b = a;
    // share the right edge of `a` exactly, offset downward by 6 mm
    const Vec2 shift = toScan(s, a, a.w * 1.5, a.h / 2 + 6) - toScan(s, a, a.w / 2, a.h / 2);
    b.cx += shift.x;
    b.cy += shift.y;
    b.frame = {0.85, 0.83, 0.78};
    s.cards = {a, b};
    const auto p = writeScene(dir, s, "touching.tif");
    const auto d = detect(p, 2);
    ASSERT_EQ(d.res.items.size(), 2u);
    for (const Card& c : s.cards) {
        const DetectedItem* it = nearest(d.res, c);
        EXPECT_GE(worstContainment(s, c, it->outer), -0.5);
        EXPECT_LE(maxCornerError(s, c, it->outer), 30.0);
        EXPECT_LT(it->confidence, kLowConfidence) << "touching prints always need review";
    }
}

TEST(Detection, InvisibleFrameIsContainedAndFlagged) {
    TempDir dir;
    Scene s;
    s.bg = 0.06;
    Card a;
    a.cx = 600;
    a.cy = 650;
    a.angleDeg = -1.0;
    a.frame = {0.07, 0.07, 0.07}; // black frame on a black background
    s.cards = {a};
    const auto p = writeScene(dir, s, "black.tif");
    const auto d = detect(p, 1);
    if (d.res.items.empty()) {
        // allowed only with a scan-level warning
        ASSERT_FALSE(d.res.warnings.empty());
        return;
    }
    const DetectedItem* it = nearest(d.res, a);
    EXPECT_GE(worstContainment(s, a, it->outer), -0.5);
    EXPECT_LT(it->confidence, kLowConfidence);
}

TEST(Detection, CountMismatchIsReported) {
    TempDir dir;
    const Scene s = twoMinis();
    const auto p = writeScene(dir, s, "two.tif");
    const auto d = detect(p, 3);
    ASSERT_EQ(d.res.items.size(), 2u);
    bool warned = false;
    for (const auto& w : d.res.warnings)
        warned = warned || w.rfind("count_mismatch", 0) == 0;
    EXPECT_TRUE(warned);
    for (const auto& it : d.res.items)
        EXPECT_NE(std::find(it.reasons.begin(), it.reasons.end(), "count_mismatch"), it.reasons.end());
}

TEST(Detection, CancellationStops) {
    TempDir dir;
    const Scene s = twoMinis();
    const auto p = writeScene(dir, s, "two.tif");
    auto src = openTiff(p);
    ASSERT_TRUE(src.ok());
    CancelToken c;
    auto px = buildProxy(**src, 3000, c, {});
    ASSERT_TRUE(px.ok());
    c.cancel();
    auto r = detectPrints(**src, *px, DetectParams{}, c, {});
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, Errc::cancelled);
}

TEST(Detection, BuiltinFilmTypesAreConsistent) {
    const auto& types = builtinFilmTypes();
    ASSERT_GE(types.size(), 5u);
    for (const auto& t : types) {
        EXPECT_GT(t.widthMm, t.imageWidthMm) << t.id;
        EXPECT_GT(t.heightMm, t.imageHeightMm) << t.id;
        EXPECT_LE(t.imageLeftMm + t.imageWidthMm, t.widthMm) << t.id;
        EXPECT_LE(t.imageTopMm + t.imageHeightMm, t.heightMm) << t.id;
    }
}
