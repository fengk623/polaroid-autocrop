#include "Display.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

namespace fkapp {

bool DisplayConverter::init(const fk::ImageInfo& info) {
    info_ = info;
    gray_ = info.channels <= 2;
    colorCh_ = gray_ ? 1 : 3;
    fk::Profile src = gray_ ? fk::Profile::gray22() : fk::Profile::sRGB();
    if (!info.icc.empty()) {
        auto p = fk::Profile::fromIcc(info.icc);
        if (p && ((gray_ && p->isGray()) || (!gray_ && p->isRgb()))) src = *p;
    }
    const bool alpha = info.channels == 2 || info.channels == 4;
    auto t1 = fk::Transform::create(src, {colorCh_, info.bitsPerSample, alpha}, fk::Profile::sRGB(),
                                    {3, 8, false}, fk::Intent::relative_colorimetric, true);
    if (!t1) return false;
    rowTransform_ = *t1;
    auto t2 = fk::Transform::create(src, {colorCh_, 32, false}, fk::Profile::sRGB(), {3, 8, false},
                                    fk::Intent::relative_colorimetric, true);
    if (!t2) return false;
    proxyTransform_ = *t2;
    return true;
}

void DisplayConverter::convertRow(const uint8_t* src, int64_t w, uint32_t* dst) const {
    std::vector<uint8_t> rgb(static_cast<size_t>(w) * 3);
    if (info_.photometric == fk::Photometric::min_is_white) {
        // stored inverted: invert into a scratch row first
        const int bs = info_.bitsPerSample / 8;
        std::vector<uint8_t> inv(static_cast<size_t>(w) * info_.channels * bs);
        std::memcpy(inv.data(), src, inv.size());
        if (bs == 1)
            for (auto& v : inv)
                v = static_cast<uint8_t>(255 - v);
        else
            for (size_t i = 0; i + 1 < inv.size(); i += 2) {
                uint16_t v;
                std::memcpy(&v, &inv[i], 2);
                v = static_cast<uint16_t>(65535 - v);
                std::memcpy(&inv[i], &v, 2);
            }
        rowTransform_.apply(inv.data(), rgb.data(), static_cast<size_t>(w));
    } else {
        rowTransform_.apply(src, rgb.data(), static_cast<size_t>(w));
    }
    for (int64_t x = 0; x < w; ++x)
        dst[x] =
            0xFF000000u | (uint32_t(rgb[x * 3]) << 16) | (uint32_t(rgb[x * 3 + 1]) << 8) | rgb[x * 3 + 2];
}

QImage DisplayConverter::convertProxy(const fk::Proxy& p) const {
    QImage img(static_cast<int>(p.width), static_cast<int>(p.height), QImage::Format_RGB32);
    std::vector<float> in(static_cast<size_t>(p.width) * colorCh_);
    std::vector<uint8_t> rgb(static_cast<size_t>(p.width) * 3);
    for (int64_t y = 0; y < p.height; ++y) {
        const float* row = p.px(0, y);
        if (gray_) {
            for (int64_t x = 0; x < p.width; ++x)
                in[x] = row[x * 3];
        } else {
            std::memcpy(in.data(), row, in.size() * sizeof(float));
        }
        proxyTransform_.apply(in.data(), rgb.data(), static_cast<size_t>(p.width));
        auto* dst = reinterpret_cast<uint32_t*>(img.scanLine(static_cast<int>(y)));
        for (int64_t x = 0; x < p.width; ++x)
            dst[x] =
                0xFF000000u | (uint32_t(rgb[x * 3]) << 16) | (uint32_t(rgb[x * 3 + 1]) << 8) | rgb[x * 3 + 2];
    }
    return img;
}

QImage boxDownscale(const QImage& src, int longSide) {
    if (src.isNull()) return {};
    const int L = std::max(src.width(), src.height());
    if (L <= longSide) return src;
    return src.scaled(src.width() * longSide / L, src.height() * longSide / L, Qt::KeepAspectRatio,
                      Qt::SmoothTransformation);
}

} // namespace fkapp
