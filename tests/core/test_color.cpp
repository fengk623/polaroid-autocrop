// RFC-0012 / PRD F12 colour tests.
#include <gtest/gtest.h>

#include "fk/color.hpp"

#include <cmath>
#include <cstring>

using namespace fk;

TEST(Color, BuiltinProfilesDescribeThemselves) {
    EXPECT_FALSE(Profile::sRGB().description().empty());
    EXPECT_NE(Profile::adobeRGB1998().description().find("Adobe RGB"), std::string::npos);
    EXPECT_TRUE(Profile::proPhoto().isRgb());
    EXPECT_TRUE(Profile::gray22().isGray());
    EXPECT_EQ(Profile::sRGB().sha256().size(), 64u);
}

TEST(Color, FromIccKeepsBytesExactly) {
    const auto bytes = Profile::adobeRGB1998().icc();
    auto p = Profile::fromIcc(bytes);
    ASSERT_TRUE(p.ok());
    EXPECT_EQ(p->icc(), bytes);
    EXPECT_FALSE(Profile::fromIcc({1, 2, 3}).ok());
}

TEST(Color, KnownValuesSrgbToAdobe) {
    // Pure sRGB red in Adobe RGB (1998) is approximately (219, 0, 0) at 8 bit.
    auto t = Transform::create(Profile::sRGB(), {3, 8, false}, Profile::adobeRGB1998(), {3, 8, false});
    ASSERT_TRUE(t.ok());
    const uint8_t in[6] = {255, 0, 0, 255, 255, 255};
    uint8_t out[6] = {0};
    t->apply(in, out, 2);
    EXPECT_NEAR(out[0], 219, 2);
    EXPECT_LE(out[1], 2);
    EXPECT_LE(out[2], 2);
    EXPECT_GE(out[3], 254);
    EXPECT_GE(out[4], 254);
    EXPECT_GE(out[5], 254);
}

TEST(Color, IdentityTransformIsExactAt16Bit) {
    auto t =
        Transform::create(Profile::adobeRGB1998(), {3, 16, false}, Profile::adobeRGB1998(), {3, 16, false});
    ASSERT_TRUE(t.ok());
    std::vector<uint16_t> in(3 * 1024), out(3 * 1024);
    for (size_t i = 0; i < in.size(); ++i)
        in[i] = static_cast<uint16_t>(i * 64 + 7);
    t->apply(in.data(), out.data(), 1024);
    int maxErr = 0;
    for (size_t i = 0; i < in.size(); ++i)
        maxErr = std::max(maxErr, std::abs(int(in[i]) - int(out[i])));
    EXPECT_LE(maxErr, 2); // 16-bit precision kept (no 8-bit intermediate)
}

TEST(Color, Sixteen_bit_precision_survives_roundtrip_conversion) {
    auto a = Transform::create(Profile::adobeRGB1998(), {3, 16, false}, Profile::proPhoto(), {3, 16, false});
    auto b = Transform::create(Profile::proPhoto(), {3, 16, false}, Profile::adobeRGB1998(), {3, 16, false});
    ASSERT_TRUE(a.ok() && b.ok());
    // Mid-tone values: a channel near 0 inside a saturated colour legitimately amplifies 16-bit quantisation.
    std::vector<uint16_t> in = {1000, 20000, 40000, 65535, 32768, 12000, 5000, 5001, 5002};
    std::vector<uint16_t> mid(in.size()), out(in.size());
    a->apply(in.data(), mid.data(), 3);
    b->apply(mid.data(), out.data(), 3);
    for (size_t i = 0; i < in.size(); ++i)
        EXPECT_NEAR(out[i], in[i], 40) << i; // far below 1 LSB at 8 bit (257)
}

TEST(Color, LinearizedProfileHasSameColorants) {
    auto lin = Profile::linearized(Profile::adobeRGB1998());
    ASSERT_TRUE(lin.ok());
    std::array<double, 9> a{}, b{};
    ASSERT_TRUE(Profile::adobeRGB1998().colorants(a));
    ASSERT_TRUE(lin->colorants(b));
    for (int i = 0; i < 9; ++i)
        EXPECT_NEAR(a[i], b[i], 1e-4);
    auto t = Transform::create(Profile::adobeRGB1998(), {3, 32, false}, *lin, {3, 32, false});
    ASSERT_TRUE(t.ok());
    const float in[3] = {0.5f, 0.5f, 0.5f};
    float out[3];
    t->apply(in, out, 1);
    EXPECT_NEAR(out[0], std::pow(0.5, 563.0 / 256.0), 2e-3);
}

TEST(Color, SrgbToLabWhiteAndBlack) {
    float w[3] = {1, 1, 1}, k[3] = {0, 0, 0}, lab[3];
    srgbToLab(w, lab);
    EXPECT_NEAR(lab[0], 100, 0.2);
    EXPECT_NEAR(lab[1], 0, 0.5);
    srgbToLab(k, lab);
    EXPECT_NEAR(lab[0], 0, 0.01);
}

TEST(DeltaE2000, MatchesThePublishedTestData) {
    // Sharma, Wu, Dalal (2005), test pairs 1, 7, 17, 18, 19, 20
    const float pairs[][7] = {{50.0f, 2.6772f, -79.7751f, 50.0f, 0.0f, -82.7485f, 2.0425f},
                              {50.0f, 0.0f, 0.0f, 50.0f, -1.0f, 2.0f, 2.3669f},
                              {50.0f, 2.5f, 0.0f, 73.0f, 25.0f, -18.0f, 27.1492f},
                              {50.0f, 2.5f, 0.0f, 61.0f, -5.0f, 29.0f, 22.8977f},
                              {50.0f, 2.5f, 0.0f, 56.0f, -27.0f, -3.0f, 31.9030f},
                              {50.0f, 2.5f, 0.0f, 58.0f, 24.0f, 15.0f, 19.4535f}};
    for (const auto& p : pairs) {
        EXPECT_NEAR(fk::deltaE2000(p, p + 3), p[6], 1e-3) << p[3] << " " << p[4] << " " << p[5];
        EXPECT_NEAR(fk::deltaE2000(p + 3, p), p[6], 1e-3); // symmetric
    }
    EXPECT_EQ(fk::deltaE2000(pairs[0], pairs[0]), 0.0);
}
