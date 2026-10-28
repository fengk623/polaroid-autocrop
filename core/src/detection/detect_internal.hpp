// Internal detection structures shared by stage A (detect.cpp) and stage B (refine.cpp).
#pragma once

#include "cv.hpp"
#include "fk/detection.hpp"

#include <string>
#include <vector>

namespace fk::detail {

/// Interleaved approximate CIELAB of the proxy (detection only; ADR-0003).
struct LabImage {
    int64_t w = 0, h = 0;
    std::vector<float> v;
    const float* at(int64_t x, int64_t y) const { return v.data() + (static_cast<size_t>(y) * w + x) * 3; }
};
LabImage toLab(const Proxy& p);
/// Bilinear sample with edge clamping.
void sampleLab(const LabImage& lab, Vec2 p, float out[3]);
inline float deltaE(const float* a, const float* b) {
    const float d0 = a[0] - b[0], d1 = a[1] - b[1], d2 = a[2] - b[2];
    return std::sqrt(d0 * d0 + d1 * d1 + d2 * d2);
}

struct RefineStats {
    double validFraction = 0; ///< fraction of outer profile lines with a detectable edge
    double contrast = 0;      ///< median paper/background distance divided by noise
    double residualSigma = 0; ///< robust spread of edge points around the fitted lines (px)
    int sidesLocked = 0;      ///< sides abutting a neighbour or the scan border (kept at the proxy estimate)
    bool measured = false;    ///< at least one outer side was measured (stats above are meaningful)
    int sidesKept = 0;        ///< sides that could not be refined (proxy estimate kept)
    double innerValidFraction = 0;
};

/// Stage B: refines outer quads (containment by construction) and inner shapes of all items in one
/// increasing-row pass over the source.
/// `locked[i][s]`: outer side s of item i abuts other material (a touching neighbour); it is not refined
/// inward (no background to measure against) and keeps its proxy estimate plus the safety margin.
Status refineItems(ImageSource& src, std::vector<DetectedItem>& items, double dpi, int proxyScale,
                   double safetyPx, std::vector<RefineStats>& stats, const CancelToken& cancel,
                   const ProgressFn& progress, const std::vector<std::array<bool, 4>>* locked = nullptr);

} // namespace fk::detail
