// Proxy generation: exact box filter while streaming rows (RFC-0004 stage A, ADR-0014).
#include "fk/detection.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace fk {

Result<Proxy> buildProxy(ImageSource& src, int longSide, const CancelToken& cancel,
                         const ProgressFn& progress) {
    const ImageInfo& in = src.info();
    if (in.width <= 0 || in.height <= 0) return makeError(Errc::invalid_argument, "Empty image");
    longSide = std::max(64, longSide);
    const int s = std::max<int>(
        1, static_cast<int>(std::ceil(static_cast<double>(std::max(in.width, in.height)) / longSide)));
    Proxy p;
    p.scale = s;
    p.sourceWidth = in.width;
    p.sourceHeight = in.height;
    p.width = (in.width + s - 1) / s;
    p.height = (in.height + s - 1) / s;
    const size_t n = static_cast<size_t>(p.width * p.height * 3);
    p.rgb.assign(n, 0.0f);
    p.ticket = accountBytes(MemCategory::proxy, n * sizeof(float));

    const int ch = in.channels;
    const bool gray = ch <= 2;
    const int bps = in.bytesPerSample();
    const double norm = in.bitsPerSample == 16 ? 65535.0 : 255.0;
    auto band = AccountedBuffer::allocate(MemCategory::band, in.rowBytes() * static_cast<size_t>(s));
    if (!band) return band.error();
    std::vector<double> acc(static_cast<size_t>(p.width) * 3);
    for (int64_t py = 0; py < p.height; ++py) {
        if (cancel.cancelled()) return cancelledError();
        const int64_t y0 = py * s;
        const int64_t rows = std::min<int64_t>(s, in.height - y0);
        FK_TRY(src.readRows(y0, rows, 0, in.width, band->data(), in.rowBytes()));
        std::fill(acc.begin(), acc.end(), 0.0);
        for (int64_t r = 0; r < rows; ++r) {
            const uint8_t* row = band->data() + static_cast<size_t>(r) * in.rowBytes();
            for (int64_t x = 0; x < in.width; ++x) {
                double v[3];
                for (int c = 0; c < (gray ? 1 : 3); ++c) {
                    if (bps == 1)
                        v[c] = row[x * ch + c];
                    else {
                        uint16_t t;
                        std::memcpy(&t, row + (x * ch + c) * 2, 2);
                        v[c] = t;
                    }
                }
                if (gray) v[1] = v[2] = v[0];
                double* a = &acc[static_cast<size_t>(x / s) * 3];
                a[0] += v[0];
                a[1] += v[1];
                a[2] += v[2];
            }
        }
        float* out = p.rgb.data() + static_cast<size_t>(py * p.width) * 3;
        for (int64_t px = 0; px < p.width; ++px) {
            const int64_t cols = std::min<int64_t>(s, in.width - px * s);
            const double cnt = static_cast<double>(cols * rows) * norm;
            for (int c = 0; c < 3; ++c)
                out[px * 3 + c] = static_cast<float>(acc[px * 3 + c] / cnt);
        }
        if ((py & 15) == 0) report(progress, static_cast<double>(py) / p.height, "proxy");
    }
    report(progress, 1.0, "proxy");
    return p;
}

} // namespace fk
