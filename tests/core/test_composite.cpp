// RFC-0007 / RFC-0008 / PRD F07–F08: compositing into the real scanned frame, synthetic frames.
#include <gtest/gtest.h>

#include "fk/color.hpp"
#include "fk/composite.hpp"
#include "fk/hash.hpp"
#include "fk/project.hpp"
#include "support/test_support.hpp"

#include <tiffio.h>

#include <algorithm>
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

// ---- FK-003: graded files without a usable colour profile (RFC-0012 §Graded files without a usable profile)
// -------

namespace {
double meanAbsDiffInside(const ImageBuffer& a, const ImageBuffer& b, RectI r) {
    double sum = 0;
    int64_t n = 0;
    for (int64_t y = r.y; y < r.bottom(); ++y)
        for (int64_t x = r.x; x < r.right(); ++x)
            for (int c = 0; c < 3; ++c) {
                sum += std::abs(static_cast<double>(sample16(a, x, y, c)) - sample16(b, x, y, c));
                ++n;
            }
    return n ? sum / n / 65535.0 : 1.0;
}
} // namespace

TEST(GradedProfile, AnUntaggedGradedFileIsInterpretedAsConfirmed) {
    // Adobe RGB scan → sRGB export → the editor drops the profile → relinked
    TempDir dir;
    // muted colours: inside the sRGB gamut, so Adobe RGB → sRGB → Adobe RGB is reversible up to rounding
    ImageBuffer scan = std::move(ImageBuffer::create(320, 240, 3, 16).value());
    for (int64_t y = 0; y < 240; ++y)
        for (int64_t x = 0; x < 320; ++x) {
            const double base = 0.35 + 0.3 * std::sin(x * 0.03) * std::cos(y * 0.02);
            const uint16_t v[3] = {static_cast<uint16_t>((base + 0.04 * std::sin(y * 0.05)) * 65535),
                                   static_cast<uint16_t>(base * 65535),
                                   static_cast<uint16_t>((base - 0.04 * std::cos(x * 0.04)) * 65535)};
            std::memcpy(scan.row(y) + x * 6, v, 6);
        }
    fk::test::TestTiffOptions o;
    o.icc = Profile::adobeRGB1998().icc();
    o.dpi = 600;
    fk::test::writeTestTiff(dir / "scan.tif", scan, o);
    auto src = openTiff(dir / "scan.tif");
    ASSERT_TRUE(src.ok());
    const std::vector<uint8_t> scanIcc = (*src)->info().icc;
    ASSERT_FALSE(scanIcc.empty());
    const Shape outer = Shape::fromQuad(rectQuad(10, 10, 300, 230));
    const Shape inner = Shape::fromQuad(rectQuad(40, 30, 260, 200));
    ExportRequest r = exportReq(inner, outer.quad, dir / "edit.tif");
    r.color.kind = ColorPolicy::convert;
    r.color.icc = Profile::sRGB().icc();
    auto edit = exportRegion(**src, r, {}, {});
    ASSERT_TRUE(edit.ok()) << edit.error().message;
    ImageInfo ei;
    auto edited = readImageFile(edit->output, &ei);
    ASSERT_TRUE(edited.ok());
    ei.icc.clear(); // what the editor wrote: no profile

    Sidecar rec;
    rec.iccSha256 = edit->iccSha256;
    rec.iccDescription = edit->iccDescription;
    rec.exportSize = edit->size;
    rec.bits = 16;
    Project p;
    // the choices: the export's own profile identified by its hash (sRGB), then scan / built-ins without
    // duplicates
    const auto choices = iccChoicesFor(rec, scanIcc, p.iccProfiles);
    ASSERT_GE(choices.size(), 2u);
    EXPECT_EQ(choices[0].id, "export");
    EXPECT_EQ(choices[0].sha256, Profile::sRGB().sha256());
    EXPECT_EQ(
        std::count_if(choices.begin(), choices.end(), [](const IccChoice& c) { return c.id == "srgb"; }), 0)
        << "sRGB is already listed as the export profile";

    // never guessed: a new relink without a confirmed interpretation is refused
    RelinkTransform t;
    t.set = true;
    auto refused = gradedProfile(ei, t, rec, scanIcc, p.iccProfiles);
    ASSERT_FALSE(refused.ok());
    EXPECT_EQ(refused.error().code, Errc::needs_confirmation);
    // confirmed "export": stored with the project, resolved by hash
    t.iccSource = "export";
    t.iccSha256 = choices[0].sha256;
    p.iccProfiles[t.iccSha256] = choices[0].icc;
    auto icc = gradedProfile(ei, t, rec, scanIcc, p.iccProfiles);
    ASSERT_TRUE(icc.ok()) << icc.error().message;
    EXPECT_EQ(*icc, Profile::sRGB().icc());

    auto composite = [&](const std::vector<uint8_t>& editedIcc, const char* name) {
        CompositeRequest c = compositeReq(outer, inner, *edit, *edited, dir / name);
        c.editedIcc = editedIcc;
        auto rep = compositeItem(**src, c, {}, {});
        EXPECT_TRUE(rep.ok()) << (rep.ok() ? "" : rep.error().message);
        auto out = readImageFile(dir / name);
        EXPECT_TRUE(out.ok());
        return std::move(out.value());
    };
    auto direct = exportRegion(**src, exportReq(outer, outer.quad, dir / "direct.tif"), {}, {});
    ASSERT_TRUE(direct.ok());
    auto print = readImageFile(dir / "direct.tif");
    ASSERT_TRUE(print.ok());
    const RectI innerOut{30, 20, 220, 170}; // inner rect relative to the outer crop
    const ImageBuffer right = composite(*icc, "right.tif");
    const ImageBuffer wrong =
        composite({}, "wrong.tif"); // the old behaviour: assumed to be in the scan's space
    const double eRight = meanAbsDiffInside(right, *print, innerOut);
    const double eWrong = meanAbsDiffInside(wrong, *print, innerOut);
    RecordProperty("fk003_mean_abs_error_confirmed", std::to_string(eRight));
    RecordProperty("fk003_mean_abs_error_assumed_scan", std::to_string(eWrong));
    EXPECT_LT(eRight, 0.001) << "sRGB → Adobe RGB once restores the scan's colours";
    EXPECT_GT(eWrong, 0.005) << "assuming the scan's space shifts the colours";
    // the frame stays the scan's own pixels in both
    const RectI top{0, 0, right.width(), 20};
    EXPECT_EQ(meanAbsDiffInside(right, *print, top), 0.0);
    EXPECT_EQ(meanAbsDiffInside(wrong, *print, top), 0.0);
}

