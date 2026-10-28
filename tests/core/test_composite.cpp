// RFC-0007 / RFC-0008 / PRD F07–F08: compositing into the real scanned frame, synthetic frames.
#include <gtest/gtest.h>

#include "fk/composite.hpp"
#include "fk/hash.hpp"
#include "support/test_support.hpp"

#include <tiffio.h>

#include <cmath>
#include <cstring>
#include <numbers>

using namespace fk;
using fk::test::TempDir;

namespace {

struct Fixture {
    TempDir dir;
    std::filesystem::path scanPath;
    ImageBuffer scan;
    std::unique_ptr<TiffSource> src;
};

std::unique_ptr<Fixture> makeFixture(ImageBuffer img) {
    auto f = std::make_unique<Fixture>();
    f->scan = std::move(img);
    f->scanPath = f->dir / "scan.tif";
    fk::test::TestTiffOptions o;
    o.compression = COMPRESSION_LZW;
    o.dpi = 600;
    fk::test::writeTestTiff(f->scanPath, f->scan, o);
    auto s = openTiff(f->scanPath);
    EXPECT_TRUE(s.ok());
    f->src = std::move(*s);
    return f;
}

/// Smooth 16-bit RGB image (band-limited: resampling round trips stay close).
ImageBuffer smoothImage(int64_t w, int64_t h) {
    ImageBuffer img = std::move(ImageBuffer::create(w, h, 3, 16).value());
    for (int64_t y = 0; y < h; ++y)
        for (int64_t x = 0; x < w; ++x) {
            uint16_t v[3] = {static_cast<uint16_t>(20000 + 15000 * std::sin(x * 0.03) * std::cos(y * 0.02)),
                             static_cast<uint16_t>(30000 + 100 * x - 50 * y),
                             static_cast<uint16_t>(40000 - 60 * x)};
            std::memcpy(img.row(y) + x * 6, v, 6);
        }
    return img;
}

ExportRequest exportReq(const Shape& region, const Quad& frame, const std::filesystem::path& out,
                        GeometryMode mode = GeometryMode::pixel_crop, int upright = 0) {
    ExportRequest r;
    r.region = region;
    r.frame = frame;
    r.mode = mode;
    r.upright = upright;
    r.output = out;
    r.write.format = FileFormat::tiff;
    r.write.compression = Compression::lzw;
    r.ids = XmpIds{"p", "s", "i", "ev", "inst", 1, "P01", "image_area"};
    return r;
}

CompositeRequest compositeReq(const Shape& outer, const Shape& inner, const ExportReport& edit,
                              const ImageBuffer& edited, const std::filesystem::path& out) {
    CompositeRequest c;
    c.outerRegion = outer;
    c.frame = outer.quad;
    c.inner = inner;
    c.scanToEdit = edit.geometry.scanToOut;
    c.editSize = edit.size;
    c.editExact = !edit.resampled;
    c.edited = &edited;
    c.outputMode = GeometryMode::pixel_crop;
    c.write.format = FileFormat::tiff;
    c.write.compression = Compression::lzw;
    c.output = out;
    return c;
}

uint16_t sample16(const ImageBuffer& img, int64_t x, int64_t y, int c) {
    uint16_t v;
    std::memcpy(&v, img.row(y) + (x * img.channels() + c) * 2, 2);
    return v;
}

Quad rotatedRect(Vec2 c, double w, double h, double deg) {
    const double a = deg * std::numbers::pi / 180;
    std::array<Vec2, 4> q;
    const Vec2 d[4] = {{-w / 2, -h / 2}, {w / 2, -h / 2}, {w / 2, h / 2}, {-w / 2, h / 2}};
    for (int i = 0; i < 4; ++i)
        q[i] = {c.x + std::cos(a) * d[i].x - std::sin(a) * d[i].y,
                c.y + std::sin(a) * d[i].x + std::cos(a) * d[i].y};
    return normalizeQuad(q);
}

} // namespace

