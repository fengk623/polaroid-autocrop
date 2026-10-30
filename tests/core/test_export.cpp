// RFC-0005 / PRD F05 export tests.
#include <gtest/gtest.h>

#include "fk/color.hpp"
#include "fk/export.hpp"
#include "fk/hash.hpp"
#include "support/test_support.hpp"

#include <tiffio.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <numbers>

using namespace fk;
using fk::test::TempDir;

namespace {

struct Scan {
    TempDir dir;
    std::filesystem::path path;
    ImageBuffer img;
};

std::unique_ptr<Scan> makeScan(int ch, int bits, int compression = COMPRESSION_LZW, double dpi = 800,
                               std::vector<uint8_t> icc = Profile::adobeRGB1998().icc()) {
    auto s = std::make_unique<Scan>();
    s->img = fk::test::makePatternImage(320, 240, ch, bits, 5);
    fk::test::TestTiffOptions o;
    o.compression = compression;
    o.predictor = compression == COMPRESSION_LZW ? 2 : 1;
    o.dpi = dpi;
    if (ch >= 3) o.icc = std::move(icc);
    s->path = s->dir / "scan.tif";
    fk::test::writeTestTiff(s->path, s->img, o);
    return s;
}

ExportRequest cropRequest(const std::filesystem::path& out, double x0, double y0, double x1, double y1) {
    ExportRequest r;
    r.region = Shape::fromQuad(rectQuad(x0, y0, x1, y1));
    r.frame = r.region.quad;
    r.mode = GeometryMode::pixel_crop;
    r.output = out;
    r.write.format = FileFormat::tiff;
    r.write.compression = Compression::lzw;
    r.write.bits = 0; // = source
    r.ids = XmpIds{"proj", "scan", "item", "ev1", "inst", 1, "P01", "image_area"};
    return r;
}

} // namespace

TEST(Export, PixelCropIsBitExactAllDepths) {
    for (int bits : {8, 16})
        for (int ch : {1, 3, 4}) {
            auto s = makeScan(ch, bits);
            auto src = openTiff(s->path);
            ASSERT_TRUE(src.ok());
            const auto out = s->dir / "crop.tif";
            auto rep = exportRegion(**src, cropRequest(out, 10.2, 20.7, 150.0, 199.9), {}, {});
            ASSERT_TRUE(rep.ok()) << rep.error().message << " " << rep.error().detail;
            EXPECT_TRUE(rep->frameBitExact);
            EXPECT_FALSE(rep->resampled);
            EXPECT_EQ(rep->size, (SizeI{140, 180}));
            auto back = readImageFile(out);
            ASSERT_TRUE(back.ok());
            for (int64_t y = 0; y < 180; ++y)
                ASSERT_EQ(0, std::memcmp(back->row(y), s->img.row(20 + y) + 10 * s->img.bytesPerPixel(),
                                         140 * s->img.bytesPerPixel()))
                    << "bits=" << bits << " ch=" << ch << " y=" << y;
        }
}

TEST(Export, LosslessUprightIsAPermutation) {
    auto s = makeScan(3, 16);
    auto src = openTiff(s->path);
    ASSERT_TRUE(src.ok());
    auto req = cropRequest(s->dir / "up.tif", 30, 40, 90, 140); // 60×100
    req.upright = 1;                                            // 90° clockwise
    auto rep = exportRegion(**src, req, {}, {});
    ASSERT_TRUE(rep.ok()) << rep.error().message;
    EXPECT_TRUE(rep->frameBitExact);
    EXPECT_EQ(rep->size, (SizeI{100, 60}));
    auto back = readImageFile(s->dir / "up.tif");
    ASSERT_TRUE(back.ok());
    // output (u,v) = scan (30 + v, 40 + (100-1-u))
    for (int v = 0; v < 60; v += 7)
        for (int u = 0; u < 100; u += 9)
            for (int c = 0; c < 3; ++c)
                ASSERT_EQ(back->sample16(u, v, c), s->img.sample16(30 + v, 40 + 99 - u, c)) << u << "," << v;
}