TEST(GradedProfile, UnreadableOrUnknownProfilesAreNotGuessed) {
    Sidecar rec;
    rec.iccSha256 = std::string(64, 'c'); // an export profile this project cannot identify
    rec.exportSize = {10, 10};
    Project p;
    const std::vector<uint8_t> scanIcc = Profile::adobeRGB1998().icc();
    const auto choices = iccChoicesFor(rec, scanIcc, p.iccProfiles);
    EXPECT_TRUE(
        std::none_of(choices.begin(), choices.end(), [](const IccChoice& c) { return c.id == "export"; }));
    EXPECT_EQ(choices.front().id, "scan");
    // an earlier version's relink without a profile: refused instead of assuming anything
    ImageInfo noIcc;
    EXPECT_FALSE(gradedProfile(noIcc, RelinkTransform{}, rec, scanIcc, p.iccProfiles).ok());
    // an unreadable embedded profile
    ImageInfo bad;
    bad.icc = {1, 2, 3, 4, 5, 6, 7, 8};
    auto r = gradedProfile(bad, RelinkTransform{}, rec, scanIcc, p.iccProfiles);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, Errc::needs_confirmation);
    auto img = ImageBuffer::create(10, 10, 3, 16);
    ASSERT_TRUE(img.ok());
    const RelinkCheck chk = checkEdited(rec, *img, bad);
    EXPECT_TRUE(chk.iccInvalid);
    EXPECT_NE(std::find(chk.actionsNeeded.begin(), chk.actionsNeeded.end(), "choose_icc"),
              chk.actionsNeeded.end());
    // an untagged export needs nothing: its pixels are in the scan's space
    Sidecar untagged = rec;
    untagged.iccSha256.clear();
    auto none = gradedProfile(noIcc, RelinkTransform{}, untagged, scanIcc, p.iccProfiles);
    ASSERT_TRUE(none.ok());
    EXPECT_TRUE(none->empty());
    // a chosen profile whose bytes are gone is an error, not a fallback
    RelinkTransform t;
    t.set = true;
    t.iccSource = "file";
    t.iccSha256 = std::string(64, 'd');
    EXPECT_FALSE(gradedProfile(noIcc, t, rec, scanIcc, p.iccProfiles).ok());
}

TEST(GradedProfile, ChosenProfilesAreSavedWithTheProject) {
    TempDir dir;
    Project p = newProject("icc");
    const auto bytes = Profile::adobeRGB1998().icc();
    p.iccProfiles[Profile::adobeRGB1998().sha256()] = bytes;
    ASSERT_TRUE(saveProject(p, dir / "p.fkproj").ok());
    auto back = loadProject(dir / "p.fkproj");
    ASSERT_TRUE(back.ok());
    ASSERT_EQ(back->iccProfiles.size(), 1u);
    EXPECT_EQ(back->iccProfiles.begin()->second, bytes);
    EXPECT_TRUE(back->unknownJson.empty()) << "iccProfiles is a known key";
}
