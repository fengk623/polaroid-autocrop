// RFC-0003 / PRD F01 tests: every TIFF layout reads back exactly; writers round-trip; safety rules.
#include <gtest/gtest.h>

#include "fk/color.hpp"
#include "fk/image_io.hpp"
#include "support/test_support.hpp"

#include <tiffio.h>

#include <fstream>

using namespace fk;
using fk::test::TempDir;

namespace {

ImageBuffer readAll(ImageSource& s) {
    const auto& i = s.info();
    auto b = ImageBuffer::create(i.width, i.height, i.channels, i.bitsPerSample);
    EXPECT_TRUE(b.ok());
    // Deliberately odd band sizes to cross strip/tile boundaries.
    for (int64_t y = 0; y < i.height; y += 7) {
        const int64_t n = std::min<int64_t>(7, i.height - y);
        auto st = s.readRows(y, n, 0, i.width, b->row(y), b->rowBytes());
        EXPECT_TRUE(st.ok()) << (st.ok() ? "" : st.error().detail);
    }
    return std::move(b.value());
}

struct Case {
    int channels, bits, compression, predictor;
    fk::test::TiffLayout layout;
    bool planar;
};

} // namespace

class TiffReadLayouts : public ::testing::TestWithParam<Case> {};

TEST_P(TiffReadLayouts, ReadsBackExactly) {
    const Case c = GetParam();
    TempDir dir;
    const auto img = fk::test::makePatternImage(53, 41, c.channels, c.bits, 7);
    fk::test::TestTiffOptions o;
    o.layout = c.layout;
    o.compression = c.compression;
    o.predictor = c.predictor;
    o.planarSeparate = c.planar;
    o.rowsPerStrip = 5;
    o.tile = 16;
    const auto p = dir / "in.tif";
    fk::test::writeTestTiff(p, img, o);
    auto src = openTiff(p);
    ASSERT_TRUE(src.ok()) << src.error().message << " " << src.error().detail;
    EXPECT_EQ((*src)->info().width, 53);
    EXPECT_EQ((*src)->info().channels, c.channels);
    const auto back = readAll(**src);
    EXPECT_TRUE(fk::test::sameSamples(img, back));
    // Partial-width requests.
    std::vector<uint8_t> row(static_cast<size_t>(10) * img.bytesPerPixel());
    ASSERT_TRUE((*src)->readRows(40, 1, 30, 10, row.data(), row.size()).ok());
    EXPECT_EQ(0, std::memcmp(row.data(), img.row(40) + 30 * img.bytesPerPixel(), row.size()));
}

INSTANTIATE_TEST_SUITE_P(
    AllLayouts, TiffReadLayouts,
    ::testing::Values(Case{3, 8, COMPRESSION_NONE, 1, fk::test::TiffLayout::strips, false},
                      Case{3, 16, COMPRESSION_LZW, 2, fk::test::TiffLayout::strips, false},
                      Case{3, 16, COMPRESSION_ADOBE_DEFLATE, 2, fk::test::TiffLayout::strips, false},
                      Case{3, 8, COMPRESSION_PACKBITS, 1, fk::test::TiffLayout::strips, false},
                      Case{1, 16, COMPRESSION_LZW, 2, fk::test::TiffLayout::strips, false},
                      Case{4, 8, COMPRESSION_LZW, 1, fk::test::TiffLayout::strips, false},
                      Case{3, 16, COMPRESSION_LZW, 2, fk::test::TiffLayout::single_strip, false},
                      Case{3, 8, COMPRESSION_NONE, 1, fk::test::TiffLayout::single_strip, false},
                      Case{3, 16, COMPRESSION_ADOBE_DEFLATE, 1, fk::test::TiffLayout::tiles, false},
                      Case{3, 8, COMPRESSION_NONE, 1, fk::test::TiffLayout::tiles, true},
                      Case{3, 16, COMPRESSION_LZW, 1, fk::test::TiffLayout::strips, true},
                      Case{3, 16, COMPRESSION_LZW, 1, fk::test::TiffLayout::single_strip, true},
                      Case{2, 8, COMPRESSION_NONE, 1, fk::test::TiffLayout::strips, false}));

TEST(TiffRead, AccessPathSelection) {
    TempDir dir;
    const auto img = fk::test::makePatternImage(64, 64, 3, 16);
    fk::test::TestTiffOptions o;
    o.compression = COMPRESSION_LZW;
    o.layout = fk::test::TiffLayout::tiles;
    fk::test::writeTestTiff(dir / "t.tif", img, o);
    EXPECT_EQ((*openTiff(dir / "t.tif"))->accessPath(), TiffAccessPath::tiles);
    o.layout = fk::test::TiffLayout::strips;
    fk::test::writeTestTiff(dir / "s.tif", img, o);
    EXPECT_EQ((*openTiff(dir / "s.tif"))->accessPath(), TiffAccessPath::strips);
}

