// RFC-0006 / ADR-0009 / PRD F06: content signatures and file-name matching.
#include <gtest/gtest.h>

#include "fk/relink.hpp"
#include "support/test_support.hpp"

#include <cmath>
#include <cstring>

using namespace fk;

namespace {

/// Smooth 16-bit RGB "photo" with structure at several scales (seeded).
ImageBuffer photo(int64_t w, int64_t h, double phase) {
    ImageBuffer img = std::move(ImageBuffer::create(w, h, 3, 16).value());
    for (int64_t y = 0; y < h; ++y)
        for (int64_t x = 0; x < w; ++x) {
            const double u = static_cast<double>(x) / w, v = static_cast<double>(y) / h;
            const double l = 0.5 + 0.3 * std::sin(9 * u + phase) * std::cos(5 * v - phase) +
                             0.15 * std::sin(31 * u * v + phase);
            const uint16_t s[3] = {static_cast<uint16_t>(std::clamp(l, 0.0, 1.0) * 65535),
                                   static_cast<uint16_t>(std::clamp(l * 0.8 + 0.1 * u, 0.0, 1.0) * 65535),
                                   static_cast<uint16_t>(std::clamp(1.0 - l * 0.7, 0.0, 1.0) * 65535)};
            std::memcpy(img.row(y) + x * 6, s, 6);
        }
    return img;
}

ImageBuffer rotate90(const ImageBuffer& in) { // clockwise
    ImageBuffer out =
        std::move(ImageBuffer::create(in.height(), in.width(), in.channels(), in.bits()).value());
    const size_t px = in.bytesPerPixel();
    for (int64_t y = 0; y < in.height(); ++y)
        for (int64_t x = 0; x < in.width(); ++x)
            std::memcpy(out.row(x) + (in.height() - 1 - y) * px, in.row(y) + x * px, px);
    return out;
}

/// Simulates a grade: strong tone curve, white balance shift, saturation change, down to 8 bits.
ImageBuffer grade(const ImageBuffer& in) {
    ImageBuffer out = std::move(ImageBuffer::create(in.width(), in.height(), 3, 8).value());
    for (int64_t y = 0; y < in.height(); ++y)
        for (int64_t x = 0; x < in.width(); ++x) {
            uint16_t s[3];
            std::memcpy(s, in.row(y) + x * 6, 6);
            const double m = (s[0] + s[1] + s[2]) / 3.0 / 65535.0;
            for (int c = 0; c < 3; ++c) {
                double v = s[c] / 65535.0;
                v = m + 1.4 * (v - m);                      // saturation
                v = std::pow(std::clamp(v, 0.0, 1.0), 0.6); // tone curve
                v *= c == 0 ? 1.08 : c == 2 ? 0.9 : 1.0;    // white balance
                out.row(y)[x * 3 + c] = static_cast<uint8_t>(std::clamp(v, 0.0, 1.0) * 255 + 0.5);
            }
        }
    return out;
}

} // namespace

TEST(Signature, IdenticalAndGradedImagesMatch) {
    const ImageBuffer a = photo(640, 480, 0.3);
    const Signature sa = computeSignature(a);
    ASSERT_TRUE(sa.valid);
    EXPECT_NEAR(compareSignatures(sa, sa), 1.0, 1e-9);
    const double graded = compareSignatures(sa, computeSignature(grade(a)));
    EXPECT_GE(graded, 0.95) << "tone/colour edits keep the rank structure";
    const double other = compareSignatures(sa, computeSignature(photo(640, 480, 2.1)));
    EXPECT_LT(other, kSignatureMatchThreshold);
    RecordProperty("graded_score", std::to_string(graded));
    RecordProperty("different_score", std::to_string(other));
}

TEST(Signature, ResizedImageStillMatches) {
    const ImageBuffer a = photo(640, 480, 0.7);
    const ImageBuffer small = photo(320, 240, 0.7); // same content rendered smaller
    EXPECT_GE(compareSignatures(computeSignature(a), computeSignature(small)), 0.97);
}

TEST(Signature, RotationIsFoundByRotatingTheSignature) {
    const ImageBuffer a = photo(400, 300, 1.1);
    const Signature sa = computeSignature(a);
    ImageBuffer r = rotate90(a);
    for (int k = 1; k <= 3; ++k) {
        if (k > 1) r = rotate90(r);
        const Signature sr = computeSignature(r);
        EXPECT_GE(compareSignatures(rotateSignature(sa, k), sr), 0.99) << "k=" << k;
        EXPECT_LT(compareSignatures(sa, sr), compareSignatures(rotateSignature(sa, k), sr));
    }
}

TEST(Signature, StreamedAccumulatorEqualsWholeImage) {
    const ImageBuffer a = photo(333, 251, 0.2);
    SignatureAccumulator acc(a.width(), a.height());
    for (int64_t y = 0; y < a.height(); y += 17) {
        const int64_t n = std::min<int64_t>(17, a.height() - y);
        acc.addRows(y, n, a.row(y), a.rowBytes(), 3, 16);
    }
    const Signature s1 = acc.finish(), s2 = computeSignature(a);
    EXPECT_NEAR(compareSignatures(s1, s2), 1.0, 1e-9);
}

TEST(Signature, EncodeDecodeRoundTrip) {
    const Signature s = computeSignature(photo(200, 150, 0.4));
    auto d = decodeSignature(encodeSignature(s));
    ASSERT_TRUE(d.ok());
    EXPECT_TRUE(d->valid);
    EXPECT_EQ(0, std::memcmp(d->v.data(), s.v.data(), sizeof(float) * s.v.size()));
    EXPECT_FALSE(decodeSignature("not base64 !!").ok());
    EXPECT_FALSE(decodeSignature("AAAA").ok()); // wrong length
}

TEST(EditFileName, ParsesThePattern) {
    auto e = parseEditFileName("Scan 2024-05-01_P03_EDIT_v002");
    ASSERT_TRUE(e.has_value());
    EXPECT_EQ(e->scanStem, "Scan 2024-05-01");
    EXPECT_EQ(e->itemIndex, 3);
    EXPECT_EQ(e->version, 2);
    auto e2 = parseEditFileName("roll_P112_EDIT_v1234");
    ASSERT_TRUE(e2.has_value());
    EXPECT_EQ(e2->itemIndex, 112);
    EXPECT_EQ(e2->version, 1234);
    EXPECT_FALSE(parseEditFileName("scan_P3_EDIT_v001").has_value());
    EXPECT_FALSE(parseEditFileName("scan_P03_EDIT_v01").has_value());
    EXPECT_FALSE(parseEditFileName("scan_P03_FULL_v001").has_value());
}

TEST(EditFileName, SidecarStemCandidatesStripEditorSuffixes) {
    auto c = sidecarStemCandidates("scan_P01_EDIT_v001-Edit");
    EXPECT_NE(std::find(c.begin(), c.end(), "scan_P01_EDIT_v001"), c.end());
    c = sidecarStemCandidates("scan_P01_EDIT_v001 copy 2");
    EXPECT_NE(std::find(c.begin(), c.end(), "scan_P01_EDIT_v001"), c.end());
    c = sidecarStemCandidates("scan_P01_EDIT_v001-Edit-2");
    EXPECT_NE(std::find(c.begin(), c.end(), "scan_P01_EDIT_v001"), c.end());
    c = sidecarStemCandidates("plain");
    ASSERT_EQ(c.size(), 1u);
    EXPECT_EQ(c[0], "plain");
}
