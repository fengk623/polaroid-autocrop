// In-house computer-vision primitives for detection (RFC-0004, ADR-0001). Deterministic, no allocation
// surprises: all buffers are sized from proxy dimensions.
#pragma once

#include "fk/geometry.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace fk::cv {

struct Mask {
    int64_t w = 0, h = 0;
    std::vector<uint8_t> v;
    Mask() = default;
    Mask(int64_t w_, int64_t h_) : w(w_), h(h_), v(static_cast<size_t>(w_ * h_), 0) {}
    uint8_t& at(int64_t x, int64_t y) { return v[static_cast<size_t>(y * w + x)]; }
    uint8_t at(int64_t x, int64_t y) const { return v[static_cast<size_t>(y * w + x)]; }
};

struct ImageF {
    int64_t w = 0, h = 0;
    std::vector<float> v;
    ImageF() = default;
    ImageF(int64_t w_, int64_t h_) : w(w_), h(h_), v(static_cast<size_t>(w_ * h_), 0.f) {}
    float& at(int64_t x, int64_t y) { return v[static_cast<size_t>(y * w + x)]; }
    float at(int64_t x, int64_t y) const { return v[static_cast<size_t>(y * w + x)]; }
};

double median(std::vector<double> v);
/// Median absolute deviation scaled to σ (×1.4826).
double madSigma(const std::vector<double>& v, double med);
double quantile(std::vector<double> v, double q);

/// Otsu threshold over values in [0, maxVal].
double otsuThreshold(const std::vector<float>& values, double maxVal, int bins = 512);

/// Binary morphology with a (2r+1)² square structuring element (separable, O(N)).
void dilate(Mask& m, int r);
void erode(Mask& m, int r);
inline void closeMask(Mask& m, int r) {
    dilate(m, r);
    erode(m, r);
}
inline void openMask(Mask& m, int r) {
    erode(m, r);
    dilate(m, r);
}
/// Sets every 0-pixel not 4-connected to the image border to 1.
void fillHoles(Mask& m);

struct Component {
    int label = 0;
    int64_t area = 0;
    int64_t x0 = 0, y0 = 0, x1 = 0, y1 = 0; ///< inclusive bounds
    std::vector<Vec2> boundaryCorners;      ///< corners of boundary pixels (proxy coords)
};
/// 8-connected components of non-zero pixels. `labels` receives a label per pixel (0 = background).
std::vector<Component> connectedComponents(const Mask& m, std::vector<int32_t>& labels);

std::vector<Vec2> convexHull(std::vector<Vec2> pts);

struct RotRect {
    Vec2 c{};
    double w = 0, h = 0; ///< along the rect's own axes
    double angle = 0;    ///< direction of the w axis, clockwise, normalised to (-π/4, π/4]
    Quad quad() const;
    double area() const { return w * h; }
};
RotRect minAreaRect(const std::vector<Vec2>& hull);

/// Robust total-least-squares line (Tukey IRLS). Needs ≥ 2 points.
std::optional<Line> fitLineRobust(const std::vector<Vec2>& pts, std::vector<double>* weightsOut = nullptr);

struct Circle {
    Vec2 c{};
    double r = 0;
};
std::optional<Circle> fitCircleRobust(const std::vector<Vec2>& pts);

/// Least squares z = c0 + c1 x + c2 y + c3 x² + c4 xy + c5 y².
std::array<double, 6> fitPoly2(const std::vector<std::array<double, 3>>& xyz);
inline double evalPoly2(const std::array<double, 6>& c, double x, double y) {
    return c[0] + c[1] * x + c[2] * y + c[3] * x * x + c[4] * x * y + c[5] * y * y;
}

} // namespace fk::cv