TEST(Composite, UnchangedEditRoundTripIsBitExact) {
    for (int upright : {0, 1, 2, 3}) {
        auto f = makeFixture(fk::test::makePatternImage(320, 240, 3, 16, 9));
        const Shape outer = Shape::fromQuad(rectQuad(10, 10, 300, 230));
        const Shape inner = Shape::fromQuad(rectQuad(40, 30, 260, 200));
        auto edit = exportRegion(
            *f->src, exportReq(inner, outer.quad, f->dir / "edit.tif", GeometryMode::pixel_crop, upright), {},
            {});
        ASSERT_TRUE(edit.ok()) << edit.error().message;
        EXPECT_FALSE(edit->resampled);
        auto edited = readImageFile(edit->output);
        ASSERT_TRUE(edited.ok());
        auto comp =
            compositeItem(*f->src, compositeReq(outer, inner, *edit, *edited, f->dir / "final.tif"), {}, {});
        ASSERT_TRUE(comp.ok()) << comp.error().message;
        auto direct = exportRegion(*f->src, exportReq(outer, outer.quad, f->dir / "direct.tif"), {}, {});
        ASSERT_TRUE(direct.ok());
        EXPECT_EQ(comp->pixelSha256, direct->pixelSha256) << "upright " << upright;
        EXPECT_TRUE(comp->frameBitExact);
    }
}

TEST(Composite, GradedPixelsOnlyInsideTheImageArea) {
    auto f = makeFixture(fk::test::makePatternImage(320, 240, 3, 16, 3));
    const Shape outer = Shape::fromQuad(rectQuad(10, 10, 300, 230));
    const Shape inner = Shape::fromQuad(rectQuad(40.4, 30.6, 260.2, 200.7)); // fractional boundary
    auto edit = exportRegion(*f->src, exportReq(inner, outer.quad, f->dir / "edit.tif"), {}, {});
    ASSERT_TRUE(edit.ok());
    auto edited = readImageFile(edit->output);
    ASSERT_TRUE(edited.ok());
    // "grade": invert every sample
    for (int64_t y = 0; y < edited->height(); ++y) {
        uint16_t* r = reinterpret_cast<uint16_t*>(edited->row(y));
        for (int64_t i = 0; i < edited->width() * 3; ++i)
            r[i] = static_cast<uint16_t>(65535 - r[i]);
    }
    auto comp =
        compositeItem(*f->src, compositeReq(outer, inner, *edit, *edited, f->dir / "final.tif"), {}, {});
    ASSERT_TRUE(comp.ok()) << comp.error().message;
    auto out = readImageFile(comp->output);
    ASSERT_TRUE(out.ok());
    ASSERT_EQ(out->width(), 290);
    ASSERT_EQ(out->height(), 220);
    int64_t inside = 0, outside = 0;
    for (int64_t y = 0; y < out->height(); ++y)
        for (int64_t x = 0; x < out->width(); ++x) {
            const int64_t sx = x + 10, sy = y + 10;
            const bool in = contains(inner, {sx + 0.5, sy + 0.5});
            for (int c = 0; c < 3; ++c) {
                const uint16_t s = sample16(f->scan, sx, sy, c);
                const uint16_t o = sample16(*out, x, y, c);
                if (in)
                    ASSERT_EQ(o, 65535 - s) << x << "," << y;
                else
                    ASSERT_EQ(o, s) << "frame pixel changed at " << x << "," << y;
            }
            (in ? inside : outside)++;
        }
    EXPECT_GT(inside, 30000);
    EXPECT_GT(outside, 20000);
}