TEST(Export, StraightenedResamplesAndIsLabelled) {
    auto s = makeScan(3, 16);
    auto src = openTiff(s->path);
    ASSERT_TRUE(src.ok());
    const double a = 2.0 * std::numbers::pi / 180;
    const Mat3 m = Mat3::translate(160, 120) * Mat3::rotate(a);
    const Quad q =
        normalizeQuad({m.apply({-80, -60}), m.apply({80, -60}), m.apply({80, 60}), m.apply({-80, 60})});
    ExportRequest req = cropRequest(s->dir / "st.tif", 0, 0, 1, 1);
    req.region = Shape::fromQuad(q);
    req.frame = q;
    req.mode = GeometryMode::straightened;
    auto rep = exportRegion(**src, req, {}, {});
    ASSERT_TRUE(rep.ok()) << rep.error().message;
    EXPECT_TRUE(rep->resampled);
    EXPECT_FALSE(rep->frameBitExact);
    EXPECT_EQ(rep->size, (SizeI{160, 120}));
}

TEST(Export, DepthReductionNeedsConsent) {
    auto s = makeScan(3, 16);
    auto src = openTiff(s->path);
    ASSERT_TRUE(src.ok());
    auto req = cropRequest(s->dir / "j.jpg", 0, 0, 64, 64);
    req.write.format = FileFormat::jpeg;
    auto r1 = exportRegion(**src, req, {}, {});
    ASSERT_FALSE(r1.ok());
    EXPECT_EQ(r1.error().code, Errc::needs_confirmation);
    EXPECT_FALSE(std::filesystem::exists(s->dir / "j.jpg"));
    req.allowDepthReduction = true;
    auto r2 = exportRegion(**src, req, {}, {});
    ASSERT_TRUE(r2.ok()) << r2.error().message;
    EXPECT_EQ(r2->bits, 8);
    EXPECT_FALSE(r2->frameBitExact);
}

TEST(Export, IccPoliciesKeepAssignConvertUntagged) {
    auto s = makeScan(3, 16);
    auto src = openTiff(s->path);
    ASSERT_TRUE(src.ok());
    const auto adobe = Profile::adobeRGB1998().icc();
    const auto srgb = Profile::sRGB().icc();
    {
        auto rep = exportRegion(**src, cropRequest(s->dir / "keep.tif", 0, 0, 32, 32), {}, {});
        ASSERT_TRUE(rep.ok());
        ImageInfo ii;
        auto b = readImageFile(s->dir / "keep.tif", &ii);
        EXPECT_EQ(ii.icc, adobe); // byte-identical
    }
    {
        auto req = cropRequest(s->dir / "assign.tif", 0, 0, 32, 32);
        req.color.kind = ColorPolicy::assign;
        req.color.icc = srgb;
        auto rep = exportRegion(**src, req, {}, {});
        ASSERT_TRUE(rep.ok());
        EXPECT_TRUE(rep->frameBitExact); // numbers unchanged
        ImageInfo ii;
        auto b = readImageFile(s->dir / "assign.tif", &ii);
        EXPECT_EQ(ii.icc, srgb);
        EXPECT_EQ(0, std::memcmp(b->row(0), s->img.row(0), 32 * 6));
    }
    {
        auto req = cropRequest(s->dir / "conv.tif", 0, 0, 32, 32);
        req.color.kind = ColorPolicy::convert;
        req.color.icc = srgb;
        auto rep = exportRegion(**src, req, {}, {});
        ASSERT_TRUE(rep.ok());
        EXPECT_FALSE(rep->frameBitExact);
        ImageInfo ii;
        auto b = readImageFile(s->dir / "conv.tif", &ii);
        EXPECT_EQ(ii.icc, srgb);
        EXPECT_NE(0, std::memcmp(b->row(0), s->img.row(0), 32 * 6));
    }
    {
        auto req = cropRequest(s->dir / "untag.tif", 0, 0, 32, 32);
        req.color.kind = ColorPolicy::untagged;
        ASSERT_TRUE(exportRegion(**src, req, {}, {}).ok());
        ImageInfo ii;
        auto b = readImageFile(s->dir / "untag.tif", &ii);
        EXPECT_TRUE(ii.icc.empty());
    }
}

TEST(Export, DpiPreservedAndScaledForStandardSize) {
    auto s = makeScan(3, 16, COMPRESSION_LZW, 1200);
    auto src = openTiff(s->path);
    ASSERT_TRUE(src.ok());
    auto rep = exportRegion(**src, cropRequest(s->dir / "a.tif", 0, 0, 100, 100), {}, {});
    ASSERT_TRUE(rep.ok());
    EXPECT_DOUBLE_EQ(rep->dpiX, 1200);
    auto req = cropRequest(s->dir / "b.tif", 0, 0, 100, 100);
    req.mode = GeometryMode::standard_size;
    req.standardSize = SizeI{50, 50};
    auto rep2 = exportRegion(**src, req, {}, {});
    ASSERT_TRUE(rep2.ok());
    EXPECT_EQ(rep2->size, (SizeI{50, 50}));
    EXPECT_NEAR(rep2->dpiX, 600, 1e-6);
}

