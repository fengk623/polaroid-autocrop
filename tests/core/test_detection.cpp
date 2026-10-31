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
#include <optional>

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
    std::optional<std::array<double, 3>> flatImage; ///< a picture of one colour
};

struct Scene {
    double dpi = 300;
    double wMm = 150, hMm = 120;
    double bg = 0.08;
    std::vector<Card> cards;
    /// A white print on a white lid (owner's scans): a dark band beyond the top and bottom edges, a dark edge
    /// line and a soft shade rising to the lid beyond the left and right edges (0 = none).
    double bandMm = 0, bandLevel = 0, lineMm = 0, lineLevel = 0, rampMm = 0;
    int blurPx = 0; ///< box blur radius applied after rendering (an out-of-focus edge)
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
                        if (q.x < 0 || q.y < 0 || q.x >= c.w || q.y >= c.h) {
                            const double dx = std::max({0.0, -q.x, q.x - c.w}),
                                         dy = std::max({0.0, -q.y, q.y - c.h});
                            double v = -1;
                            if (dx == 0 && dy < s.bandMm)
                                v = s.bandLevel;
                            else if (dy == 0 && dx < s.lineMm)
                                v = s.lineLevel;
                            else if (dy == 0 && dx < s.lineMm + s.rampMm)
                                v = c.frame[0] + (s.bg - c.frame[0]) * (dx - s.lineMm) / s.rampMm;
                            if (v >= 0)
                                for (int i = 0; i < 3; ++i)
                                    col[i] = v;
                            continue;
                        }
                        if (q.x >= c.ix && q.y >= c.iy && q.x < c.ix + c.iw && q.y < c.iy + c.ih &&
                            c.flatImage) {
                            for (int i = 0; i < 3; ++i)
                                col[i] = (*c.flatImage)[i];
                        } else if (q.x >= c.ix && q.y >= c.iy && q.x < c.ix + c.iw && q.y < c.iy + c.ih) {
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

std::filesystem::path writeScene(const TempDir& dir, const Scene& s, const std::string& name,
                                 double taggedDpi = 0) {
    const auto p = dir / name;
    fk::test::TestTiffOptions o;
    o.dpi = taggedDpi > 0 ? taggedDpi : s.dpi;
    o.compression = COMPRESSION_LZW;
    ImageBuffer img = render(s);
    if (s.blurPx > 0) { // separable box blur
        const int r = s.blurPx;
        for (int pass = 0; pass < 2; ++pass) {
            const int64_t W = img.width(), H = img.height();
            std::vector<uint8_t> line(static_cast<size_t>(std::max(W, H) * 3));
            for (int64_t a = 0; a < (pass == 0 ? H : W); ++a) {
                const int64_t len = pass == 0 ? W : H;
                auto at = [&](int64_t k) -> uint8_t* {
                    return pass == 0 ? img.row(a) + k * 3 : img.row(k) + a * 3;
                };
                for (int64_t k = 0; k < len; ++k)
                    for (int c = 0; c < 3; ++c) {
                        int sum = 0;
                        for (int64_t t = k - r; t <= k + r; ++t)
                            sum += at(std::clamp<int64_t>(t, 0, len - 1))[c];
                        line[static_cast<size_t>(k * 3 + c)] = static_cast<uint8_t>(sum / (2 * r + 1));
                    }
                for (int64_t k = 0; k < len; ++k)
                    for (int c = 0; c < 3; ++c)
                        at(k)[c] = line[static_cast<size_t>(k * 3 + c)];
            }
        }
    }
    fk::test::writeTestTiff(p, img, o);
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

TEST(Detection, SinglePrintCroppedTightlyInTheScanner) {
    // Real scans are often cropped to one print in the scanner software: no background at all (the frame
    // is cut by the crop), or a thin dark sliver along some sides.
    for (const double marginMm : {-1.0, 0.0, 0.6}) {
        TempDir dir;
        Scene s;
        Card a;
        s.wMm = a.w + 2 * marginMm;
        s.hMm = a.h + 2 * marginMm;
        s.bg = 0.05;
        a.cx = s.wMm / 2 * pxPerMm(s);
        a.cy = s.hMm / 2 * pxPerMm(s);
        s.cards = {a};
        const auto p = writeScene(dir, s, "single.tif");
        const auto d = detect(p, 1);
        ASSERT_EQ(d.res.items.size(), 1u) << "margin " << marginMm;
        const DetectedItem& it = d.res.items[0];
        const double W = static_cast<double>(d.src->info().width),
                     H = static_cast<double>(d.src->info().height);
        for (const Vec2& q : it.outer.quad.p) {
            EXPECT_GE(q.x, -1e-9);
            EXPECT_GE(q.y, -1e-9);
            EXPECT_LE(q.x, W + 1e-9);
            EXPECT_LE(q.y, H + 1e-9);
        }
        // every part of the card that is inside the scan is inside the outer
        for (int k = 0; k <= 50; ++k)
            for (const Vec2 uv : {Vec2{k * a.w / 50, 0}, Vec2{a.w, k * a.h / 50}, Vec2{k * a.w / 50, a.h},
                                  Vec2{0, k * a.h / 50}}) {
                Vec2 q = toScan(s, a, uv.x, uv.y);
                q = {std::clamp(q.x, 0.0, W), std::clamp(q.y, 0.0, H)};
                EXPECT_GE(signedDistance(it.outer, q), -0.5) << "margin " << marginMm;
            }
        EXPECT_EQ(it.filmTypeId, "instax_mini") << "margin " << marginMm;
        ASSERT_EQ(it.inner.kind, ShapeKind::quad);
        EXPECT_LE(maxInnerCornerError(s, a, it.inner), 3.0) << "margin " << marginMm;
        EXPECT_GE(it.confidence, kLowConfidence) << "margin " << marginMm;
    }
}

TEST(Detection, ScaledOutputWithNominalDpiTag) {
    // The scanner software scaled the output to 103 % but tagged the nominal resolution.
    TempDir dir;
    Scene s = twoMinis();
    s.dpi = 309;
    for (Card& c : s.cards) {
        c.cx *= 309.0 / 300.0;
        c.cy *= 309.0 / 300.0;
    }
    const auto p = writeScene(dir, s, "scaled.tif", 300);
    const auto d = detect(p, 2);
    ASSERT_EQ(d.res.items.size(), 2u);
    bool rescaled = false;
    for (const auto& w : d.res.warnings)
        rescaled = rescaled || w.rfind("dpi_rescaled", 0) == 0;
    EXPECT_TRUE(rescaled);
    EXPECT_NEAR(d.res.dpiUsed, 309, 4);
    for (const Card& c : s.cards) {
        const DetectedItem* it = nearest(d.res, c);
        EXPECT_GE(worstContainment(s, c, it->outer), -0.5);
        EXPECT_EQ(it->filmTypeId, "instax_mini");
        EXPECT_LE(maxInnerCornerError(s, c, it->inner), 3.0);
    }
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

// ---------------------------------------------------------------- UX-01: automatic count (U011)

namespace {
bool hasWarning(const DetectionResult& r, const std::string& prefix) {
    return std::any_of(r.warnings.begin(), r.warnings.end(),
                       [&](const std::string& w) { return w.rfind(prefix, 0) == 0; });
}
} // namespace

TEST(Detection, AutomaticCountHasNoCountWarning) {
    TempDir dir;
    const auto p = writeScene(dir, twoMinis(), "two.tif");
    const auto d = detect(p, 0);
    ASSERT_EQ(d.res.items.size(), 2u);
    EXPECT_FALSE(hasWarning(d.res, "count_mismatch"));
    EXPECT_FALSE(hasWarning(d.res, "possible_missed")) << "every object on the scan is a found print";
    EXPECT_FALSE(hasWarning(d.res, "no_prints_found"));
    for (const auto& it : d.res.items)
        EXPECT_EQ(std::find(it.reasons.begin(), it.reasons.end(), "count_mismatch"), it.reasons.end());
}

TEST(Detection, BlankScanReportsNoPrints) {
    TempDir dir;
    const auto p = writeScene(dir, Scene{}, "blank.tif");
    const auto automatic = detect(p, 0);
    EXPECT_TRUE(automatic.res.items.empty()) << "no print is made up";
    EXPECT_TRUE(hasWarning(automatic.res, "no_prints_found"));
    EXPECT_FALSE(hasWarning(automatic.res, "count_mismatch"));
    EXPECT_FALSE(hasWarning(automatic.res, "possible_missed"));
    const auto manual = detect(p, 4);
    EXPECT_TRUE(manual.res.items.empty()) << "a manual count never creates outlines";
    EXPECT_TRUE(hasWarning(manual.res, "count_mismatch"));
}

TEST(Detection, FaintPrintIsReportedAsPossiblyMissed) {
    // frame and picture barely differ from the background (ΔE ≈ 2–3): below the detection threshold, but a
    // print-sized area is still reported when the count is automatic
    TempDir dir;
    Scene s;
    Card a;
    a.cx = 600;
    a.cy = 650;
    a.angleDeg = 2.0;
    a.frame = {0.095, 0.095, 0.095};
    a.flatImage = std::array<double, 3>{0.10, 0.10, 0.10};
    s.cards = {a};
    const auto d = detect(writeScene(dir, s, "faint.tif"), 0);
    if (!d.res.items.empty()) GTEST_SKIP() << "the faint print was detected";
    EXPECT_TRUE(hasWarning(d.res, "possible_missed: 1")) << ::testing::PrintToString(d.res.warnings);
}

TEST(Detection, PartlyVisibleObjectIsReportedAsPossiblyMissed) {
    // a 30 × 40 mm object: too large for dust, too small for any print (e.g. the visible part of a print
    // whose frame matches the background)
    TempDir dir;
    Scene s = twoMinis();
    Card part;
    s.hMm = 160;
    part.cx = 80 * pxPerMm(s);
    part.cy = 135 * pxPerMm(s); // below the two prints
    part.w = 30;
    part.h = 40;
    part.iw = 0;
    part.ih = 0;
    part.frame = {0.8, 0.3, 0.2};
    s.cards.push_back(part);
    const auto d = detect(writeScene(dir, s, "part.tif"), 0);
    EXPECT_EQ(d.res.items.size(), 2u);
    EXPECT_TRUE(hasWarning(d.res, "possible_missed: 1"));
}

// ---------------------------------------------------------------- UX-02: outer accuracy (U012)

namespace {
/// Mean signed distance (px) of the detected outer outside the card's side `side` (0 top … 3 left), over the
/// middle 80 %: positive = slack, negative = cut.
double sideSlack(const Scene& s, const Card& c, const Shape& outer, int side) {
    double sum = 0;
    for (int k = 0; k < 40; ++k) {
        const double t = 0.1 + 0.8 * (k + 0.5) / 40;
        const Vec2 uv = side == 0   ? Vec2{t * c.w, 0}
                        : side == 1 ? Vec2{c.w, t * c.h}
                        : side == 2 ? Vec2{t * c.w, c.h}
                                    : Vec2{0, t * c.h};
        sum += signedDistance(outer, toScan(s, c, uv.x, uv.y));
    }
    return sum / 40;
}
} // namespace

TEST(Detection, WhitePrintOnWhiteLidExcludesTheShadowBand) {
    // owner's 826 dpi scan: shadow band ≈ 0.5 mm beyond the top/bottom edges, a dark edge line and a soft
    // shade beyond the sides; the outer used to land 0.9–1.9 mm out, on the lid
    TempDir dir;
    Scene s;
    s.wMm = 110;
    s.hMm = 130;
    s.bg = 0.97;
    s.bandMm = 0.5;
    s.bandLevel = 0.78;
    s.lineMm = 0.25;
    s.lineLevel = 0.79;
    s.rampMm = 1.2;
    Card a;
    a.w = 88;
    a.h = 107;
    a.ix = 6;
    a.iy = 8;
    a.iw = 76;
    a.ih = 78;
    a.frame = {0.86, 0.86, 0.85};
    a.cx = 55 * pxPerMm(s);
    a.cy = 65 * pxPerMm(s);
    a.angleDeg = 0.6;
    s.cards = {a};
    const auto d = detect(writeScene(dir, s, "white-on-white.tif"), 0);
    ASSERT_EQ(d.res.items.size(), 1u);
    const DetectedItem& it = d.res.items[0];
    EXPECT_GE(worstContainment(s, a, it.outer), -0.5) << "never cut the paper";
    for (int side = 0; side < 4; ++side) {
        const double slackMm = sideSlack(s, a, it.outer, side) / pxPerMm(s);
        EXPECT_LE(slackMm, 0.30) << "side " << side << ": on the shadow band or the lid";
        EXPECT_GE(slackMm, 0.0) << "side " << side;
    }
}

TEST(Detection, DarkPrintOnLighterBackingIsNotCut) {
    // a soft (out-of-focus) step toward a lighter backing: only half way is past the paper — a quarter of the
    // way would cut into the blurred edge (found on a generated corpus scan: 0.74 px)
    TempDir dir;
    Scene s;
    s.bg = 0.35;
    s.blurPx = 4;
    Card a;
    a.cx = 600;
    a.cy = 650;
    a.angleDeg = 1.3;
    a.frame = {0.12, 0.12, 0.12};
    s.cards = {a};
    const auto d = detect(writeScene(dir, s, "dark-on-grey.tif"), 0);
    ASSERT_EQ(d.res.items.size(), 1u);
    EXPECT_GE(worstContainment(s, a, d.res.items[0].outer), -0.5);
    for (int side = 0; side < 4; ++side)
        EXPECT_LE(sideSlack(s, a, d.res.items[0].outer, side) / pxPerMm(s), 0.30) << "side " << side;
}

TEST(Detection, SinglePrintLeavingTheScanIsContained) {
    // owner's img001: a tightly cropped scan where one side of the print leaves the scan part of the way; the
    // side used to be turned between its clamped corners and cut up to 5 px of paper
    TempDir dir;
    Scene s;
    Card a;
    s.wMm = a.w + 1.0;
    s.hMm = a.h + 1.0;
    s.bg = 0.05;
    a.cx = (s.wMm / 2 - 0.3) * pxPerMm(s);
    a.cy = s.hMm / 2 * pxPerMm(s);
    a.angleDeg = 0.5;
    s.cards = {a};
    const auto p = writeScene(dir, s, "leaving.tif");
    const auto d = detect(p, 0);
    ASSERT_EQ(d.res.items.size(), 1u);
    const DetectedItem& it = d.res.items[0];
    const double W = static_cast<double>(d.src->info().width), H = static_cast<double>(d.src->info().height);
    for (const Vec2& q : it.outer.quad.p) {
        EXPECT_GE(q.x, -1e-9);
        EXPECT_GE(q.y, -1e-9);
        EXPECT_LE(q.x, W + 1e-9);
        EXPECT_LE(q.y, H + 1e-9);
    }
    for (int k = 0; k <= 80; ++k)
        for (const Vec2 uv : {Vec2{k * a.w / 80, 0}, Vec2{a.w, k * a.h / 80}, Vec2{k * a.w / 80, a.h},
                              Vec2{0, k * a.h / 80}}) {
            Vec2 q = toScan(s, a, uv.x, uv.y);
            if (q.x < 0 || q.y < 0 || q.x > W || q.y > H) continue; // outside the scan: nothing to contain
            EXPECT_GE(signedDistance(it.outer, q), -0.5) << "card (" << uv.x << ", " << uv.y << ") mm";
        }
}
