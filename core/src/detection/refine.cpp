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
/// Outer edge on a backing lighter than the paper: fraction of the backing–paper luminance difference by
/// which the luminance must drop below the paper's to end the paper (shadow band, dark edge line). On the
/// owner's 826 dpi white-on-white scan the band is 0.3–1.0 of that difference darker than the paper.
constexpr double kPaperDeparture = 0.3;

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
    float tau;            ///< projection of the pixel centre on the line normal, relative to base
    bool outside = false; ///< the position lies outside the scan (the value repeats the border pixel)
};

double medianOf(std::vector<double> v) {
    return cv::median(std::move(v));
}

/// Inner edge: frame at the start of the profile (τ < 0), picture at the end; the crossing is the first
/// position where the 3-px smoothed distance to the frame exceeds half the contrast for persist+1 samples.
/// Returns τ or NaN.
double analyse(const Sample* s, int n, double& contrastOut, double& noiseOut, int persist) {
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
    if (C < std::max(4.0 * noise, 4.0)) return std::numeric_limits<double>::quiet_NaN();
    std::vector<double> sm(n);
    for (int i = 0; i < n; ++i) {
        const int i0 = std::max(0, i - 1), i1 = std::min(n - 1, i + 1);
        sm[i] = (dist[i0] + dist[i] + dist[i1]) / 3.0;
    }
    const double thr = 0.5 * C;
    for (int i = 1; i + persist < n; ++i) {
        bool ok = true;
        for (int k = 0; k <= persist; ++k)
            if (sm[i + k] <= thr) {
                ok = false;
                break;
            }
        if (!ok) continue;
        const double f = (thr - sm[i - 1]) / std::max(1e-9, sm[i] - sm[i - 1]);
        return s[i - 1].tau + std::clamp(f, 0.0, 1.0) * (s[i].tau - s[i - 1].tau);
    }
    return std::numeric_limits<double>::quiet_NaN();
}

