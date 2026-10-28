// Internal pixel helpers shared by export and compositing.
#pragma once

#include "fk/color.hpp"
#include "fk/common.hpp"
#include "fk/image.hpp"
#include "fk/image_io.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <optional>
#include <thread>
#include <vector>

namespace fk::detail {

/// Reads a rectangle of the source into an accounted, contiguous buffer (native depth).
Status loadRegion(ImageSource& src, const RectI& r, AccountedBuffer& out, const CancelToken& cancel,
                  const ProgressFn& progress, double p0, double p1);

/// Reads one sample (normalised [0,1]) from an interleaved buffer.
inline float sampleAt(const uint8_t* buf, size_t rowBytes, int ch, int bits, int64_t x, int64_t y, int c) {
    const uint8_t* p = buf + static_cast<size_t>(y) * rowBytes;
    if (bits == 8) return p[x * ch + c] * (1.0f / 255.0f);
    uint16_t v;
    std::memcpy(&v, p + (x * ch + c) * 2, 2);
    return v * (1.0f / 65535.0f);
}

/// Lanczos-3 sample at continuous position (x,y) (pixel (i,j) centre at i+0.5, j+0.5) of a w×h buffer.
/// `kscale` ≥ 1 widens the kernel for downscaling. Writes `ch` normalised values (edge-clamped, clamped to
/// [0,1]).
void lanczosSample(const uint8_t* buf, int64_t w, int64_t h, int ch, int bits, double x, double y,
                   double kscale, float* out);

/// Runs fn(i) for i in [0,n) on up to (hardware threads − 1) workers (≥ 1).
void parallelFor(int64_t n, const std::function<void(int64_t)>& fn);

/// Converts rows from the working layout (source channels/depth) to the output layout:
/// channel mapping (drop alpha, grey→RGB), optional colour transform (single conversion), depth.
class RowConverter {
  public:
    Status init(int srcChannels, int srcBits, int outChannels, int outBits,
                const std::optional<Transform>& xf, bool srcAlpha);
    /// Converts `n` pixels.
    void convert(const uint8_t* in, uint8_t* out, int64_t n) const;
    bool identity() const { return identity_; }

  private:
    int sc_ = 0, sb_ = 0, oc_ = 0, ob_ = 0;
    bool alpha_ = false, identity_ = false;
    std::optional<Transform> xf_;
};

/// Writes a normalised float to an output sample with rounding.
inline void storeSample(uint8_t* p, int bits, float v) {
    v = std::clamp(v, 0.0f, 1.0f);
    if (bits == 8)
        *p = static_cast<uint8_t>(std::lround(v * 255.0f));
    else {
        const uint16_t t = static_cast<uint16_t>(std::lround(v * 65535.0f));
        std::memcpy(p, &t, 2);
    }
}

} // namespace fk::detail
