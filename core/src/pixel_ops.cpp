#include "pixel_ops.hpp"

#include <atomic>
#include <numbers>

namespace fk::detail {

Status loadRegion(ImageSource& src, const RectI& r, AccountedBuffer& out, const CancelToken& cancel,
                  const ProgressFn& progress, double p0, double p1) {
    const ImageInfo& in = src.info();
    const size_t rb = static_cast<size_t>(r.w) * in.bytesPerPixel();
    auto b = AccountedBuffer::allocate(MemCategory::region, rb * static_cast<size_t>(r.h));
    if (!b) return b.error();
    out = std::move(b.value());
    constexpr int64_t kBand = 128;
    for (int64_t y = 0; y < r.h; y += kBand) {
        if (cancel.cancelled()) return cancelledError();
        const int64_t n = std::min(kBand, r.h - y);
        FK_TRY(src.readRows(r.y + y, n, r.x, r.w, out.data() + static_cast<size_t>(y) * rb, rb));
        report(progress, p0 + (p1 - p0) * static_cast<double>(y + n) / r.h, "read");
    }
    return {};
}

namespace {
inline double lanczos3(double x) {
    x = std::abs(x);
    if (x < 1e-9) return 1.0;
    if (x >= 3.0) return 0.0;
    const double px = std::numbers::pi * x;
    return 3.0 * std::sin(px) * std::sin(px / 3.0) / (px * px);
}
} // namespace

void lanczosSample(const uint8_t* buf, int64_t w, int64_t h, int ch, int bits, double x, double y,
                   double kscale, float* out) {
    const double cx = x - 0.5, cy = y - 0.5; // pixel-index space
    const double support = 3.0 * kscale;
    const int64_t ix0 = static_cast<int64_t>(std::floor(cx - support)) + 1;
    const int64_t ix1 = static_cast<int64_t>(std::floor(cx + support));
    const int64_t iy0 = static_cast<int64_t>(std::floor(cy - support)) + 1;
    const int64_t iy1 = static_cast<int64_t>(std::floor(cy + support));
    double acc[4] = {0, 0, 0, 0};
    double wsum = 0;
    const size_t rb = static_cast<size_t>(w) * ch * (bits / 8);
    double wx[64];
    const int64_t nx = std::min<int64_t>(64, ix1 - ix0 + 1);
    for (int64_t i = 0; i < nx; ++i)
        wx[i] = lanczos3((ix0 + i - cx) / kscale);
    for (int64_t j = iy0; j <= iy1; ++j) {
        const double wy = lanczos3((j - cy) / kscale);
        if (wy == 0) continue;
        const int64_t yy = std::clamp<int64_t>(j, 0, h - 1);
        for (int64_t i = 0; i < nx; ++i) {
            const double wgt = wx[i] * wy;
            if (wgt == 0) continue;
            const int64_t xx = std::clamp<int64_t>(ix0 + i, 0, w - 1);
            for (int c = 0; c < ch; ++c)
                acc[c] += wgt * sampleAt(buf, rb, ch, bits, xx, yy, c);
            wsum += wgt;
        }
    }
    for (int c = 0; c < ch; ++c)
        out[c] = static_cast<float>(std::clamp(wsum != 0 ? acc[c] / wsum : 0.0, 0.0, 1.0));
}

void parallelFor(int64_t n, const std::function<void(int64_t)>& fn) {
    const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
    const int64_t workers = std::min<int64_t>(n, std::max<int64_t>(1, static_cast<int64_t>(hw) - 1));
    if (workers <= 1) {
        for (int64_t i = 0; i < n; ++i)
            fn(i);
        return;
    }
    std::atomic<int64_t> next{0};
    std::vector<std::thread> ts;
    for (int64_t t = 0; t < workers; ++t)
        ts.emplace_back([&] {
            for (int64_t i = next++; i < n; i = next++)
                fn(i);
        });
    for (auto& t : ts)
        t.join();
}

Status RowConverter::init(int srcChannels, int srcBits, int outChannels, int outBits,
                          const std::optional<Transform>& xf, bool srcAlpha) {
    sc_ = srcChannels;
    sb_ = srcBits;
    oc_ = outChannels;
    ob_ = outBits;
    xf_ = xf;
    alpha_ = srcAlpha;
    identity_ = !xf_ && sc_ == oc_ && sb_ == ob_;
    return {};
}

void RowConverter::convert(const uint8_t* in, uint8_t* out, int64_t n) const {
    if (identity_) {
        std::memcpy(out, in, static_cast<size_t>(n) * sc_ * (sb_ / 8));
        return;
    }
    const int sColor = alpha_ ? sc_ - 1 : sc_;
    const int oColor = (oc_ == 2 || oc_ == 4) ? oc_ - 1 : oc_;
    const bool outAlpha = oc_ == 2 || oc_ == 4;
    if (xf_) {
        // transform handles colour channels + depth; alpha (if kept) copied by lcms (COPY_ALPHA)
        if (sc_ == oc_ || (!alpha_ && !outAlpha)) {
            xf_->apply(in, out, static_cast<size_t>(n));
            return;
        }
        // drop alpha first into a temporary colour-only buffer
        std::vector<uint8_t> tmp(static_cast<size_t>(n) * sColor * (sb_ / 8));
        const int bs = sb_ / 8;
        for (int64_t i = 0; i < n; ++i)
            std::memcpy(&tmp[(i * sColor) * bs], in + i * sc_ * bs, sColor * bs);
        xf_->apply(tmp.data(), out, static_cast<size_t>(n));
        return;
    }
    const int bs = sb_ / 8, bo = ob_ / 8;
    for (int64_t i = 0; i < n; ++i) {
        const uint8_t* ip = in + i * sc_ * bs;
        uint8_t* op = out + i * oc_ * bo;
        for (int c = 0; c < oColor; ++c) {
            const int scIdx = sColor == 1 ? 0 : std::min(c, sColor - 1);
            convertDepth(ip + scIdx * bs, sb_, op + c * bo, ob_, 1);
        }
        if (outAlpha) {
            if (alpha_)
                convertDepth(ip + (sc_ - 1) * bs, sb_, op + (oc_ - 1) * bo, ob_, 1);
            else
                storeSample(op + (oc_ - 1) * bo, ob_, 1.0f);
        }
    }
}

} // namespace fk::detail