TEST(Export, NeverOverwritesAndNeedsReview) {
    auto s = makeScan(3, 8);
    auto src = openTiff(s->path);
    ASSERT_TRUE(src.ok());
    {
        std::ofstream f(s->dir / "exists.tif");
        f << "x";
    }
    auto r = exportRegion(**src, cropRequest(s->dir / "exists.tif", 0, 0, 10, 10), {}, {});
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, Errc::exists);
    auto req = cropRequest(s->dir / "rev.tif", 0, 0, 10, 10);
    req.reviewed = false;
    auto r2 = exportRegion(**src, req, {}, {});
    ASSERT_FALSE(r2.ok());
    EXPECT_EQ(r2.error().code, Errc::needs_review);
    int v = 0;
    const auto p = nextFreeVersionPath(s->dir.path(), "scan_P01_EDIT", "tif", &v);
    EXPECT_EQ(v, 1);
    EXPECT_EQ(p.filename().string(), "scan_P01_EDIT_v001.tif");
}

TEST(Export, PngSixteenBitExactAndXmp) {
    auto s = makeScan(3, 16);
    auto src = openTiff(s->path);
    ASSERT_TRUE(src.ok());
    auto req = cropRequest(s->dir / "a.png", 5, 5, 105, 55);
    req.write.format = FileFormat::png;
    auto rep = exportRegion(**src, req, {}, {});
    ASSERT_TRUE(rep.ok()) << rep.error().message;
    EXPECT_TRUE(rep->frameBitExact);
    MetadataSet meta;
    auto b = readImageFile(s->dir / "a.png", nullptr, &meta);
    ASSERT_TRUE(b.ok());
    EXPECT_EQ(b->bits(), 16);
    EXPECT_EQ(parseXmpIds(meta.sourceXmp).editVersionId, "ev1");
    EXPECT_EQ(0, std::memcmp(b->row(0), s->img.row(5) + 5 * 6, 100 * 6));
}

TEST(Export, LinearDngStructure) {
    auto s = makeScan(3, 16);
    auto src = openTiff(s->path);
    ASSERT_TRUE(src.ok());
    auto req = cropRequest(s->dir / "a.dng", 0, 0, 64, 48);
    req.write.format = FileFormat::dng;
    req.write.compression = Compression::none;
    auto rep = exportRegion(**src, req, {}, {});
    ASSERT_TRUE(rep.ok()) << rep.error().message << " " << rep.error().detail;
    TIFF* t = TIFFOpen((s->dir / "a.dng").string().c_str(), "r");
    ASSERT_NE(t, nullptr);
    uint16_t photo = 0;
    TIFFGetField(t, TIFFTAG_PHOTOMETRIC, &photo);
    EXPECT_EQ(photo, 34892);
    uint16_t n = 0;
    float* cm = nullptr;
    EXPECT_TRUE(TIFFGetField(t, TIFFTAG_COLORMATRIX1, &n, &cm));
    EXPECT_EQ(n, 9);
    TIFFClose(t);
}

TEST(Export, SourceFileUntouched) {
    auto s = makeScan(3, 16);
    const auto before = fk::test::sha256OfFile(s->path);
    {
        auto src = openTiff(s->path);
        ASSERT_TRUE(src.ok());
        ASSERT_TRUE(exportRegion(**src, cropRequest(s->dir / "o.tif", 0, 0, 50, 50), {}, {}).ok());
    }
    EXPECT_EQ(before, fk::test::sha256OfFile(s->path));
}

// ---- FK-004 (U009): privacy presets on every output format, read back independently
// ---------------------------