TEST(TiffRead, MinIsWhiteIsInvertedOnRead) {
    TempDir dir;
    const auto img = fk::test::makePatternImage(20, 9, 1, 16);
    fk::test::TestTiffOptions o;
    o.minIsWhite = true;
    fk::test::writeTestTiff(dir / "w.tif", img, o);
    auto src = openTiff(dir / "w.tif");
    ASSERT_TRUE(src.ok());
    EXPECT_EQ((*src)->info().photometric, Photometric::min_is_white);
    EXPECT_TRUE(fk::test::sameSamples(img, readAll(**src)));
}

TEST(TiffRead, InfoDpiAndIcc) {
    TempDir dir;
    const auto img = fk::test::makePatternImage(16, 16, 3, 8);
    fk::test::TestTiffOptions o;
    o.dpi = 1600;
    o.icc = Profile::adobeRGB1998().icc();
    fk::test::writeTestTiff(dir / "i.tif", img, o);
    auto src = openTiff(dir / "i.tif");
    ASSERT_TRUE(src.ok());
    EXPECT_DOUBLE_EQ((*src)->info().dpiX, 1600);
    EXPECT_EQ((*src)->info().icc, o.icc);
    EXPECT_NE((*src)->info().iccDescription.find("Adobe RGB"), std::string::npos);
}

TEST(TiffRead, BigTiff) {
    TempDir dir;
    const auto img = fk::test::makePatternImage(33, 17, 3, 16);
    fk::test::TestTiffOptions o;
    o.bigTiff = true;
    o.compression = COMPRESSION_LZW;
    fk::test::writeTestTiff(dir / "b.tif", img, o);
    auto src = openTiff(dir / "b.tif");
    ASSERT_TRUE(src.ok());
    EXPECT_TRUE((*src)->info().bigTiff);
    EXPECT_TRUE(fk::test::sameSamples(img, readAll(**src)));
}

TEST(TiffRead, UnsupportedAndMissing) {
    TempDir dir;
    EXPECT_EQ(openTiff(dir / "nope.tif").error().code, Errc::not_found);
    {
        std::ofstream f(dir / "junk.tif", std::ios::binary);
        f << "not a tiff at all";
    }
    EXPECT_EQ(openTiff(dir / "junk.tif").error().code, Errc::unsupported_format);
    // palette TIFF
    TIFF* t = TIFFOpen((dir / "pal.tif").string().c_str(), "w");
    TIFFSetField(t, TIFFTAG_IMAGEWIDTH, 4u);
    TIFFSetField(t, TIFFTAG_IMAGELENGTH, 4u);
    TIFFSetField(t, TIFFTAG_SAMPLESPERPIXEL, 1);
    TIFFSetField(t, TIFFTAG_BITSPERSAMPLE, 8);
    TIFFSetField(t, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_PALETTE);
    uint16_t map[256] = {0};
    TIFFSetField(t, TIFFTAG_COLORMAP, map, map, map);
    uint8_t row[4] = {0};
    for (uint32_t y = 0; y < 4; ++y)
        TIFFWriteScanline(t, row, y, 0);
    TIFFClose(t);
    auto r = openTiff(dir / "pal.tif");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, Errc::unsupported_format);
}

TEST(TiffRead, TruncatedFileIsReportedDamaged) {
    TempDir dir;
    const auto img = fk::test::makePatternImage(64, 64, 3, 16);
    fk::test::TestTiffOptions o;
    o.compression = COMPRESSION_NONE;
    fk::test::writeTestTiff(dir / "full.tif", img, o);
    const auto size = std::filesystem::file_size(dir / "full.tif");
    std::filesystem::copy_file(dir / "full.tif", dir / "cut.tif");
    std::filesystem::resize_file(dir / "cut.tif", size / 2);
    auto rep = checkTiffIntegrity(dir / "cut.tif");
    ASSERT_TRUE(rep.ok());
    EXPECT_FALSE(rep->ok);
    auto good = checkTiffIntegrity(dir / "full.tif");
    ASSERT_TRUE(good.ok());
    EXPECT_TRUE(good->ok);
}

TEST(TiffRead, UnicodePaths) {
    TempDir dir;
    const auto img = fk::test::makePatternImage(8, 8, 3, 8);
    for (const char* name : {"中文 扫描 01.tif", "日本語スキャン.tif", "emoji 📷 scan.tif"}) {
        const auto p = dir / name;
        auto opt = WriteOptions{};
        opt.format = FileFormat::tiff;
        opt.bits = 8;
        auto sink = createSink(p, opt);
        ASSERT_TRUE(sink.ok()) << name;
        ASSERT_TRUE((*sink)->begin(img.size(), 3, 8).ok());
        ASSERT_TRUE((*sink)->writeRows(img.data(), img.height(), img.rowBytes()).ok());
        ASSERT_TRUE((*sink)->finish().ok());
        auto src = openTiff(p);
        ASSERT_TRUE(src.ok()) << name;
        EXPECT_TRUE(fk::test::sameSamples(img, readAll(**src)));
    }
}

