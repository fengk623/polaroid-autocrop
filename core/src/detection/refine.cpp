// Stage B: full-resolution edge refinement in one increasing-row pass (RFC-0004).
//
// Outer sides: for each sampled line perpendicular to a side, find the outermost paper position, fit a
// robust line, then translate it outward until every accepted edge point is inside — containment by
// construction — plus the safety margin. Inner edges: frame→image transition, robust fit, no shift.
#include "detect_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

namespace fk::detail {

namespace {

constexpr double kPi = std::numbers::pi;

enum class Kind : uint8_t { outer = 0, inner = 1, innerCircle = 2 };

struct ProfileLine {
    size_t item = 0;
    int side = 0; ///< 0..3 for quads (edge p_side → p_side+1); ray index for circles
    Kind kind = Kind::outer;
    Vec2 base{};       ///< point on the current boundary estimate
    Vec2 n{};          ///< unit normal pointing inside (paper for outer, image for inner)
    double along = 0;  ///< position along the side (0..1)
    int half = 0;      ///< samples on each side of base
    int extraIn = 0;   ///< additional samples beyond `half` on the inside (outer lines: reach real paper)
    size_t offset = 0; ///< index of the first sample in the value store
};

struct Need {
    int64_t y, x;
    uint32_t sample; ///< global sample index
};

struct Sample {
    float v[3];
    float tau; ///< projection of the pixel centre on the line normal, relative to base
};

double medianOf(std::vector<double> v) {
    return cv::median(std::move(v));
}

/// Analyses one profile; returns the edge position τ (relative to base along n) or NaN.
/// `outerMode`: background at the start (τ < 0), paper at the end.
double analyse(const Sample* s, int n, bool outerMode, double& contrastOut, double& noiseOut, int persist) {
    const int q = std::max(3, n / 5);
    std::vector<double> c0[3], c1[3];
    for (int i = 0; i < q; ++i)
        for (int c = 0; c < 3; ++c) {
            c0[c].push_back(s[i].v[c]);
            c1[c].push_back(s[n - 1 - i].v[c]);
        }
    double a[3], b[3];
    for (int c = 0; c < 3; ++c) {
        a[c] = medianOf(c0[c]);
        b[c] = medianOf(c1[c]);
    }
    std::vector<double> dist(n);
    for (int i = 0; i < n; ++i) {
        double d2 = 0;
        for (int c = 0; c < 3; ++c)
            d2 += (s[i].v[c] - a[c]) * (s[i].v[c] - a[c]);
        dist[i] = std::sqrt(d2) * 100.0;
    }
    double C2 = 0;
    for (int c = 0; c < 3; ++c)
        C2 += (b[c] - a[c]) * (b[c] - a[c]);
    const double C = std::sqrt(C2) * 100.0;
    std::vector<double> head(dist.begin(), dist.begin() + q);
    const double noise = std::max(0.3, cv::madSigma(head, cv::median(head)) + 0.5 * cv::median(head));
    contrastOut = C;
    noiseOut = noise;
    if (C < std::max(4.0 * noise, outerMode ? 3.0 : 4.0)) return std::numeric_limits<double>::quiet_NaN();
    std::vector<double> sm(n), smB(n), distB(n);
    for (int i = 0; i < n; ++i) {
        double d2 = 0;
        for (int c = 0; c < 3; ++c)
            d2 += (s[i].v[c] - b[c]) * (s[i].v[c] - b[c]);
        distB[i] = std::sqrt(d2) * 100.0;
    }
    for (int i = 0; i < n; ++i) {
        const int i0 = std::max(0, i - 1), i1 = std::min(n - 1, i + 1);
        sm[i] = (dist[i0] + dist[i] + dist[i1]) / 3.0;
        smB[i] = (distB[i0] + distB[i] + distB[i1]) / 3.0;
    }
    const double thr = 0.5 * C;
    // Outer edges: paper is connected to the inner end of the profile. A detached speck (dust on the
    // glass) is followed by background again; start after the last background run so that it is ignored.
    int start = 1;
    if (outerMode) {
        int run = 0;
        for (int i = 0; i < n; ++i) {
            run = sm[i] <= thr ? run + 1 : 0;
            if (run >= persist + 1) start = std::max(start, i + 1);
        }
        if (start >= n - persist) return std::numeric_limits<double>::quiet_NaN();
    }
    for (int i = start; i + persist < n; ++i) {
        bool ok = true;
        for (int k = 0; k <= persist; ++k)
            // outer: the pixels past the crossing must also be paper coloured (a light scratch or dust
            // crossing a dark frame edge is far from both background and paper)
            if (sm[i + k] <= thr || (outerMode && smB[i + k] > std::max(thr, 0.75 * C))) {
                ok = false;
                break;
            }
        if (!ok) continue;
        const double f = (thr - sm[i - 1]) / std::max(1e-9, sm[i] - sm[i - 1]);
        return s[i - 1].tau + std::clamp(f, 0.0, 1.0) * (s[i].tau - s[i - 1].tau);
    }
    return std::numeric_limits<double>::quiet_NaN();
}

} // namespace

Status refineItems(ImageSource& src, std::vector<DetectedItem>& items, double dpi, int proxyScale,
                   double safetyPx, std::vector<RefineStats>& stats, const CancelToken& cancel,
                   const ProgressFn& progress, const std::vector<std::array<bool, 4>>* locked) {
    const ImageInfo& info = src.info();
    const double pxPerMm = dpi / 25.4;
    const int half = 4 * proxyScale + 16;
    // The proxy estimate may include a scanner shadow (~1 mm) along the edge: outer profiles reach 2 mm
    // further inside so that their inner end is real paper.
    const int extraIn = static_cast<int>(std::lround(2.0 * pxPerMm));
    stats.assign(items.size(), RefineStats{});

    // ---- 1. build profile lines ----
    std::vector<ProfileLine> lines;
    for (size_t i = 0; i < items.size(); ++i) {
        // start from the proxy quad expanded by one proxy pixel (outside the paper)
        const Shape outer = expandShape(items[i].outer, proxyScale * 0.5);
        for (int s = 0; s < 4; ++s) {
            const Vec2 a = outer.quad.p[s], b = outer.quad.p[(s + 1) % 4];
            const double len = norm(b - a);
            const Vec2 d = normalized(b - a);
            const int count = std::clamp(static_cast<int>(len / std::max(4.0, len / 160.0)), 8, 200);
            for (int k = 0; k < count; ++k) {
                const double t = (k + 0.5) / count;
                lines.push_back({i, s, Kind::outer, a + (b - a) * t, perp(d), t, half, extraIn, 0});
            }
        }
        const Shape& in = items[i].inner;
        if (in.kind == ShapeKind::quad) {
            for (int s = 0; s < 4; ++s) {
                const Vec2 a = in.quad.p[s], b = in.quad.p[(s + 1) % 4];
                const double len = norm(b - a);
                const Vec2 d = normalized(b - a);
                const int count = std::clamp(static_cast<int>(len / std::max(6.0, len / 100.0)), 8, 120);
                for (int k = 0; k < count; ++k) {
                    const double t = 0.15 + 0.7 * (k + 0.5) / count;
                    lines.push_back({i, s, Kind::inner, a + (b - a) * t, perp(d), t, half, 0, 0});
                }
            }
        } else {
            const Ellipse& e = in.ellipse;
            for (int k = 0; k < 180; ++k) {
                const double th = k * 2 * kPi / 180;
                const Vec2 dir{std::cos(th), std::sin(th)};
                lines.push_back({i, k, Kind::innerCircle, e.c + dir * e.rx, dir * -1.0, th, half, 0, 0});
            }
        }
    }

    // ---- 2. pixel needs, sorted by row ----
    std::vector<Need> needs;
    size_t total = 0;
    for (auto& l : lines) {
        l.offset = total;
        total += static_cast<size_t>(2 * l.half + 1 + l.extraIn);
    }
    std::vector<Sample> samples(total);
    needs.reserve(total);
    for (const auto& l : lines) {
        for (int t = -l.half; t <= l.half + l.extraIn; ++t) {
            const Vec2 p = l.base + l.n * static_cast<double>(t);
            const int64_t x = std::clamp<int64_t>(static_cast<int64_t>(std::floor(p.x)), 0, info.width - 1);
            const int64_t y = std::clamp<int64_t>(static_cast<int64_t>(std::floor(p.y)), 0, info.height - 1);
            const size_t idx = l.offset + static_cast<size_t>(t + l.half);
            samples[idx].tau = static_cast<float>(dot(Vec2{x + 0.5, y + 0.5} - l.base, l.n));
            needs.push_back({y, x, static_cast<uint32_t>(idx)});
        }
    }
    std::sort(needs.begin(), needs.end(),
              [](const Need& a, const Need& b) { return a.y < b.y || (a.y == b.y && a.x < b.x); });

    // ---- 3. one increasing-row pass ----
    const int ch = info.channels;
    const bool gray = ch <= 2;
    const int bps = info.bytesPerSample();
    const double normv = info.bitsPerSample == 16 ? 65535.0 : 255.0;
    constexpr int64_t kBand = 64;
    auto band = AccountedBuffer::allocate(MemCategory::band, info.rowBytes() * kBand);
    if (!band) return band.error();
    size_t ni = 0;
    while (ni < needs.size()) {
        if (cancel.cancelled()) return cancelledError();
        const int64_t y0 = needs[ni].y;
        const int64_t rows = std::min<int64_t>(kBand, info.height - y0);
        FK_TRY(src.readRows(y0, rows, 0, info.width, band->data(), info.rowBytes()));
        while (ni < needs.size() && needs[ni].y < y0 + rows) {
            const Need& nd = needs[ni++];
            const uint8_t* px = band->data() + static_cast<size_t>(nd.y - y0) * info.rowBytes() +
                                static_cast<size_t>(nd.x) * ch * bps;
            float v[3];
            for (int c = 0; c < (gray ? 1 : 3); ++c) {
                if (bps == 1)
                    v[c] = static_cast<float>(px[c] / normv);
                else {
                    uint16_t t;
                    std::memcpy(&t, px + c * 2, 2);
                    v[c] = static_cast<float>(t / normv);
                }
            }
            if (gray) v[1] = v[2] = v[0];
            std::memcpy(samples[nd.sample].v, v, sizeof(v));
        }
        report(progress, static_cast<double>(ni) / needs.size(), "refine");
    }

    // ---- 4. analysis per item ----
    for (size_t i = 0; i < items.size(); ++i) {
        DetectedItem& item = items[i];
        RefineStats& st = stats[i];
        const Shape start = expandShape(item.outer, proxyScale * 0.5);
        const double cornerZone = std::max(4.0 * pxPerMm, item.cornerRadiusPx + 1.0 * pxPerMm);
        std::array<Line, 4> finalLines{};
        int validTotal = 0, linesTotal = 0;
        std::vector<double> contrasts, residAll;
        for (int s = 0; s < 4; ++s) {
            const Vec2 a = start.quad.p[s], b = start.quad.p[(s + 1) % 4];
            const double len = norm(b - a);
            const Line startLine{a, normalized(b - a)};
            if (locked && i < locked->size() && (*locked)[i][s]) {
                // abuts a neighbour or the scan border: nothing to measure against
                finalLines[s] = Line{startLine.p - perp(startLine.d) * safetyPx, startLine.d};
                st.sidesLocked++;
                continue;
            }
            std::vector<Vec2> pts, fitPts;
            std::vector<double> alongs;
            for (const auto& l : lines) {
                if (l.item != i || l.kind != Kind::outer || l.side != s) continue;
                ++linesTotal;
                double C = 0, noise = 1;
                const double tau = analyse(&samples[l.offset], 2 * l.half + 1 + l.extraIn, true, C, noise, 3);
                if (noise > 0) contrasts.push_back(C / noise);
                if (std::isnan(tau)) continue;
                ++validTotal;
                const Vec2 p = l.base + l.n * tau;
                pts.push_back(p);
                alongs.push_back(l.along);
                const double along = l.along * len;
                if (along > cornerZone && along < len - cornerZone) fitPts.push_back(p);
            }
            if (fitPts.size() < 6) {
                // cannot refine this side: keep the (outward) proxy estimate
                finalLines[s] = Line{startLine.p - perp(startLine.d) * safetyPx, startLine.d};
                st.sidesKept++;
                continue;
            }
            auto fit = cv::fitLineRobust(fitPts);
            Line L = fit ? *fit : startLine;
            if (dot(L.d, startLine.d) < 0) L.d = L.d * -1.0;
            // residual spread
            std::vector<double> res;
            for (const Vec2& p : fitPts)
                res.push_back(signedDistance(L, p));
            const double med = cv::median(res);
            const double sig = cv::madSigma(res, med);
            residAll.push_back(sig);
            // containment: accepted outward residuals (isolated single-line outliers ignored)
            std::vector<double> outward(pts.size());
            for (size_t k = 0; k < pts.size(); ++k)
                outward[k] = -signedDistance(L, pts[k]);
            double shift = 0;
            const double outlierGate = 4.0 * sig + 2.0;
            for (size_t k = 0; k < pts.size(); ++k) {
                double o = outward[k];
                if (o <= shift) continue;
                if (o - (-med) > outlierGate) {
                    const bool prev = k > 0 && outward[k - 1] > o - 2.0;
                    const bool next = k + 1 < pts.size() && outward[k + 1] > o - 2.0;
                    if (!prev && !next) continue; // isolated speck, not paper
                }
                shift = o;
            }
            finalLines[s] = Line{L.p - perp(L.d) * (shift + safetyPx), L.d};
        }
        st.measured = linesTotal > 0;
        st.validFraction = linesTotal ? static_cast<double>(validTotal) / linesTotal : 0.0;
        st.contrast = contrasts.empty() ? 0.0 : cv::median(contrasts);
        st.residualSigma = residAll.empty() ? 0.0 : *std::max_element(residAll.begin(), residAll.end());
        std::array<Vec2, 4> corners;
        bool ok = true;
        for (int s = 0; s < 4; ++s) {
            auto c = intersect(finalLines[(s + 3) % 4], finalLines[s]);
            if (!c) {
                ok = false;
                break;
            }
            corners[s] = *c;
        }
        Shape refined = ok ? Shape::fromQuad(normalizeQuad(corners)) : expandShape(start, safetyPx);
        if (!isValid(refined)) refined = expandShape(start, safetyPx);
        for (int s = 0; s < 4; ++s) {
            const Vec2 a = item.outer.quad.p[s], b = item.outer.quad.p[(s + 1) % 4];
            const Line orig{a, normalized(b - a)};
            const Vec2 mid = (refined.quad.p[s] + refined.quad.p[(s + 1) % 4]) * 0.5;
            item.refineOffsetPx[s] = -signedDistance(orig, mid);
        }
        item.outer = refined;

        // ---- inner ----
        Shape& in = item.inner;
        int innerValid = 0, innerTotal = 0;
        if (in.kind == ShapeKind::quad) {
            std::array<Line, 4> il{};
            bool allOk = true;
            for (int s = 0; s < 4; ++s) {
                const Vec2 a = in.quad.p[s], b = in.quad.p[(s + 1) % 4];
                const Line startLine{a, normalized(b - a)};
                std::vector<Vec2> pts;
                for (const auto& l : lines) {
                    if (l.item != i || l.kind != Kind::inner || l.side != s) continue;
                    ++innerTotal;
                    double C = 0, noise = 1;
                    // orientation: inner normal points into the image; frame is at τ < 0
                    const double tau = analyse(&samples[l.offset], 2 * l.half + 1, false, C, noise, 2);
                    if (std::isnan(tau)) continue;
                    ++innerValid;
                    pts.push_back(l.base + l.n * tau);
                }
                if (pts.size() < 6) {
                    il[s] = startLine;
                    continue;
                }
                auto fit = cv::fitLineRobust(pts);
                if (!fit) {
                    il[s] = startLine;
                    continue;
                }
                Line L = *fit;
                if (dot(L.d, startLine.d) < 0) L.d = L.d * -1.0;
                // do not move further than half the search window from the proxy estimate
                const double moved = signedDistance(startLine, L.p);
                if (std::abs(moved) > half * 0.9) {
                    il[s] = startLine;
                    continue;
                }
                il[s] = L;
            }
            std::array<Vec2, 4> c;
            for (int s = 0; s < 4 && allOk; ++s) {
                auto v = intersect(il[(s + 3) % 4], il[s]);
                if (!v)
                    allOk = false;
                else
                    c[s] = *v;
            }
            if (allOk) {
                Shape ni2 = Shape::fromQuad(normalizeQuad(c));
                if (isValid(ni2)) in = ni2;
            }
        } else {
            std::vector<Vec2> pts;
            for (const auto& l : lines) {
                if (l.item != i || l.kind != Kind::innerCircle) continue;
                ++innerTotal;
                double C = 0, noise = 1;
                const double tau = analyse(&samples[l.offset], 2 * l.half + 1, false, C, noise, 2);
                if (std::isnan(tau)) continue;
                ++innerValid;
                pts.push_back(l.base + l.n * tau);
            }
            if (pts.size() >= 40) {
                auto circ = cv::fitCircleRobust(pts);
                if (circ && std::abs(circ->r - in.ellipse.rx) < half && norm(circ->c - in.ellipse.c) < half)
                    in = Shape::fromEllipse({circ->c, circ->r, circ->r, 0});
            }
        }
        st.innerValidFraction = innerTotal ? static_cast<double>(innerValid) / innerTotal : 0.0;
        // inner confidence nudged by full-resolution evidence
        if (st.innerValidFraction < 0.4)
            item.innerConfidence = std::min(item.innerConfidence, 0.55);
        else if (st.innerValidFraction > 0.8)
            item.innerConfidence = std::min(1.0, item.innerConfidence + 0.1);
    }
    return {};
}

} // namespace fk::detail

namespace fk {

Result<DetectedItem> snapToEdges(ImageSource& src, const DetectedItem& seed, double dpi,
                                 const CancelToken& cancel) {
    std::vector<DetectedItem> items{seed};
    std::vector<detail::RefineStats> st;
    const int scale = std::max(1, static_cast<int>(std::round(dpi / 300.0)));
    FK_TRY(detail::refineItems(src, items, dpi, scale, 1.0, st, cancel, {}));
    return items[0];
}

} // namespace fk