namespace {
/// A scan carrying identity in every channel FrameKeeper reads: TIFF Artist/HostComputer and an XMP
/// dc:creator.
std::filesystem::path scanWithIdentity(const std::filesystem::path& dir) {
    const auto p = dir / "identity.tif";
    const ImageBuffer img = fk::test::makePatternImage(200, 150, 3, 8, 3);
    TIFF* t = TIFFOpen(p.string().c_str(), "w");
    EXPECT_NE(t, nullptr);
    TIFFSetField(t, TIFFTAG_IMAGEWIDTH, 200u);
    TIFFSetField(t, TIFFTAG_IMAGELENGTH, 150u);
    TIFFSetField(t, TIFFTAG_SAMPLESPERPIXEL, 3);
    TIFFSetField(t, TIFFTAG_BITSPERSAMPLE, 8);
    TIFFSetField(t, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
    TIFFSetField(t, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField(t, TIFFTAG_ROWSPERSTRIP, 16u);
    TIFFSetField(t, TIFFTAG_MAKE, "EPSON");
    TIFFSetField(t, TIFFTAG_MODEL, "Perfection V750");
    TIFFSetField(t, TIFFTAG_ARTIST, "Ann Example");
    TIFFSetField(t, TIFFTAG_HOSTCOMPUTER, "ann-host-1234");
    TIFFSetField(t, TIFFTAG_COPYRIGHT, "(c) Rights Holder");
    const std::string xmp = "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF><rdf:Description>"
                            "<dc:creator><rdf:Seq><rdf:li>Ann Example</rdf:li></rdf:Seq></dc:creator>"
                            "<dc:title><rdf:Alt><rdf:li>Beach day</rdf:li></rdf:Alt></dc:title>"
                            "</rdf:Description></rdf:RDF></x:xmpmeta>";
    TIFFSetField(t, TIFFTAG_XMLPACKET, static_cast<uint32_t>(xmp.size()), xmp.data());
    const std::vector<uint8_t> icc = Profile::sRGB().icc(); // DNG export needs the scan's profile
    TIFFSetField(t, TIFFTAG_ICCPROFILE, static_cast<uint32_t>(icc.size()), icc.data());
    for (uint32_t y = 0; y < 150; ++y)
        TIFFWriteScanline(t, const_cast<uint8_t*>(img.row(y)), y, 0);
    TIFFClose(t);
    return p;
}

std::string fileBytes(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}
} // namespace

TEST(Privacy, RemoveIdentityLeavesNoIdentityInAnyFormat) {
    TempDir dir;
    auto src = openTiff(scanWithIdentity(dir.path()));
    ASSERT_TRUE(src.ok());
    EXPECT_EQ((*src)->metadata().artist, "Ann Example");
    struct Case {
        FileFormat fmt;
        const char* ext;
    };
    for (const Case c : {Case{FileFormat::tiff, "tif"}, Case{FileFormat::png, "png"},
                         Case{FileFormat::jpeg, "jpg"}, Case{FileFormat::dng, "dng"}})
        for (PrivacyPreset preset :
             {PrivacyPreset::keep_all_safe, PrivacyPreset::remove_location, PrivacyPreset::remove_identity}) {
            ExportRequest r = cropRequest(
                dir / (std::string("out_") + privacyPresetName(preset) + "." + c.ext), 10, 10, 190, 140);
            r.write.format = c.fmt;
            r.privacy = preset;
            if (c.fmt == FileFormat::jpeg) r.allowDepthReduction = true;
            auto rep = exportRegion(**src, r, {}, {});
            ASSERT_TRUE(rep.ok()) << c.ext << " " << rep.error().message;
            // independent read-back: the raw bytes of the written file
            const std::string bytes = fileBytes(rep->output);
            const bool identityGone = preset == PrivacyPreset::remove_identity;
            EXPECT_EQ(bytes.find("Ann Example") == std::string::npos, identityGone)
                << c.ext << " " << privacyPresetName(preset);
            const bool tiffTags = c.fmt == FileFormat::tiff || c.fmt == FileFormat::dng;
            EXPECT_EQ(bytes.find("ann-host-1234") != std::string::npos, tiffTags && !identityGone) << c.ext;
            // safe fields stay (title in XMP everywhere; scanner make in TIFF/DNG tags)
            EXPECT_NE(bytes.find("Beach day"), std::string::npos) << c.ext;
            if (c.fmt == FileFormat::tiff) {
                EXPECT_NE(bytes.find("EPSON"), std::string::npos) << c.ext;
            }
            // the list shown to the user matches what was written
            const MetadataSet filtered = applyPrivacy((*src)->metadata(), preset);
            const auto listed = writtenMetadata(filtered, c.fmt == FileFormat::tiff  ? TagChannel::tiff
                                                          : c.fmt == FileFormat::dng ? TagChannel::dng
                                                                                     : TagChannel::none);
            for (const auto& f : listed)
                EXPECT_NE(bytes.find(f.value), std::string::npos)
                    << c.ext << " lists " << f.name << " = " << f.value;
            const bool listsIdentity = std::any_of(listed.begin(), listed.end(), [](const MetadataField& f) {
                return f.cls == PrivacyClass::identity;
            });
            EXPECT_EQ(listsIdentity, !identityGone) << c.ext;
        }
}