TEST(Composite, StraightenedRoundTripKeepsFrameExact) {
    auto f = makeFixture(smoothImage(400, 320));
    const Shape outer = Shape::fromQuad(rotatedRect({200, 160}, 300, 240, 3.0));
    const Shape inner = Shape::fromQuad(rotatedRect({200, 150}, 240, 170, 3.0));
    auto edit = exportRegion(
        *f->src, exportReq(inner, outer.quad, f->dir / "edit.tif", GeometryMode::straightened), {}, {});
    ASSERT_TRUE(edit.ok()) << edit.error().message;
    EXPECT_TRUE(edit->resampled);
    auto edited = readImageFile(edit->output);
    ASSERT_TRUE(edited.ok());
    auto comp =
        compositeItem(*f->src, compositeReq(outer, inner, *edit, *edited, f->dir / "final.tif"), {}, {});
    ASSERT_TRUE(comp.ok()) << comp.error().message;
    auto out = readImageFile(comp->output);
    ASSERT_TRUE(out.ok());
    const RectI R = boundingRect(outer);
    double maxErrInside = 0;
    for (int64_t y = 0; y < out->height(); ++y)
        for (int64_t x = 0; x < out->width(); ++x) {
            const int64_t sx = x + R.x, sy = y + R.y;
            const Vec2 c{sx + 0.5, sy + 0.5};
            if (!contains(outer, c)) continue;
            for (int ch = 0; ch < 3; ++ch) {
                const int s = sample16(f->scan, sx, sy, ch), o = sample16(*out, x, y, ch);
                if (!contains(inner, c))
                    ASSERT_EQ(o, s) << "frame pixel changed at " << sx << "," << sy;
                else if (signedDistance(inner, c) > 4.0) // away from the edge (kernel support)
                    maxErrInside = std::max(maxErrInside, std::abs(o - s) / 65535.0);
            }
        }
    // two Lanczos-3 resamplings of a smooth image: well below 0.5 % of full scale
    EXPECT_LT(maxErrInside, 0.005);
    RecordProperty("straightened_roundtrip_max_error_fraction", std::to_string(maxErrInside));
}

TEST(Composite, SizeAndAspectChecks) {
    auto f = makeFixture(fk::test::makePatternImage(320, 240, 3, 16, 4));
    const Shape outer = Shape::fromQuad(rectQuad(10, 10, 300, 230));
    const Shape inner = Shape::fromQuad(rectQuad(40, 30, 260, 200)); // 220×170
    auto edit = exportRegion(*f->src, exportReq(inner, outer.quad, f->dir / "edit.tif"), {}, {});
    ASSERT_TRUE(edit.ok());
    // same aspect, half size → needs confirmation; with consent it works
    ImageBuffer half = std::move(ImageBuffer::create(110, 85, 3, 16).value());
    auto c1 = compositeReq(outer, inner, *edit, half, f->dir / "a.tif");
    auto r1 = compositeItem(*f->src, c1, {}, {});
    ASSERT_FALSE(r1.ok());
    EXPECT_EQ(r1.error().code, Errc::needs_confirmation);
    c1.adjust.allowScale = true;
    EXPECT_TRUE(compositeItem(*f->src, c1, {}, {}).ok());
    // different aspect → manual alignment required
    ImageBuffer wrong = std::move(ImageBuffer::create(200, 200, 3, 16).value());
    auto r2 = compositeItem(*f->src, compositeReq(outer, inner, *edit, wrong, f->dir / "b.tif"), {}, {});
    ASSERT_FALSE(r2.ok());
    EXPECT_EQ(r2.error().code, Errc::invalid_argument);
    // rotated in the editor: accepted with the rotate adjustment
    ImageBuffer rot = std::move(ImageBuffer::create(170, 220, 3, 16).value());
    auto c3 = compositeReq(outer, inner, *edit, rot, f->dir / "c.tif");
    const auto chk = checkEditedGeometry(edit->size, rot, 16);
    EXPECT_FALSE(chk.sizeMatches);
    EXPECT_NE(chk.suggestedRotate, 0);
    c3.adjust.rotate = chk.suggestedRotate;
    EXPECT_TRUE(compositeItem(*f->src, c3, {}, {}).ok());
}