class TiffWriteRoundTrip : public ::testing::TestWithParam<std::tuple<int, int, Compression>> {};

TEST_P(TiffWriteRoundTrip, ExactAndTagged) {
    const auto [channels, bits, comp] = GetParam();
    TempDir dir;
    const auto img = fk::test::makePatternImage(301, 77, channels, bits, 3);
    WriteOptions o;
    o.format = FileFormat::tiff;
    o.bits = bits;
    o.compression = comp;
    o.dpiX = o.dpiY = 1200;
    if (channels >= 3) o.icc = Profile::sRGB().icc();
    o.xmp = buildXmp(XmpIds{"p", "s", "i", "e", "x", 1, "P01", "image_area"});
    const auto p = dir / "out.tif";
    auto sink = createSink(p, o);
    ASSERT_TRUE(sink.ok());
    ASSERT_TRUE((*sink)->begin(img.size(), channels, bits).ok());
    for (int64_t y = 0; y < img.height(); y += 10) {
        const int64_t n = std::min<int64_t>(10, img.height() - y);
        ASSERT_TRUE((*sink)->writeRows(img.row(y), n, img.rowBytes()).ok());
    }
    ASSERT_TRUE((*sink)->finish().ok());
    EXPECT_FALSE(std::filesystem::exists(p.string() + ".fk-tmp"));
    auto src = openTiff(p);
    ASSERT_TRUE(src.ok());
    EXPECT_TRUE(fk::test::sameSamples(img, readAll(**src)));
    EXPECT_DOUBLE_EQ((*src)->info().dpiX, 1200);
    EXPECT_EQ((*src)->info().icc, o.icc);
    EXPECT_EQ(parseXmpIds((*src)->metadata().sourceXmp).itemId, "i");
}

INSTANTIATE_TEST_SUITE_P(Writers, TiffWriteRoundTrip,
                         ::testing::Combine(::testing::Values(1, 3, 4), ::testing::Values(8, 16),
                                            ::testing::Values(Compression::none, Compression::lzw,
                                                              Compression::deflate, Compression::packbits)));

TEST(TiffWrite, NeverOverwritesAndAbortRemovesTemp) {
    TempDir dir;
    const auto p = dir / "x.tif";
    {
        std::ofstream f(p);
        f << "existing";
    }
    WriteOptions o;
    auto s = createSink(p, o);
    ASSERT_FALSE(s.ok());
    EXPECT_EQ(s.error().code, Errc::exists);
    auto s2 = createSink(dir / "y.tif", o);
    ASSERT_TRUE(s2.ok());
    ASSERT_TRUE((*s2)->begin({4, 4}, 3, 16).ok());
    (*s2)->abort();
    int files = 0;
    for (auto& e : std::filesystem::directory_iterator(dir.path())) {
        (void)e;
        ++files;
    }
    EXPECT_EQ(files, 1); // only the pre-existing x.tif
}

// Regression: compressed single-strip TIFFs can only be decoded sequentially; out-of-order and
// forward-skipping row requests must still return the right rows (red before the fix: "Compression algorithm
// does not support random access" on fkfixture corpus scan_0005, single-strip Deflate).
TEST(TiffRead, SingleStripCompressedRandomRowOrder) {
    for (int comp : {COMPRESSION_ADOBE_DEFLATE, COMPRESSION_LZW}) {
        TempDir dir;
        const auto img = fk::test::makePatternImage(37, 61, 3, 16, 9);
        fk::test::TestTiffOptions o;
        o.layout = fk::test::TiffLayout::single_strip;
        o.compression = comp;
        o.predictor = 2;
        fk::test::writeTestTiff(dir / "s.tif", img, o);
        TiffOpenOptions opt;
        opt.maxCachedStripBytes = 1024; // force the sequential scanline cursor
        auto src = openTiff(dir / "s.tif", opt);
        ASSERT_TRUE(src.ok());
        ASSERT_EQ((*src)->accessPath(), TiffAccessPath::scanlines);
        std::vector<uint8_t> row(img.rowBytes());
        for (int64_t y : {30, 5, 31, 0, 60, 59, 12}) {
            auto st = (*src)->readRows(y, 1, 0, 37, row.data(), row.size());
            ASSERT_TRUE(st.ok()) << "comp=" << comp << " y=" << y << " " << st.error().detail;
            EXPECT_EQ(0, std::memcmp(row.data(), img.row(y), row.size())) << "comp=" << comp << " y=" << y;
        }
        EXPECT_GE((*src)->restartCount(), 3u);
    }
}
