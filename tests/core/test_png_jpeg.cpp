// RFC-0003: PNG and JPEG writers/readers keep pixels (PNG exact), ICC, DPI and XMP.
#include <gtest/gtest.h>

#include "fk/color.hpp"
#include "fk/image_io.hpp"
#include "support/test_support.hpp"

#include <cmath>

using namespace fk;
using fk::test::TempDir;

namespace {
Status writeImage(const std::filesystem::path& p, const ImageBuffer& img, const WriteOptions& o) {
    auto s = createSink(p, o);
    if (!s) return s.error();
    FK_TRY((*s)->begin(img.size(), img.channels(), img.bits()));
    FK_TRY((*s)->writeRows(img.data(), img.height(), img.rowBytes()));
    return (*s)->finish();
}
} // namespace

class PngRoundTrip : public ::testing::TestWithParam<std::tuple<int, int>> {};

TEST_P(PngRoundTrip, ExactWithMetadata) {
    const auto [ch, bits] = GetParam();
    TempDir dir;
    const auto img = fk::test::makePatternImage(97, 41, ch, bits, 11);
    WriteOptions o;
    o.format = FileFormat::png;
    o.bits = bits;
    o.dpiX = o.dpiY = 800;
    if (ch >= 3) o.icc = Profile::adobeRGB1998().icc();
    o.xmp = buildXmp(XmpIds{"p", "s", "item-7", "ev", "", 2, "P02", "image_area"});
    ASSERT_TRUE(writeImage(dir / "a.png", img, o).ok());
    ImageInfo ii;
    MetadataSet meta;
    auto back = readImageFile(dir / "a.png", &ii, &meta);
    ASSERT_TRUE(back.ok()) << back.error().detail;
    EXPECT_TRUE(fk::test::sameSamples(img, *back));
    EXPECT_NEAR(ii.dpiX, 800, 0.05);
    EXPECT_EQ(ii.icc, o.icc);
    EXPECT_EQ(parseXmpIds(meta.sourceXmp).itemId, "item-7");
}

INSTANTIATE_TEST_SUITE_P(Png, PngRoundTrip,
                         ::testing::Combine(::testing::Values(1, 2, 3, 4), ::testing::Values(8, 16)));

TEST(Jpeg, RoundTripCloseWithMetadata) {
    TempDir dir;
    auto r = ImageBuffer::create(64, 48, 3, 8);
    ASSERT_TRUE(r.ok());
    ImageBuffer img = std::move(r.value());
    for (int y = 0; y < 48; ++y)
        for (int x = 0; x < 64; ++x) {
            img.row(y)[x * 3] = static_cast<uint8_t>(x * 4);
            img.row(y)[x * 3 + 1] = static_cast<uint8_t>(y * 5);
            img.row(y)[x * 3 + 2] = 128;
        }
    WriteOptions o;
    o.format = FileFormat::jpeg;
    o.bits = 8;
    o.dpiX = o.dpiY = 300;
    o.icc = Profile::sRGB().icc();
    o.xmp = buildXmp(XmpIds{"p", "s", "item-9", "ev", "", 1, "P01", "image_area"});
    ASSERT_TRUE(writeImage(dir / "a.jpg", img, o).ok());
    ImageInfo ii;
    MetadataSet meta;
    auto back = readImageFile(dir / "a.jpg", &ii, &meta);
    ASSERT_TRUE(back.ok());
    double maxErr = 0;
    for (int y = 0; y < 48; ++y)
        for (int x = 0; x < 64 * 3; ++x)
            maxErr = std::max(maxErr, std::abs(double(img.row(y)[x]) - double(back->row(y)[x])));
    EXPECT_LE(maxErr, 6.0); // q100 4:4:4 on a smooth gradient
    EXPECT_EQ(ii.icc, o.icc);
    EXPECT_DOUBLE_EQ(ii.dpiX, 300);
    EXPECT_EQ(parseXmpIds(meta.sourceXmp).itemId, "item-9");
}

TEST(Jpeg, RejectsSixteenBit) {
    TempDir dir;
    WriteOptions o;
    o.format = FileFormat::jpeg;
    auto s = createSink(dir / "x.jpg", o);
    ASSERT_TRUE(s.ok());
    EXPECT_FALSE((*s)->begin({4, 4}, 3, 16).ok());
}