TEST(Composite, LowerDepthNeedsConsent) {
    auto f = makeFixture(fk::test::makePatternImage(320, 240, 3, 16, 4));
    const Shape outer = Shape::fromQuad(rectQuad(10, 10, 300, 230));
    const Shape inner = Shape::fromQuad(rectQuad(40, 30, 260, 200));
    auto edit = exportRegion(*f->src, exportReq(inner, outer.quad, f->dir / "edit.tif"), {}, {});
    ASSERT_TRUE(edit.ok());
    ImageBuffer eight = fk::test::makePatternImage(220, 170, 3, 8, 2);
    auto c = compositeReq(outer, inner, *edit, eight, f->dir / "a.tif");
    auto r = compositeItem(*f->src, c, {}, {});
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, Errc::needs_confirmation);
    c.adjust.allowDepthReduction = true;
    EXPECT_TRUE(compositeItem(*f->src, c, {}, {}).ok());
}

TEST(Composite, RemoveStrokeKeepsScanPixels) {
    auto f = makeFixture(fk::test::makePatternImage(320, 240, 3, 16, 5));
    const Shape outer = Shape::fromQuad(rectQuad(10, 10, 300, 230));
    const Shape inner = Shape::fromQuad(rectQuad(40, 30, 260, 200));
    auto edit = exportRegion(*f->src, exportReq(inner, outer.quad, f->dir / "edit.tif"), {}, {});
    ASSERT_TRUE(edit.ok());
    auto edited = readImageFile(edit->output);
    ASSERT_TRUE(edited.ok());
    for (int64_t y = 0; y < edited->height(); ++y)
        std::memset(edited->row(y), 0, edited->rowBytes());
    auto c = compositeReq(outer, inner, *edit, *edited, f->dir / "final.tif");
    c.strokes.push_back({{{150, 115}, {170, 115}}, 8.0, false});
    auto comp = compositeItem(*f->src, c, {}, {});
    ASSERT_TRUE(comp.ok());
    auto out = readImageFile(comp->output);
    ASSERT_TRUE(out.ok());
    // on the stroke: scan; elsewhere inside: graded (zero)
    EXPECT_EQ(sample16(*out, 160 - 10, 115 - 10, 1), sample16(f->scan, 160, 115, 1));
    EXPECT_EQ(sample16(*out, 100 - 10, 150 - 10, 1), 0);
}

TEST(Composite, NeverOverwritesAndSourceUntouched) {
    auto f = makeFixture(fk::test::makePatternImage(320, 240, 3, 16, 6));
    const std::string before = fk::test::sha256OfFile(f->scanPath);
    const Shape outer = Shape::fromQuad(rectQuad(10, 10, 300, 230));
    const Shape inner = Shape::fromQuad(rectQuad(40, 30, 260, 200));
    auto edit = exportRegion(*f->src, exportReq(inner, outer.quad, f->dir / "edit.tif"), {}, {});
    ASSERT_TRUE(edit.ok());
    auto edited = readImageFile(edit->output);
    ASSERT_TRUE(edited.ok());
    const auto out = f->dir / "final.tif";
    ASSERT_TRUE(compositeItem(*f->src, compositeReq(outer, inner, *edit, *edited, out), {}, {}).ok());
    const std::string first = fk::test::sha256OfFile(out);
    auto again = compositeItem(*f->src, compositeReq(outer, inner, *edit, *edited, out), {}, {});
    ASSERT_FALSE(again.ok());
    EXPECT_EQ(again.error().code, Errc::exists);
    EXPECT_EQ(fk::test::sha256OfFile(out), first);
    EXPECT_EQ(fk::test::sha256OfFile(f->scanPath), before);
}

// ---- synthetic frame -----------------------------------------------------------------------------------

