#include "fk/color.hpp"
#include "fk/hash.hpp"

#include <lcms2.h>

#include <cmath>
#include <mutex>

namespace fk {

namespace {
std::vector<uint8_t> saveProfile(cmsHPROFILE h) {
    cmsUInt32Number len = 0;
    if (!cmsSaveProfileToMem(h, nullptr, &len) || len == 0) return {};
    std::vector<uint8_t> b(len);
    if (!cmsSaveProfileToMem(h, b.data(), &len)) return {};
    b.resize(len);
    return b;
}

std::string describe(cmsHPROFILE h) {
    char buf[512] = {0};
    const cmsUInt32Number n = cmsGetProfileInfoASCII(h, cmsInfoDescription, "en", "US", buf, sizeof(buf));
    if (n == 0) return {};
    return std::string(buf);
}

cmsHPROFILE makeRgb(double wx, double wy, const double prim[6], double gamma, const char* desc) {
    cmsCIExyY white{wx, wy, 1.0};
    cmsCIExyYTRIPLE p{{prim[0], prim[1], 1.0}, {prim[2], prim[3], 1.0}, {prim[4], prim[5], 1.0}};
    cmsToneCurve* g = cmsBuildGamma(nullptr, gamma);
    cmsToneCurve* curves[3] = {g, g, g};
    cmsHPROFILE h = cmsCreateRGBProfile(&white, &p, curves);
    cmsFreeToneCurve(g);
    if (h) {
        cmsMLU* mlu = cmsMLUalloc(nullptr, 1);
        cmsMLUsetASCII(mlu, "en", "US", desc);
        cmsWriteTag(h, cmsSigProfileDescriptionTag, mlu);
        cmsMLUfree(mlu);
        cmsMLU* cpr = cmsMLUalloc(nullptr, 1);
        cmsMLUsetASCII(cpr, "en", "US", "No copyright, use freely");
        cmsWriteTag(h, cmsSigCopyrightTag, cpr);
        cmsMLUfree(cpr);
    }
    return h;
}

cmsUInt32Number formatOf(const PixelLayout& l) {
    const bool gray = l.channels == 1;
    cmsUInt32Number f = COLORSPACE_SH(gray ? PT_GRAY : PT_RGB) | CHANNELS_SH(gray ? 1 : 3);
    if (l.bits == 32)
        f |= FLOAT_SH(1) | BYTES_SH(4);
    else
        f |= BYTES_SH(l.bits / 8);
    if (l.alpha) f |= EXTRA_SH(1);
    return f;
}

struct LcmsInit {
    LcmsInit() {
        cmsSetLogErrorHandler([](cmsContext, cmsUInt32Number, const char*) {});
    }
};
void ensureInit() {
    static LcmsInit init;
}
} // namespace

Profile::Impl::~Impl() {
    if (handle) cmsCloseProfile(static_cast<cmsHPROFILE>(handle));
}

Profile Profile::wrap(void* handle) {
    Profile p;
    p.impl_ = std::make_shared<Impl>();
    p.impl_->handle = handle;
    auto h = static_cast<cmsHPROFILE>(handle);
    p.impl_->bytes = saveProfile(h);
    p.impl_->description = describe(h);
    p.impl_->sha256 = sha256Hex(p.impl_->bytes.data(), p.impl_->bytes.size());
    const auto cs = cmsGetColorSpace(h);
    p.impl_->gray = cs == cmsSigGrayData;
    p.impl_->rgb = cs == cmsSigRgbData;
    return p;
}

Result<Profile> Profile::fromIcc(const std::vector<uint8_t>& bytes) {
    ensureInit();
    if (bytes.empty()) return makeError(Errc::color_profile, "Empty ICC profile");
    cmsHPROFILE h = cmsOpenProfileFromMem(bytes.data(), static_cast<cmsUInt32Number>(bytes.size()));
    if (!h) return makeError(Errc::color_profile, "The embedded ICC profile cannot be read");
    Profile p;
    p.impl_ = std::make_shared<Impl>();
    p.impl_->handle = h;
    p.impl_->bytes = bytes; // keep the original bytes exactly (never re-serialise an input profile)
    p.impl_->description = describe(h);
    p.impl_->sha256 = sha256Hex(bytes.data(), bytes.size());
    const auto cs = cmsGetColorSpace(h);
    p.impl_->gray = cs == cmsSigGrayData;
    p.impl_->rgb = cs == cmsSigRgbData;
    return p;
}

Profile Profile::sRGB() {
    ensureInit();
    static Profile p = wrap(cmsCreate_sRGBProfile());
    return p;
}

Profile Profile::adobeRGB1998() {
    ensureInit();
    static Profile p = [] {
        const double prim[6] = {0.6400, 0.3300, 0.2100, 0.7100, 0.1500, 0.0600};
        return wrap(
            makeRgb(0.3127, 0.3290, prim, 563.0 / 256.0, "Adobe RGB (1998) compatible (FrameKeeper)"));
    }();
    return p;
}

Profile Profile::proPhoto() {
    ensureInit();
    static Profile p = [] {
        const double prim[6] = {0.7347, 0.2653, 0.1596, 0.8404, 0.0366, 0.0001};
        return wrap(makeRgb(0.3457, 0.3585, prim, 1.8, "ProPhoto RGB compatible (FrameKeeper)"));
    }();
    return p;
}

Profile Profile::gray22() {
    ensureInit();
    static Profile p = [] {
        cmsToneCurve* g = cmsBuildGamma(nullptr, 2.2);
        cmsHPROFILE h = cmsCreateGrayProfile(cmsD50_xyY(), g);
        cmsFreeToneCurve(g);
        cmsMLU* mlu = cmsMLUalloc(nullptr, 1);
        cmsMLUsetASCII(mlu, "en", "US", "Gray Gamma 2.2 (FrameKeeper)");
        cmsWriteTag(h, cmsSigProfileDescriptionTag, mlu);
        cmsMLUfree(mlu);
        return wrap(h);
    }();
    return p;
}

bool Profile::colorants(std::array<double, 9>& xyzColumns) const {
    auto h = static_cast<cmsHPROFILE>(impl_->handle);
    auto* r = static_cast<cmsCIEXYZ*>(cmsReadTag(h, cmsSigRedColorantTag));
    auto* g = static_cast<cmsCIEXYZ*>(cmsReadTag(h, cmsSigGreenColorantTag));
    auto* b = static_cast<cmsCIEXYZ*>(cmsReadTag(h, cmsSigBlueColorantTag));
    if (!r || !g || !b) return false;
    xyzColumns = {r->X, g->X, b->X, r->Y, g->Y, b->Y, r->Z, g->Z, b->Z};
    return true;
}

Result<Profile> Profile::linearized(const Profile& p) {
    if (!p.isRgb()) return makeError(Errc::color_profile, "Linearisation needs an RGB profile");
    std::array<double, 9> m{};
    if (!p.colorants(m)) return makeError(Errc::color_profile, "Profile is not a matrix-shaper RGB profile");
    cmsHPROFILE h = cmsCreateProfilePlaceholder(nullptr);
    cmsSetProfileVersion(h, 4.3);
    cmsSetDeviceClass(h, cmsSigDisplayClass);
    cmsSetColorSpace(h, cmsSigRgbData);
    cmsSetPCS(h, cmsSigXYZData);
    cmsCIEXYZ r{m[0], m[3], m[6]}, g{m[1], m[4], m[7]}, b{m[2], m[5], m[8]};
    cmsWriteTag(h, cmsSigRedColorantTag, &r);
    cmsWriteTag(h, cmsSigGreenColorantTag, &g);
    cmsWriteTag(h, cmsSigBlueColorantTag, &b);
    cmsWriteTag(h, cmsSigMediaWhitePointTag, cmsD50_XYZ());
    cmsToneCurve* lin = cmsBuildGamma(nullptr, 1.0);
    cmsWriteTag(h, cmsSigRedTRCTag, lin);
    cmsWriteTag(h, cmsSigGreenTRCTag, lin);
    cmsWriteTag(h, cmsSigBlueTRCTag, lin);
    cmsFreeToneCurve(lin);
    cmsMLU* mlu = cmsMLUalloc(nullptr, 1);
    cmsMLUsetASCII(mlu, "en", "US", ("Linear " + p.description()).c_str());
    cmsWriteTag(h, cmsSigProfileDescriptionTag, mlu);
    cmsMLUfree(mlu);
    return wrap(h);
}

Transform::Impl::~Impl() {
    if (handle) cmsDeleteTransform(static_cast<cmsHTRANSFORM>(handle));
}

Result<Transform> Transform::create(const Profile& src, const PixelLayout& in, const Profile& dst,
                                    const PixelLayout& out, Intent intent, bool bpc) {
    ensureInit();
    cmsUInt32Number flags = cmsFLAGS_NOCACHE;
    if (bpc) flags |= cmsFLAGS_BLACKPOINTCOMPENSATION;
    if (in.alpha && out.alpha) flags |= cmsFLAGS_COPY_ALPHA;
    // LittleCMS's optimised 16-bit pipelines (pre-linearisation + CLUT) lose thousands of 16-bit codes
    // near black between matrix-shaper profiles of different gamma (measured: test_color). Archival
    // 16-bit paths therefore evaluate the full-precision pipeline; 8-bit (display) stays optimised.
    if (in.bits >= 16 || out.bits >= 16) flags |= cmsFLAGS_NOOPTIMIZE;
    cmsHTRANSFORM t = cmsCreateTransform(static_cast<cmsHPROFILE>(src.handle()), formatOf(in),
                                         static_cast<cmsHPROFILE>(dst.handle()), formatOf(out),
                                         static_cast<cmsUInt32Number>(intent), flags);
    if (!t) return makeError(Errc::color_profile, "Cannot build a colour transform between these profiles");
    Transform tr;
    tr.impl_ = std::make_shared<Impl>();
    tr.impl_->handle = t;
    return tr;
}

void Transform::apply(const void* in, void* out, size_t pixels) const {
    cmsDoTransform(static_cast<cmsHTRANSFORM>(impl_->handle), in, out, static_cast<cmsUInt32Number>(pixels));
}

std::string iccDescription(const std::vector<uint8_t>& icc) {
    if (icc.empty()) return {};
    ensureInit();
    cmsHPROFILE h = cmsOpenProfileFromMem(icc.data(), static_cast<cmsUInt32Number>(icc.size()));
    if (!h) return {};
    std::string d = describe(h);
    cmsCloseProfile(h);
    return d;
}

std::string sha256Hex(const void* data, size_t n) {
    Sha256 s;
    s.update(data, n);
    return s.finishHex();
}

double deltaE2000(const float* x, const float* y) {
    constexpr double kPi = 3.14159265358979323846;
    const double L1 = x[0], A1 = x[1], B1 = x[2], L2 = y[0], A2 = y[1], B2 = y[2];
    const double C1 = std::hypot(A1, B1), C2 = std::hypot(A2, B2), Cm = (C1 + C2) / 2;
    const double Cm7 = std::pow(Cm, 7), k25 = 6103515625.0; // 25^7
    const double G = 0.5 * (1 - std::sqrt(Cm7 / (Cm7 + k25)));
    const double a1 = (1 + G) * A1, a2 = (1 + G) * A2;
    const double C1p = std::hypot(a1, B1), C2p = std::hypot(a2, B2);
    auto hue = [&](double bb, double aa) {
        if (bb == 0 && aa == 0) return 0.0;
        const double h = std::atan2(bb, aa) * 180 / kPi;
        return h < 0 ? h + 360 : h;
    };
    const double h1 = hue(B1, a1), h2 = hue(B2, a2);
    const double dL = L2 - L1, dC = C2p - C1p;
    double dh = 0;
    if (C1p * C2p != 0) {
        dh = h2 - h1;
        if (dh > 180) dh -= 360;
        if (dh < -180) dh += 360;
    }
    const double dH = 2 * std::sqrt(C1p * C2p) * std::sin(dh / 2 * kPi / 180);
    const double Lm = (L1 + L2) / 2, Cpm = (C1p + C2p) / 2;
    double hm = h1 + h2;
    if (C1p * C2p != 0) {
        if (std::abs(h1 - h2) > 180) hm += hm < 360 ? 360 : -360;
        hm /= 2;
    }
    const double T = 1 - 0.17 * std::cos((hm - 30) * kPi / 180) + 0.24 * std::cos(2 * hm * kPi / 180) +
                     0.32 * std::cos((3 * hm + 6) * kPi / 180) - 0.20 * std::cos((4 * hm - 63) * kPi / 180);
    const double dTheta = 30 * std::exp(-std::pow((hm - 275) / 25, 2));
    const double Cpm7 = std::pow(Cpm, 7);
    const double Rc = 2 * std::sqrt(Cpm7 / (Cpm7 + k25));
    const double Sl = 1 + 0.015 * std::pow(Lm - 50, 2) / std::sqrt(20 + std::pow(Lm - 50, 2));
    const double Sc = 1 + 0.045 * Cpm, Sh = 1 + 0.015 * Cpm * T;
    const double Rt = -std::sin(2 * dTheta * kPi / 180) * Rc;
    return std::sqrt(std::pow(dL / Sl, 2) + std::pow(dC / Sc, 2) + std::pow(dH / Sh, 2) +
                     Rt * (dC / Sc) * (dH / Sh));
}

void srgbToLab(const float* rgb, float* lab) {
    auto lin = [](float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); };
    const float r = lin(rgb[0]), g = lin(rgb[1]), b = lin(rgb[2]);
    const float X = (0.4124f * r + 0.3576f * g + 0.1805f * b) / 0.95047f;
    const float Y = 0.2126f * r + 0.7152f * g + 0.0722f * b;
    const float Z = (0.0193f * r + 0.1192f * g + 0.9505f * b) / 1.08883f;
    auto f = [](float t) { return t > 0.008856f ? std::cbrt(t) : 7.787f * t + 16.0f / 116.0f; };
    const float fx = f(X), fy = f(Y), fz = f(Z);
    lab[0] = 116.0f * fy - 16.0f;
    lab[1] = 500.0f * (fx - fy);
    lab[2] = 200.0f * (fy - fz);
}

} // namespace fk