/// Outer edge, paper side first (UX-02, U012; RFC-0004 §Stage B). Walking from the paper end of the profile
/// outward, the paper ends at the first of:
///  - **toward the backing**: the colour, projected on the paper→backing direction, passes half way (a
///  quarter
///    on a backing lighter than the paper), persisting persist+1 samples. On a clean step this is the
///    crossing seen from the backing side; on a darker backing a rolled, darker paper edge ("shoulder") stays
///    paper until the step; on a lighter backing the soft shade the print casts rises to the lid from the
///    paper edge.
///  - **darker than the paper on a lighter backing**: the luminance drops below the paper's by
///    `kPaperDeparture` of the backing–paper luminance difference for persist samples — the shadow band and
///    the dark edge line along a print on a white lid (≈ 0.5–2 mm, darker than both). A colour shift
///    without darkening (yellowed paper) stays paper; an edge darkened by more than that fraction would be
///    cut (RFC-0004 §Known limits).
/// Positions beyond the scan border: when the paper reaches the border, the edge is the border (`atBorder`) —
/// the paper continues outside the scan and must be contained; such profiles use the print's contrast
/// (`contrastHint`, from complete profiles) and only the "toward" rule. Returns τ or NaN.
double analyseOuter(const Sample* s, int n, double contrastHint, double& contrastOut, double& noiseOut,
                    int persist, double rampMinPx, bool& atBorder, bool& truncated,
                    std::array<double, 3>& paperOut) {
    atBorder = false;
    truncated = false;
    contrastOut = std::numeric_limits<double>::quiet_NaN();
    int k0 = 0;
    while (k0 < n && s[k0].outside)
        ++k0;
    const int q = std::max(3, n / 5);
    if (k0 >= n - 2 * q) return std::numeric_limits<double>::quiet_NaN();
    double a[3], b[3];
    for (int c = 0; c < 3; ++c) {
        std::vector<double> c0, c1;
        for (int i = 0; i < q; ++i) {
            c0.push_back(s[k0 + i].v[c]);
            c1.push_back(s[n - 1 - i].v[c]);
        }
        a[c] = medianOf(c0);
        b[c] = medianOf(c1);
    }
    paperOut = {b[0], b[1], b[2]};
    std::vector<double> distB(n), smB(n);
    for (int i = 0; i < n; ++i) {
        double d2 = 0;
        for (int c = 0; c < 3; ++c)
            d2 += (s[i].v[c] - b[c]) * (s[i].v[c] - b[c]);
        distB[i] = std::sqrt(d2) * 100.0;
    }
    for (int i = 0; i < n; ++i) {
        const int i0 = std::max(k0, i - 1), i1 = std::min(n - 1, i + 1);
        smB[i] = (distB[i0] + distB[i] + distB[i1]) / 3.0;
    }
    // A profile cut off by the scan border may keep only a sliver of background (or none): its first q
    // samples mix in paper. Its background is then its most distant in-scan samples (median of three) and,
    // for the threshold, the print's contrast from complete profiles (`contrastHint`) when there is one.
    if (k0 > 0) {
        std::vector<std::pair<double, int>> far;
        for (int i = k0; i < (k0 + n) / 2; ++i)
            far.push_back({distB[i], i});
        std::sort(far.begin(), far.end(), [](const auto& x, const auto& y) { return x.first > y.first; });
        far.resize(std::min<size_t>(3, far.size()));
        for (int c = 0; c < 3; ++c) {
            std::vector<double> v;
            for (const auto& f : far)
                v.push_back(s[f.second].v[c]);
            a[c] = medianOf(v);
        }
    }
    double C2 = 0;
    for (int c = 0; c < 3; ++c)
        C2 += (b[c] - a[c]) * (b[c] - a[c]);
    const double own = std::sqrt(C2) * 100.0;
    const double C = k0 > 0 && contrastHint > 0 ? contrastHint : own;
    std::vector<double> tail(distB.end() - q, distB.end());
    const double noise = std::max(0.3, cv::madSigma(tail, cv::median(tail)) + 0.5 * cv::median(tail));
    contrastOut = own;
    truncated = k0 > 0;
    noiseOut = noise;
    // the contrast is judged against the backing's noise (as before U012): the paper's own texture (dust,
    // handwriting) must not discard a measurable edge
    double noiseBg = noise;
    if (k0 == 0) {
        std::vector<double> head;
        for (int i = 0; i < q; ++i) {
            double d2 = 0;
            for (int c = 0; c < 3; ++c)
                d2 += (s[i].v[c] - a[c]) * (s[i].v[c] - a[c]);
            head.push_back(std::sqrt(d2) * 100.0);
        }
        noiseBg = std::max(0.3, cv::madSigma(head, cv::median(head)) + 0.5 * cv::median(head));
    }
    if (C < std::max(4.0 * noiseBg, 3.0) || (k0 > 0 && own < std::max(4.0 * noise, 3.0))) {
        if (k0 > 0) { // nothing but paper up to the scan border
            atBorder = true;
            return s[k0].tau - 0.5;
        }
        return std::numeric_limits<double>::quiet_NaN();
    }
    // projection on the paper→background direction (0 = paper, 1 = background), smoothed like smB
    const bool ownBackground = k0 == 0;
    std::vector<double> proj(n, 0.0);
    if (ownBackground) {
        double ab2 = 0;
        for (int c = 0; c < 3; ++c)
            ab2 += (a[c] - b[c]) * (a[c] - b[c]);
        std::vector<double> raw(n);
        for (int i = 0; i < n; ++i) {
            double d = 0;
            for (int c = 0; c < 3; ++c)
                d += (s[i].v[c] - b[c]) * (a[c] - b[c]);
            raw[i] = d / std::max(1e-12, ab2);
        }
        for (int i = 0; i < n; ++i) {
            const int i0 = std::max(0, i - 1), i1 = std::min(n - 1, i + 1);
            proj[i] = (raw[i0] + raw[i] + raw[i1]) / 3.0;
        }
    }
    // darkening relative to the paper (luminance, scaled like the RGB distances: √3·ΔL·100); the "away" rule
    // applies only on a backing lighter than the paper (a shadow band is darker than both)
    std::vector<double> dark(n, 0.0);
    const double Lb = (b[0] + b[1] + b[2]) / 3.0, La = (a[0] + a[1] + a[2]) / 3.0;
    const double kL = std::sqrt(3.0) * 100.0;
    const bool lighterBacking = ownBackground && (La - Lb) * kL > std::max(4.0 * noise, 3.0);
    if (lighterBacking) {
        std::vector<double> raw(n);
        for (int i = 0; i < n; ++i)
            raw[i] = (Lb - (s[i].v[0] + s[i].v[1] + s[i].v[2]) / 3.0) * kL;
        for (int i = 0; i < n; ++i) {
            const int i0 = std::max(0, i - 1), i1 = std::min(n - 1, i + 1);
            dark[i] = (raw[i0] + raw[i] + raw[i1]) / 3.0;
        }
    }
    const double thrAway = std::max({kPaperDeparture * (La - Lb) * kL, 4.0 * noise, 3.0});
    const double thrToward = 0.5 * C;
    // On a lighter backing the print casts a soft shade: the colour rises to the lid over a ramp that starts
    // at the paper edge, so a quarter of the way is already past the paper — but only on a ramp (a quarter to
    // three quarters wider than `rampMinPx`); on a sharp step a quarter of the way is still inside the
    // blurred edge, and half way is the edge. On a darker backing a rolled, darker paper edge ("shoulder")
    // darkens toward the backing: half way.
    const double towardLevel = lighterBacking ? 0.25 : 0.5;
    auto crossing = [&](const std::vector<double>& v, int i, double level) {
        const double f = (level - v[i + 1]) / (std::abs(v[i] - v[i + 1]) < 1e-12 ? 1e-12 : v[i] - v[i + 1]);
        return s[i + 1].tau + std::clamp(f, 0.0, 1.0) * (s[i].tau - s[i + 1].tau);
    };
    // first position at or beyond index `from` (walking outward) where proj exceeds `level`, or NaN
    auto beyond = [&](int from, double level) {
        for (int j = from; j >= k0; --j)
            if (proj[j] > level) return j + 1 < n ? crossing(proj, j, level) : s[j].tau;
        return std::numeric_limits<double>::quiet_NaN();
    };
    for (int i = n - 2; i >= k0 + persist; --i) {
        bool toward = true;
        for (int k = 0; k <= persist; ++k)
            if (ownBackground ? proj[i - k] <= towardLevel : smB[i - k] <= thrToward) toward = false;
        // darkening: persist samples, or — the edge line on the lit sides is only 2–3 px wide — two samples
        // reaching 1.5× the threshold
        bool away = false;
        if (lighterBacking && dark[i] > thrAway) {
            int run = 0;
            double peak = 0;
            for (int j = i; j >= k0 && dark[j] > thrAway && run < persist; --j, ++run)
                peak = std::max(peak, dark[j]);
            away = run >= persist || (run >= 2 && peak >= 1.5 * thrAway);
        }
        if (toward) {
            if (!ownBackground) return crossing(smB, i, thrToward);
            const double t25 = crossing(proj, i, towardLevel);
            if (!lighterBacking) return t25;
            const double t50 = beyond(i, 0.5), t75 = beyond(i, 0.75);
            if (!std::isnan(t75) && std::abs(t25 - t75) > rampMinPx) return t25; // shade ramp
            return std::isnan(t50) ? t25 : t50;                                  // step: half way
        }
        if (away) return crossing(dark, i, thrAway);
    }
    if (k0 > 0) { // paper all the way to the scan border
        atBorder = true;
        return s[k0].tau - 0.5;
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
            const int64_t ux = static_cast<int64_t>(std::floor(p.x)),
                          uy = static_cast<int64_t>(std::floor(p.y));
            const int64_t x = std::clamp<int64_t>(ux, 0, info.width - 1);
            const int64_t y = std::clamp<int64_t>(uy, 0, info.height - 1);
            const size_t idx = l.offset + static_cast<size_t>(t + l.half);
            samples[idx].outside = ux != x || uy != y;
            samples[idx].tau =
                static_cast<float>(samples[idx].outside ? t : dot(Vec2{x + 0.5, y + 0.5} - l.base, l.n));
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
        const double rampMin = std::max(6.0, 0.2 * pxPerMm);
        // the print's paper–background contrast from profiles that lie wholly inside the scan
        double itemC = 0;
        {
            std::vector<double> cs;
            for (const auto& l : lines) {
                if (l.item != i || l.kind != Kind::outer) continue;
                double C = 0, noise = 1;
                bool atBorder = false, truncated = false;
                std::array<double, 3> paper{};
                analyseOuter(&samples[l.offset], 2 * l.half + 1 + l.extraIn, 0, C, noise, 3, rampMin,
                             atBorder, truncated, paper);
                if (!truncated && !std::isnan(C)) cs.push_back(C);
            }
            if (!cs.empty()) itemC = cv::median(cs);
        }
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
            // the inner end of every profile must be this side's paper: near the corners a profile may end in
            // the neighbouring side's shadow band or on the backing (its "edge" would be meaningless)
            struct OuterHit {
                const ProfileLine* l;
                double tau, C, noise;
                bool atBorder, truncated;
                std::array<double, 3> paper;
            };
            std::vector<OuterHit> hits;
            for (const auto& l : lines) {
                if (l.item != i || l.kind != Kind::outer || l.side != s) continue;
                ++linesTotal;
                OuterHit h{&l, 0, 0, 1, false, false, {}};
                h.tau = analyseOuter(&samples[l.offset], 2 * l.half + 1 + l.extraIn, itemC, h.C, h.noise, 3,
                                     rampMin, h.atBorder, h.truncated, h.paper);
                if (h.noise > 0 && !std::isnan(h.C)) contrasts.push_back(h.C / h.noise);
                hits.push_back(h);
            }
            std::array<double, 3> sidePaper{};
            double sideC = 0;
            {
                std::array<std::vector<double>, 3> pc;
                std::vector<double> cs;
                for (const auto& h : hits)
                    if (!std::isnan(h.tau)) {
                        for (int c = 0; c < 3; ++c)
                            pc[c].push_back(h.paper[c]);
                        if (!h.truncated && !std::isnan(h.C)) cs.push_back(h.C);
                    }
                for (int c = 0; c < 3; ++c)
                    sidePaper[c] = pc[c].empty() ? 0.0 : cv::median(pc[c]);
                sideC = cs.empty() ? 0.0 : cv::median(cs);
            }
            std::vector<Vec2> pts, fitPts;
            std::vector<double> alongs;
            for (const auto& h : hits) {
                if (std::isnan(h.tau)) continue;
                double d2 = 0;
                for (int c = 0; c < 3; ++c)
                    d2 += (h.paper[c] - sidePaper[c]) * (h.paper[c] - sidePaper[c]);
                if (std::sqrt(d2) * 100.0 > std::max(0.25 * sideC, 4.0 * h.noise)) continue; // not this paper
                ++validTotal;
                const ProfileLine& l = *h.l;
                const Vec2 p = l.base + l.n * h.tau;
                pts.push_back(p);
                alongs.push_back(l.along);
                const double along = l.along * len;
                if (along > cornerZone && along < len - cornerZone && !h.atBorder) fitPts.push_back(p);
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
                    // corner zones: profiles there cross the neighbouring side's shadow band or the rounded
                    // corner, and the corner itself is contained by the two side lines — an outlier there is
                    // not paper beyond the line
                    const double along = alongs[k] * len;
                    if (along < cornerZone || along > len - cornerZone) continue;
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
                    const double tau = analyse(&samples[l.offset], 2 * l.half + 1, C, noise, 2);
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
                const double tau = analyse(&samples[l.offset], 2 * l.half + 1, C, noise, 2);
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