TEST(SyntheticFrame, LayoutModes) {
    FrameSpec s;
    s.mode = FrameSpecMode::sides;
    s.unit = FrameUnit::percent_long_edge;
    s.top = 5;
    s.right = 5;
    s.bottom = 18;
    s.left = 5;
    auto l = computeFrameLayout({1000, 800}, s);
    EXPECT_EQ(l.top, 50);
    EXPECT_EQ(l.bottom, 180);
    EXPECT_EQ(l.canvas, (SizeI{1100, 1030}));
    EXPECT_EQ(l.offsetX, 50);
    EXPECT_EQ(l.offsetY, 50);

    s.mode = FrameSpecMode::equal;
    s.unit = FrameUnit::pixels;
    s.top = 24;
    l = computeFrameLayout({300, 200}, s);
    EXPECT_EQ(l.canvas, (SizeI{348, 248}));

    s.mode = FrameSpecMode::canvas_ratio;
    s.ratioW = 4;
    s.ratioH = 5;
    s.minMargin = 10;
    for (SizeI pic : {SizeI{1000, 800}, SizeI{800, 1000}, SizeI{500, 2000}}) {
        l = computeFrameLayout(pic, s);
        EXPECT_NEAR(static_cast<double>(l.canvas.w) / l.canvas.h, 0.8, 2.0 / l.canvas.h);
        EXPECT_GE(std::min({l.left, l.right, l.top, l.bottom}), 10);
        EXPECT_LE(std::abs(l.left - l.right), 1);
        EXPECT_LE(std::abs(l.top - l.bottom), 1);
    }
}

TEST(SyntheticFrame, PictureCopiedExactlyAndColourApplied) {
    const ImageBuffer pic = fk::test::makePatternImage(120, 90, 3, 8, 11);
    FrameSpec s;
    s.unit = FrameUnit::pixels;
    s.top = 7;
    s.right = 9;
    s.bottom = 30;
    s.left = 9;
    s.color = {65535, 0, 32768};
    auto out = renderSyntheticFrame(pic, s);
    ASSERT_TRUE(out.ok());
    EXPECT_EQ(out->size(), (SizeI{138, 127}));
    for (int64_t y = 0; y < 90; ++y)
        ASSERT_EQ(0, std::memcmp(out->row(y + 7) + 9 * 3, pic.row(y), pic.rowBytes()));
    const uint8_t* corner = out->row(0);
    EXPECT_EQ(corner[0], 255);
    EXPECT_EQ(corner[1], 0);
    EXPECT_EQ(corner[2], 128);
    const uint8_t* bottom = out->row(126) + 60 * 3;
    EXPECT_EQ(bottom[0], 255);
}

TEST(SyntheticFrame, PlatformTargetHasExactSize) {
    const ImageBuffer pic = fk::test::makePatternImage(300, 200, 3, 16, 12);
    FrameSpec s;
    s.platformTarget = SizeI{1080, 1350};
    auto out = renderSyntheticFrame(pic, s);
    ASSERT_TRUE(out.ok());
    EXPECT_EQ(out->size(), (SizeI{1080, 1350}));
    EXPECT_EQ(out->bits(), 16);
}

TEST(SyntheticFrame, SampleFrameColorIsTheBandMedian) {
    ImageBuffer img = std::move(ImageBuffer::create(200, 200, 3, 16).value());
    for (int64_t y = 0; y < 200; ++y)
        for (int64_t x = 0; x < 200; ++x) {
            const bool inImage = x >= 50 && x < 150 && y >= 50 && y < 150;
            const uint16_t v[3] = {static_cast<uint16_t>(inImage ? 1000 : 60000),
                                   static_cast<uint16_t>(inImage ? 2000 : 50000),
                                   static_cast<uint16_t>(inImage ? 3000 : 40000)};
            std::memcpy(img.row(y) + x * 6, v, 6);
        }
    auto f = makeFixture(std::move(img));
    auto c = sampleFrameColor(*f->src, Shape::fromQuad(rectQuad(20, 20, 180, 180)),
                              Shape::fromQuad(rectQuad(50, 50, 150, 150)));
    ASSERT_TRUE(c.ok());
    EXPECT_EQ((*c)[0], 60000);
    EXPECT_EQ((*c)[1], 50000);
    EXPECT_EQ((*c)[2], 40000);
}
