// Stage A detection on the proxy (RFC-0004): background model, mask, components, rectangles,
// film type + orientation + inner shape. Then stage B (refine.cpp), confidence and ordering.
#include "detect_internal.hpp"
#include "fk/color.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numbers>

namespace fk {
namespace detail {

LabImage toLab(const Proxy& p) {
    LabImage lab;
    lab.w = p.width;
    lab.h = p.height;
    lab.v.resize(p.rgb.size());
    for (size_t i = 0; i < p.rgb.size(); i += 3)
        srgbToLab(&p.rgb[i], &lab.v[i]);
    return lab;
}

void sampleLab(const LabImage& lab, Vec2 p, float out[3]) {
    const double x = std::clamp(p.x - 0.5, 0.0, static_cast<double>(lab.w - 1));
    const double y = std::clamp(p.y - 0.5, 0.0, static_cast<double>(lab.h - 1));
    const int64_t x0 = static_cast<int64_t>(x), y0 = static_cast<int64_t>(y);
    const int64_t x1 = std::min(x0 + 1, lab.w - 1), y1 = std::min(y0 + 1, lab.h - 1);
    const float fx = static_cast<float>(x - x0), fy = static_cast<float>(y - y0);
    const float *a = lab.at(x0, y0), *b = lab.at(x1, y0), *c = lab.at(x0, y1), *d = lab.at(x1, y1);
    for (int k = 0; k < 3; ++k)
        out[k] = (a[k] * (1 - fx) + b[k] * fx) * (1 - fy) + (c[k] * (1 - fx) + d[k] * fx) * fy;
}

} // namespace detail

namespace {

using detail::LabImage;
constexpr double kPi = std::numbers::pi;

struct Background {
    std::array<std::array<double, 6>, 3> poly{}; ///< L, a, b surfaces over normalised coords
    double noise = 1.0;                          ///< σ of ΔE on background
    int64_t w = 1, h = 1;
    void at(int64_t x, int64_t y, float out[3]) const {
        const double u = 2.0 * (x + 0.5) / w - 1.0, v = 2.0 * (y + 0.5) / h - 1.0;
        for (int c = 0; c < 3; ++c)
            out[c] = static_cast<float>(cv::evalPoly2(poly[c], u, v));
    }
};

Background modelBackground(const LabImage& lab) {
    Background bg;
    bg.w = lab.w;
    bg.h = lab.h;
    const int64_t band = std::max<int64_t>(4, std::min(lab.w, lab.h) / 50);
    std::vector<double> Ls, As, Bs;
    for (int64_t y = 0; y < lab.h; y += 2)
        for (int64_t x = 0; x < lab.w; x += 2) {
            if (x >= band && y >= band && x < lab.w - band && y < lab.h - band) continue;
            const float* p = lab.at(x, y);
            Ls.push_back(p[0]);
            As.push_back(p[1]);
            Bs.push_back(p[2]);
        }
    const float med[3] = {static_cast<float>(cv::median(Ls)), static_cast<float>(cv::median(As)),
                          static_cast<float>(cv::median(Bs))};
    std::vector<double> d0;
    d0.reserve(Ls.size());
    for (size_t i = 0; i < Ls.size(); ++i) {
        const float p[3] = {static_cast<float>(Ls[i]), static_cast<float>(As[i]), static_cast<float>(Bs[i])};
        d0.push_back(detail::deltaE(p, med));
    }
    double sigma = std::max(0.5, cv::madSigma(d0, cv::median(d0)));
    for (int c = 0; c < 3; ++c)
        bg.poly[c] = {med[c], 0, 0, 0, 0, 0};
    // Two iterations: classify background against the current model, refit the quadratic surface.
    for (int iter = 0; iter < 2; ++iter) {
        const double gate = std::max(3.0 * sigma + 3.0, 6.0);
        std::array<std::vector<std::array<double, 3>>, 3> samples;
        std::vector<double> dists;
        const int64_t step = std::max<int64_t>(2, std::min(lab.w, lab.h) / 300);
        for (int64_t y = 0; y < lab.h; y += step)
            for (int64_t x = 0; x < lab.w; x += step) {
                float b[3];
                bg.at(x, y, b);
                const float* p = lab.at(x, y);
                const double d = detail::deltaE(p, b);
                if (d > gate) continue;
                const double u = 2.0 * (x + 0.5) / lab.w - 1.0, v = 2.0 * (y + 0.5) / lab.h - 1.0;
                for (int c = 0; c < 3; ++c)
                    samples[c].push_back({u, v, static_cast<double>(p[c])});
                dists.push_back(d);
            }
        if (samples[0].size() < 50) break;
        for (int c = 0; c < 3; ++c)
            bg.poly[c] = cv::fitPoly2(samples[c]);
        sigma = std::max(0.5, cv::madSigma(dists, cv::median(dists)));
    }
    bg.noise = sigma;
    return bg;
}

/// Objects of at least this fraction of the smallest image area (Instax Mini, 46 × 62 mm) that are not
/// prints are reported as possible missed prints (UX-01): dust, scratches and labels are much smaller.
constexpr double kPossibleMissFraction = 0.25;
/// Mean colour difference (ΔE, 1 mm cells) above which a print-sized area is a faint object, and how much
/// of its bounding rectangle it must fill (cards and image areas fill ≥ 0.8; background unevenness ≤ 0.6).
constexpr double kFaintDeltaE = 1.5;
constexpr double kFaintRectangularity = 0.7;

struct Candidate {
    cv::RotRect rect; ///< proxy coordinates
    double rectangularity = 0;
    bool touchesEdge = false;
    bool split = false;
    bool imageOnly = false;
    bool fillsScan = false; ///< the scan is a single print (cropped tightly in the scanner software)
    const FilmType* type = nullptr;
    int quarterTurns = 0; ///< CW turns from upright to current
    double innerConf = 0;
    Shape innerLocal{}; ///< in rect-local coords (proxy px)
    std::vector<std::string> reasons;
};

bool insideRect(const cv::RotRect& r, Vec2 p) {
    const Vec2 d = p - r.c;
    const double s = d.x * std::cos(r.angle) + d.y * std::sin(r.angle);
    const double t = -d.x * std::sin(r.angle) + d.y * std::cos(r.angle);
    return std::abs(s) <= r.w / 2 && std::abs(t) <= r.h / 2;
}

/// UX-01: print-sized objects that differ from the background only faintly (a black frame and a dark picture
/// on a black background) stay below the detection threshold. They are counted on a 1 mm grid, where scanner
/// noise averages out, against a background surface fitted to the scan's border only (the detection's
/// background model also fits faint objects, which hides them). Only compact, card-like areas count: an
/// uneven background is irregular.
int countFaintObjects(const LabImage& lab, double mmpp, double minAreaPx,
                      const std::vector<Candidate>& cands) {
    const int64_t cell = std::max<int64_t>(1, std::lround(1.0 / mmpp));
    const int64_t gw = lab.w / cell, gh = lab.h / cell;
    if (gw < 16 || gh < 16) return 0;
    std::vector<std::array<float, 3>> mean(static_cast<size_t>(gw * gh));
    std::vector<uint8_t> covered(mean.size(), 0);
    const double n = static_cast<double>(cell * cell);
    for (int64_t gy = 0; gy < gh; ++gy)
        for (int64_t gx = 0; gx < gw; ++gx) {
            const Vec2 c{(gx + 0.5) * cell, (gy + 0.5) * cell};
            const size_t i = static_cast<size_t>(gy * gw + gx);
            covered[i] = std::any_of(cands.begin(), cands.end(),
                                     [&](const Candidate& k) { return insideRect(k.rect, c); });
            double sum[3] = {0, 0, 0};
            for (int64_t y = gy * cell; y < (gy + 1) * cell; ++y)
                for (int64_t x = gx * cell; x < (gx + 1) * cell; ++x) {
                    const float* p = lab.at(x, y);
                    for (int k = 0; k < 3; ++k)
                        sum[k] += p[k];
                }
            for (int k = 0; k < 3; ++k)
                mean[i][k] = static_cast<float>(sum[k] / n);
        }
    // background surface from the border cells (5 mm band), refitted once without outliers
    const int64_t band = 5;
    auto uv = [&](int64_t gx, int64_t gy) {
        return std::pair<double, double>{2.0 * (gx + 0.5) / gw - 1.0, 2.0 * (gy + 0.5) / gh - 1.0};
    };
    std::array<std::array<double, 6>, 3> poly{};
    auto residual = [&](int64_t gx, int64_t gy) {
        const auto [u, v] = uv(gx, gy);
        const auto& m = mean[static_cast<size_t>(gy * gw + gx)];
        double d2 = 0;
        for (int k = 0; k < 3; ++k) {
            const double d = m[k] - cv::evalPoly2(poly[k], u, v);
            d2 += d * d;
        }
        return std::sqrt(d2);
    };
    double sigma = 0;
    for (int iter = 0; iter < 2; ++iter) {
        std::array<std::vector<std::array<double, 3>>, 3> samples;
        std::vector<double> res;
        for (int64_t gy = 0; gy < gh; ++gy)
            for (int64_t gx = 0; gx < gw; ++gx) {
                if (gx >= band && gy >= band && gx < gw - band && gy < gh - band) continue;
                if (covered[static_cast<size_t>(gy * gw + gx)]) continue;
                if (iter > 0 && residual(gx, gy) > std::max(3.0 * sigma, 1.0)) continue;
                const auto [u, v] = uv(gx, gy);
                const auto& m = mean[static_cast<size_t>(gy * gw + gx)];
                for (int k = 0; k < 3; ++k)
                    samples[k].push_back({u, v, m[k]});
            }
        if (samples[0].size() < 50) return 0;
        for (int k = 0; k < 3; ++k)
            poly[k] = cv::fitPoly2(samples[k]);
        for (int64_t gy = 0; gy < gh; ++gy)
            for (int64_t gx = 0; gx < gw; ++gx)
                if (!(gx >= band && gy >= band && gx < gw - band && gy < gh - band) &&
                    !covered[static_cast<size_t>(gy * gw + gx)])
                    res.push_back(residual(gx, gy));
        sigma = cv::madSigma(res, cv::median(res)) + cv::median(res);
    }
    const double thr = std::max(kFaintDeltaE, 4.0 * sigma);
    cv::Mask m(gw, gh);
    for (int64_t gy = 0; gy < gh; ++gy)
        for (int64_t gx = 0; gx < gw; ++gx)
            m.at(gx, gy) = !covered[static_cast<size_t>(gy * gw + gx)] && residual(gx, gy) > thr ? 1 : 0;
    cv::openMask(m, 2); // dust, scratches and handwriting are thinner than 5 mm
    cv::closeMask(m, 2);
    std::vector<int32_t> labels;
    const auto comps = cv::connectedComponents(m, labels);
    const double minCells = minAreaPx / static_cast<double>(cell * cell);
    int found = 0;
    for (const auto& c : comps) {
        if (static_cast<double>(c.area) < minCells) continue;
        const cv::RotRect r = cv::minAreaRect(cv::convexHull(c.boundaryCorners));
        if (static_cast<double>(c.area) >= kFaintRectangularity * r.area()) ++found;
    }
    return found;
}

// rect-local (s,t) → proxy coords
Vec2 localToProxy(const cv::RotRect& r, double s, double t) {
    const Vec2 u{std::cos(r.angle), std::sin(r.angle)}, v{-std::sin(r.angle), std::cos(r.angle)};
    return r.c + u * (s - r.w / 2) + v * (t - r.h / 2);
}

/// Per-side inner-edge profiles, measured inward from each side of the print rectangle.
struct SideProfile {
    double band0 = 0;         ///< depth (proxy px) of the first sample
    std::vector<double> frac; ///< fraction of non-frame pixels on the line at depth band0 + i
    std::vector<double> grad; ///< line-averaged |ΔE| between depth+1 and depth−1 (straight edges peak)
    double gbase = 1;         ///< typical gradient inside the frame band (texture level)
    double fracEdgeMm =
        std::numeric_limits<double>::quiet_NaN(); ///< first sustained frac ≥ 0.5 (round frames)
};

struct EdgePick {
    bool found = false;
    double depthPx = 0;
    double strength = 0; ///< gradient peak / frame texture level
};

std::array<SideProfile, 4> sideProfiles(const LabImage& lab, const cv::RotRect& r, double mmpp,
                                        std::array<std::array<float, 3>, 4>& frameColor,
                                        std::array<float, 4>& tau) {
    std::array<SideProfile, 4> out{};
    const double band0 = 1.0 / mmpp, band1 = 3.0 / mmpp;
    for (int side = 0; side < 4; ++side) {
        const bool horiz = side == 0 || side == 2;
        const double len = horiz ? r.w : r.h;
        const double depthMax = 0.47 * (horiz ? r.h : r.w);
        auto pointAt = [&](double along, double depth) {
            switch (side) {
            case 0:
                return localToProxy(r, along, depth);
            case 1:
                return localToProxy(r, r.w - depth, along);
            case 2:
                return localToProxy(r, along, r.h - depth);
            default:
                return localToProxy(r, depth, along);
            }
        };
        const double a0 = 0.2 * len, a1 = 0.8 * len;
        std::vector<double> L, A, B;
        for (double d = band0; d <= band1; d += 1.0)
            for (double a = a0; a <= a1; a += 1.0) {
                float c[3];
                detail::sampleLab(lab, pointAt(a, d), c);
                L.push_back(c[0]);
                A.push_back(c[1]);
                B.push_back(c[2]);
            }
        std::array<float, 3> F = {static_cast<float>(cv::median(L)), static_cast<float>(cv::median(A)),
                                  static_cast<float>(cv::median(B))};
        std::vector<double> dd;
        for (size_t i = 0; i < L.size(); ++i) {
            const float c[3] = {static_cast<float>(L[i]), static_cast<float>(A[i]), static_cast<float>(B[i])};
            dd.push_back(detail::deltaE(c, F.data()));
        }
        const float t = static_cast<float>(std::max(10.0, 4.0 * cv::madSigma(dd, cv::median(dd))));
        frameColor[side] = F;
        tau[side] = t;
        SideProfile& sp = out[side];
        sp.band0 = band0;
        // sample a grid once: rows of the line at each depth
        const int nAlong = std::max(1, static_cast<int>(a1 - a0));
        const int nDepth = std::max(3, static_cast<int>(depthMax - band0));
        std::vector<float> grid(static_cast<size_t>(nAlong) * (nDepth + 2) * 3);
        for (int di = -1; di <= nDepth; ++di)
            for (int ai = 0; ai < nAlong; ++ai)
                detail::sampleLab(lab, pointAt(a0 + ai, band0 + di), &grid[((di + 1) * nAlong + ai) * 3]);
        auto at = [&](int di, int ai) { return &grid[((di + 1) * nAlong + ai) * 3]; };
        for (int di = 0; di < nDepth; ++di) {
            int k = 0;
            double g = 0;
            for (int ai = 0; ai < nAlong; ++ai) {
                if (detail::deltaE(at(di, ai), F.data()) > t) ++k;
                g += detail::deltaE(at(di + 1, ai), at(di - 1, ai));
            }
            sp.frac.push_back(static_cast<double>(k) / nAlong);
            sp.grad.push_back(g / nAlong);
        }
        const int bandN = std::max(2, static_cast<int>(band1 - band0));
        std::vector<double> gb(sp.grad.begin(), sp.grad.begin() + std::min<size_t>(sp.grad.size(), bandN));
        sp.gbase = std::max(0.5, cv::median(gb));
        for (size_t i = 1; i + 2 < sp.frac.size(); ++i) {
            if (sp.frac[i] >= 0.5 && sp.frac[i + 1] >= 0.5 && sp.frac[i + 2] >= 0.5 && sp.frac[i - 1] < 0.5) {
                const double t0 = static_cast<double>(i - 1) +
                                  (0.5 - sp.frac[i - 1]) / std::max(1e-9, sp.frac[i] - sp.frac[i - 1]);
                sp.fracEdgeMm = (band0 + t0) * mmpp;
                break;
            }
        }
    }
    return out;
}

/// Strongest straight edge near the predicted depth (gradient peak weighted by distance to the prior).
EdgePick edgeNear(const SideProfile& sp, double predPx, double tolPx) {
    // Strongest straight edge (peak/texture ≥ 2.5) near the prediction; weak texture peaks never win over a
    // real edge because they are only considered when no real edge exists in the window.
    EdgePick best, weak;
    double bestScore = 0, weakScore = 0;
    for (size_t i = 1; i + 1 < sp.grad.size(); ++i) {
        const double d = sp.band0 + static_cast<double>(i);
        if (std::abs(d - predPx) > tolPx) continue;
        if (sp.grad[i] < sp.grad[i - 1] || sp.grad[i] < sp.grad[i + 1]) continue; // local maximum
        const double strength = sp.grad[i] / sp.gbase;
        const double z = (d - predPx) / tolPx;
        const double score = strength * std::exp(-z * z);
        // parabolic sub-pixel peak
        const double ym = sp.grad[i - 1], y0 = sp.grad[i], yp = sp.grad[i + 1];
        const double den = ym - 2 * y0 + yp;
        const double off = std::abs(den) > 1e-12 ? 0.5 * (ym - yp) / den : 0.0;
        const EdgePick e{strength >= 2.5, d + std::clamp(off, -0.5, 0.5), strength};
        if (e.found && score > bestScore) {
            bestScore = score;
            best = e;
        } else if (!e.found && score > weakScore) {
            weakScore = score;
            weak = e;
        }
    }
    return best.found ? best : weak;
}

/// Upright border depths of a film type: top, right, bottom, left (mm).
std::array<double, 4> uprightBorders(const FilmType& t) {
    return {t.imageTopMm, t.widthMm - t.imageLeftMm - t.imageWidthMm,
            t.heightMm - t.imageTopMm - t.imageHeightMm, t.imageLeftMm};
}

struct TypeChoice {
    const FilmType* type = nullptr;
    int quarterTurns = 0;
    double score = std::numeric_limits<double>::max();
    std::array<double, 4> border{}; ///< predicted true border per current side (mm)
    std::array<EdgePick, 4> edges{};
    double kx = 1, ky = 1;
};

double frameLikeFraction(const LabImage& lab, const cv::RotRect& r, const std::vector<Vec2>& localPts,
                         const std::array<float, 3>& F, float tau) {
    if (localPts.empty()) return 0;
    int k = 0;
    for (const Vec2& p : localPts) {
        float c[3];
        detail::sampleLab(lab, localToProxy(r, p.x, p.y), c);
        if (detail::deltaE(c, F.data()) < tau) ++k;
    }
    return static_cast<double>(k) / localPts.size();
}

/// Search window around a predicted border: real prints deviate from nominal borders by up to ~2 mm
/// (measured on Polaroid 600/i-Type scans); the peak choice is weighted towards the prediction.
double borderTolMm(double border) {
    return std::max(3.0, 0.5 * border);
}

TypeChoice chooseType(const LabImage& lab, Candidate& cand, double mmpp,
                      const std::array<SideProfile, 4>& prof, const std::array<std::array<float, 3>, 4>& F,
                      const std::array<float, 4>& tau) {
    const double wmm = cand.rect.w * mmpp, hmm = cand.rect.h * mmpp;
    TypeChoice best;
    std::array<float, 3> Fall{};
    for (int c = 0; c < 3; ++c)
        Fall[c] = (F[0][c] + F[1][c] + F[2][c] + F[3][c]) / 4;
    const float tauAll = std::max({tau[0], tau[1], tau[2], tau[3]});
    for (const FilmType& t : builtinFilmTypes()) {
        for (int o = 0; o < 4; ++o) {
            const double Wc = (o & 1) ? t.heightMm : t.widthMm, Hc = (o & 1) ? t.widthMm : t.heightMm;
            const double sizeErr = std::max(std::abs(wmm - Wc), std::abs(hmm - Hc));
            if (sizeErr > t.toleranceMm + 0.03 * std::max(Wc, Hc)) continue;
            TypeChoice tc;
            tc.type = &t;
            tc.quarterTurns = o;
            tc.kx = wmm / Wc;
            tc.ky = hmm / Hc;
            const auto ub = uprightBorders(t);
            double score = sizeErr * 0.5;
            for (int i = 0; i < 4; ++i) {
                const double k = (i == 0 || i == 2) ? tc.ky : tc.kx;
                const double b = ub[((i - o) % 4 + 4) % 4] * k;
                tc.border[i] = b;
                if (t.innerShape == ShapeKind::ellipse) {
                    const double rad = 0.5 * t.imageWidthMm * std::min(tc.kx, tc.ky);
                    const double half = 0.15 * ((i == 0 || i == 2) ? wmm : hmm);
                    const double pred = b + rad - std::sqrt(std::max(0.0, rad * rad - half * half));
                    if (std::isnan(prof[i].fracEdgeMm))
                        score += 3.0;
                    else
                        score += std::min(8.0, std::abs(prof[i].fracEdgeMm - pred));
                } else {
                    const EdgePick e = edgeNear(prof[i], b / mmpp, borderTolMm(b) / mmpp);
                    tc.edges[i] = e;
                    if (!e.found)
                        score += 3.0;
                    else
                        score +=
                            std::min(6.0, std::abs(e.depthPx * mmpp - b)) - std::min(1.0, 0.1 * e.strength);
                }
            }
            // Corner evidence: image-area corners are frame-coloured for round frames, image for squares.
            const double ix0 = tc.border[3], iy0 = tc.border[0], ix1 = wmm - tc.border[1],
                         iy1 = hmm - tc.border[2];
            const double inset = 3.0;
            std::vector<Vec2> corners = {{(ix0 + inset) / mmpp, (iy0 + inset) / mmpp},
                                         {(ix1 - inset) / mmpp, (iy0 + inset) / mmpp},
                                         {(ix1 - inset) / mmpp, (iy1 - inset) / mmpp},
                                         {(ix0 + inset) / mmpp, (iy1 - inset) / mmpp}};
            const double fl = frameLikeFraction(lab, cand.rect, corners, Fall, tauAll);
            if (t.innerShape == ShapeKind::ellipse)
                score += fl >= 0.75 ? -2.0 : 6.0;
            else
                score += fl >= 0.75 ? 6.0 : 0.0;
            tc.score = score;
            if (score < best.score) best = tc;
        }
    }
    return best;
}

/// Radial refinement of a round image area on the proxy.
std::optional<cv::Circle> refineCircleProxy(const LabImage& lab, const cv::RotRect& r, Vec2 centerLocal,
                                            double radiusPx, const std::array<float, 3>& F, float tau) {
    std::vector<Vec2> pts;
    for (int k = 0; k < 180; ++k) {
        const double th = k * 2 * kPi / 180;
        const Vec2 dir{std::cos(th), std::sin(th)};
        int run = 0;
        for (double rr = 0.75 * radiusPx; rr <= 1.25 * radiusPx; rr += 0.5) {
            const Vec2 p = centerLocal + dir * rr;
            float c[3];
            detail::sampleLab(lab, localToProxy(r, p.x, p.y), c);
            if (detail::deltaE(c, F.data()) < tau) {
                if (++run >= 3) {
                    pts.push_back(centerLocal + dir * (rr - 1.0));
                    break;
                }
            } else {
                run = 0;
            }
        }
    }
    if (pts.size() < 40) return std::nullopt;
    return cv::fitCircleRobust(pts);
}

void detectInner(const LabImage& lab, Candidate& cand, double mmpp) {
    std::array<std::array<float, 3>, 4> F{};
    std::array<float, 4> tau{};
    const auto prof = sideProfiles(lab, cand.rect, mmpp, F, tau);
    const TypeChoice tc = chooseType(lab, cand, mmpp, prof, F, tau);
    const double wmm = cand.rect.w * mmpp, hmm = cand.rect.h * mmpp;
    std::array<double, 4> depth{};
    if (!tc.type) {
        cand.reasons.push_back("size_implausible");
        for (int i = 0; i < 4; ++i) {
            const double side = (i == 0 || i == 2) ? hmm : wmm;
            const double fallback = 0.08 * side;
            const EdgePick e = edgeNear(prof[i], fallback / mmpp, 0.3 * side / mmpp);
            depth[i] = e.found ? e.depthPx * mmpp : fallback;
        }
        cand.innerConf = 0.3;
    } else {
        cand.type = tc.type;
        cand.quarterTurns = tc.quarterTurns;
        int strong = 0, weak = 0;
        for (int i = 0; i < 4; ++i) {
            if (tc.type->innerShape == ShapeKind::ellipse) {
                depth[i] = tc.border[i];
                continue;
            }
            const EdgePick& e = tc.edges[i];
            if (e.found) {
                depth[i] = e.depthPx * mmpp;
                if (e.strength >= 4.0)
                    ++strong;
                else
                    ++weak;
            } else {
                depth[i] = tc.border[i];
            }
        }
        if (tc.type->innerShape == ShapeKind::quad) {
            if (strong + weak < 4) cand.reasons.push_back("inner_prior_fallback");
            cand.innerConf = std::clamp(0.2 + 0.2 * strong + 0.12 * weak, 0.0, 1.0);
            // left/right symmetry check (all supported types are centred horizontally when upright)
            const int o = tc.quarterTurns;
            const int a = (o & 1) ? 0 : 1, b = (o & 1) ? 2 : 3;
            if (std::abs(depth[a] - depth[b]) > std::max(1.5, 0.35 * std::max(depth[a], depth[b]))) {
                cand.reasons.push_back("inner_asymmetric");
                cand.innerConf *= 0.8;
            }
        }
    }
    const double x0 = depth[3] / mmpp, y0 = depth[0] / mmpp, x1 = cand.rect.w - depth[1] / mmpp,
                 y1 = cand.rect.h - depth[2] / mmpp;
    if (cand.type && cand.type->innerShape == ShapeKind::ellipse) {
        const Vec2 c{(x0 + x1) / 2, (y0 + y1) / 2};
        const double rad = 0.25 * ((x1 - x0) + (y1 - y0));
        std::array<float, 3> Fall{};
        for (int k = 0; k < 3; ++k)
            Fall[k] = (F[0][k] + F[1][k] + F[2][k] + F[3][k]) / 4;
        const float t = std::max({tau[0], tau[1], tau[2], tau[3]});
        auto circ = refineCircleProxy(lab, cand.rect, c, rad, Fall, t);
        Ellipse e{c, rad, rad, 0};
        if (circ && std::abs(circ->r - rad) < 0.12 * rad && norm(circ->c - c) < 3.0 / mmpp) {
            e = Ellipse{circ->c, circ->r, circ->r, 0};
            cand.innerConf = 0.9;
        } else {
            cand.reasons.push_back("inner_circle_prior");
            cand.innerConf = 0.45;
        }
        cand.innerLocal = Shape::fromEllipse(e);
    } else {
        cand.innerLocal = Shape::fromQuad(rectQuad(x0, y0, x1, y1));
    }
}

/// Splits a component whose rectangle matches k prints side by side along one axis.
/// Card size tolerance used to accept a component as one card. Cards are cut precisely (±0.5 mm) and
/// scanner resolution is accurate; the outline may include ~1 mm of scanner shadow. When the resolution is
/// unknown (estimated), sizes are only roughly right and the tolerance is wide.
struct SizeTol {
    double mm = 2.5;
    double frac = 0.01;
    double of(const FilmType& t) const {
        return std::min(t.toleranceMm, mm) + frac * std::max(t.widthMm, t.heightMm);
    }
};

std::vector<cv::RotRect> trySplit(const cv::RotRect& r, double mmpp, const SizeTol& tol) {
    for (int k = 2; k <= 4; ++k)
        for (int axis = 0; axis < 2; ++axis) {
            const double w = axis == 0 ? r.w / k : r.w, h = axis == 1 ? r.h / k : r.h;
            for (const FilmType& t : builtinFilmTypes()) {
                const double a = std::min(w, h) * mmpp, b = std::max(w, h) * mmpp;
                const double ta = std::min(t.widthMm, t.heightMm), tb = std::max(t.widthMm, t.heightMm);
                if (std::abs(a - ta) < tol.of(t) && std::abs(b - tb) < tol.of(t)) {
                    std::vector<cv::RotRect> parts;
                    const Vec2 u{std::cos(r.angle), std::sin(r.angle)},
                        v{-std::sin(r.angle), std::cos(r.angle)};
                    for (int i = 0; i < k; ++i) {
                        cv::RotRect p = r;
                        p.w = w;
                        p.h = h;
                        const double off = (i - (k - 1) / 2.0);
                        p.c = r.c + (axis == 0 ? u * (off * w) : v * (off * h));
                        parts.push_back(p);
                    }
                    return parts;
                }
            }
        }
    return {};
}

bool matchesAnyCard(const cv::RotRect& r, double mmpp, const SizeTol& tol) {
    const double a = std::min(r.w, r.h) * mmpp, b = std::max(r.w, r.h) * mmpp;
    for (const FilmType& t : builtinFilmTypes()) {
        const double ta = std::min(t.widthMm, t.heightMm), tb = std::max(t.widthMm, t.heightMm);
        if (std::abs(a - ta) < tol.of(t) && std::abs(b - tb) < tol.of(t)) return true;
    }
    return false;
}

/// How far (mm) a rectangle is from the nearest card size beyond that card's tolerance (≤ 0 = matches).
double cardExcessMm(const cv::RotRect& r, double mmpp, const SizeTol& tolerance) {
    const double a = std::min(r.w, r.h) * mmpp, b = std::max(r.w, r.h) * mmpp;
    double best = std::numeric_limits<double>::max();
    for (const FilmType& t : builtinFilmTypes()) {
        const double ta = std::min(t.widthMm, t.heightMm), tb = std::max(t.widthMm, t.heightMm);
        best = std::min(best, std::max(std::abs(a - ta), std::abs(b - tb)) - tolerance.of(t));
    }
    return best;
}

cv::RotRect rectOfPoints(const std::vector<Vec2>& pts) {
    return cv::minAreaRect(cv::convexHull(pts));
}

Vec2 proxyToLocal(const cv::RotRect& r, Vec2 p) {
    const Vec2 u{std::cos(r.angle), std::sin(r.angle)}, v{-std::sin(r.angle), std::cos(r.angle)};
    const Vec2 d = p - r.c;
    return {dot(d, u) + r.w / 2, dot(d, v) + r.h / 2};
}

/// Splits the boundary points of a merged component (touching prints, possibly offset or differently
/// tilted) with straight cuts so that every part is card sized. The cut may be tilted up to ±6° against the
/// merged rectangle (each print has its own tilt). Points within ±0.8 mm of a cut belong to the shared seam
/// and are ignored when sizing the parts; each part's seam side is then moved onto the cut line.
/// Recurses for up to four prints; prefers cuts where both parts fit a card directly.
std::vector<cv::RotRect> splitByHull(const std::vector<Vec2>& pts, double mmpp, int depth,
                                     const SizeTol& tol) {
    if (pts.size() < 8) return {};
    const cv::RotRect r = rectOfPoints(pts);
    if (cardExcessMm(r, mmpp, tol) <= 0) return {r};
    if (depth == 0) return {};
    const size_t stride = std::max<size_t>(1, pts.size() / 1200);
    const double band = 0.8 / mmpp;
    std::vector<Vec2> local(pts.size());
    for (size_t i = 0; i < pts.size(); ++i)
        local[i] = proxyToLocal(r, pts[i]);
    const Vec2 u{std::cos(r.angle), std::sin(r.angle)}, v{-std::sin(r.angle), std::cos(r.angle)};
    struct Cut {
        double key;
        int axis;
        double x, phi; // phi in degrees
    };
    std::vector<Vec2> lo, hi;
    auto partition = [&](int axis, double x, double phiDeg, size_t step) {
        lo.clear();
        hi.clear();
        const double c = std::cos(phiDeg * kPi / 180), s = std::sin(phiDeg * kPi / 180);
        for (size_t i = 0; i < pts.size(); i += step) {
            const double a = axis == 0 ? local[i].x - x : local[i].y - x;
            const double b = axis == 0 ? local[i].y - r.h / 2 : local[i].x - r.w / 2;
            const double d = a * c + b * s;
            if (std::abs(d) < band) continue;
            (d < 0 ? lo : hi).push_back(pts[i]);
        }
    };
    // cut line in proxy coordinates: point + unit normal (pointing towards `hi`)
    auto cutLine = [&](int axis, double x, double phiDeg, Vec2& p0, Vec2& nrm) {
        const double c = std::cos(phiDeg * kPi / 180), s = std::sin(phiDeg * kPi / 180);
        const Vec2 ax = axis == 0 ? u : v, other = axis == 0 ? v : u;
        p0 = axis == 0 ? localToProxy(r, x, r.h / 2) : localToProxy(r, r.w / 2, x);
        nrm = normalized(ax * c + other * s);
    };
    // move the side of `part` that faces the cut onto the cut line
    auto snapToCut = [&](cv::RotRect part, Vec2 p0, Vec2 nrm, double dir) {
        const Vec2 pu{std::cos(part.angle), std::sin(part.angle)},
            pv{-std::sin(part.angle), std::cos(part.angle)};
        const Vec2 n = nrm * dir; // from the part towards the cut
        const bool alongU = std::abs(dot(pu, n)) >= std::abs(dot(pv, n));
        const Vec2 axisDir = alongU ? (dot(pu, n) >= 0 ? pu : pu * -1.0) : (dot(pv, n) >= 0 ? pv : pv * -1.0);
        const double half = alongU ? part.w / 2 : part.h / 2;
        const Vec2 sideMid = part.c + axisDir * half;
        const double gap = dot(p0 - sideMid, nrm) * dir; // > 0: the cut lies beyond the side
        if (gap > 0 && gap < 2 * band) {
            if (alongU)
                part.w += gap;
            else
                part.h += gap;
            part.c = part.c + axisDir * (gap / 2);
        }
        return part;
    };
    std::vector<Cut> cuts;
    for (int axis = 0; axis < 2; ++axis) {
        const double L = axis == 0 ? r.w : r.h;
        const double step = std::max(1.0, L / 160);
        for (double phi = -6; phi <= 6.01; phi += 1.0)
            for (double x = 0.15 * L; x <= 0.85 * L; x += step) {
                partition(axis, x, phi, stride);
                if (lo.size() < 8 || hi.size() < 8) continue;
                const double ea = cardExcessMm(rectOfPoints(lo), mmpp, tol),
                             eb = cardExcessMm(rectOfPoints(hi), mmpp, tol);
                if (std::min(ea, eb) > 0) continue;
                const double key = std::max(ea, eb) <= 0 ? std::max(ea, eb) - 100 : std::min(ea, eb);
                cuts.push_back({key, axis, x, phi});
            }
    }
    std::sort(cuts.begin(), cuts.end(), [](const Cut& a, const Cut& b) { return a.key < b.key; });
    double bestScore = std::numeric_limits<double>::max();
    std::vector<cv::RotRect> best;
    int tried = 0;
    for (size_t k = 0; k < cuts.size() && tried < 4; ++k) {
        bool dup = false;
        for (size_t j = 0; j < k; ++j)
            if (cuts[j].axis == cuts[k].axis && std::abs(cuts[j].x - cuts[k].x) < 2 * band) dup = true;
        if (dup) continue;
        ++tried;
        Cut c = cuts[k];
        const double L = c.axis == 0 ? r.w : r.h;
        const double step = std::max(1.0, L / 160);
        double bestLocal = std::numeric_limits<double>::max();
        for (double dphi = -0.75; dphi <= 0.76; dphi += 0.25)
            for (double dx = -step; dx <= step + 1e-9; dx += 0.5) {
                partition(c.axis, cuts[k].x + dx, cuts[k].phi + dphi, 1);
                if (lo.size() < 8 || hi.size() < 8) continue;
                const double e = std::max(cardExcessMm(rectOfPoints(lo), mmpp, tol),
                                          cardExcessMm(rectOfPoints(hi), mmpp, tol));
                if (e < bestLocal) {
                    bestLocal = e;
                    c.x = cuts[k].x + dx;
                    c.phi = cuts[k].phi + dphi;
                }
            }
        partition(c.axis, c.x, c.phi, 1);
        const std::vector<Vec2> loPts = lo, hiPts = hi;
        auto a = splitByHull(loPts, mmpp, depth - 1, tol);
        auto b = splitByHull(hiPts, mmpp, depth - 1, tol);
        if (a.empty() || b.empty() || a.size() + b.size() > 4) continue;
        Vec2 p0, nrm;
        cutLine(c.axis, c.x, c.phi, p0, nrm);
        if (a.size() == 1) a[0] = snapToCut(a[0], p0, nrm, 1.0);
        if (b.size() == 1) b[0] = snapToCut(b[0], p0, nrm, -1.0);
        double score = -std::numeric_limits<double>::max();
        for (auto* vv : {&a, &b})
            for (auto& p : *vv)
                score = std::max(score, cardExcessMm(p, mmpp, tol));
        score += 0.5 * static_cast<double>(a.size() + b.size()); // prefer fewer parts
        if (score < bestScore) {
            bestScore = score;
            best = a;
            best.insert(best.end(), b.begin(), b.end());
        }
    }
    return best;
}

/// Line-averaged outward search for frame edges the threshold mask missed (black frame on a dark
/// background, frame colour close to the background). For each side returns the outward growth in proxy
/// pixels, or NaN when no frame→background transition is found within `maxGrowPx`.
std::array<double, 4> growToFrame(const LabImage& lab, const Background& bg, const cv::RotRect& r,
                                  double maxGrowPx, double mmpp) {
    std::array<double, 4> out;
    out.fill(std::numeric_limits<double>::quiet_NaN());
    const Vec2 u{std::cos(r.angle), std::sin(r.angle)}, v{-std::sin(r.angle), std::cos(r.angle)};
    const double mm = 1.0 / mmpp;
    for (int side = 0; side < 4; ++side) {
        const bool horiz = side == 0 || side == 2;
        const double len = horiz ? r.w : r.h;
        const Vec2 n = side == 0 ? v * -1.0 : side == 1 ? u : side == 2 ? v : u * -1.0;
        const Vec2 along = horiz ? u : v;
        const Vec2 base = r.c + n * (0.5 * (horiz ? r.h : r.w));
        const int nAlong = std::max(8, static_cast<int>(0.6 * len));
        std::vector<double> D;
        for (double d = 0; d <= maxGrowPx; d += 1.0) {
            double s[3] = {0, 0, 0}, b[3] = {0, 0, 0};
            int cnt = 0;
            for (int k = 0; k < nAlong; ++k) {
                const Vec2 p = base + n * (d + 0.5) + along * ((k + 0.5) / nAlong - 0.5) * (0.6 * len);
                const int64_t px = static_cast<int64_t>(p.x), py = static_cast<int64_t>(p.y);
                if (px < 0 || py < 0 || px >= lab.w || py >= lab.h) continue;
                const float* c = lab.at(px, py);
                float m[3];
                bg.at(px, py, m);
                for (int q = 0; q < 3; ++q) {
                    s[q] += c[q];
                    b[q] += m[q];
                }
                ++cnt;
            }
            if (cnt < nAlong / 2) break; // left the scan
            const float A[3] = {static_cast<float>(s[0] / cnt), static_cast<float>(s[1] / cnt),
                                static_cast<float>(s[2] / cnt)};
            const float B[3] = {static_cast<float>(b[0] / cnt), static_cast<float>(b[1] / cnt),
                                static_cast<float>(b[2] / cnt)};
            D.push_back(detail::deltaE(A, B));
        }
        if (D.size() < static_cast<size_t>(3 * mm)) continue;
        // 3-tap smoothing, then compare the level right outside the seed with the background level of
        // this profile (lowest third: the residual of the background model, not zero).
        std::vector<double> S(D.size());
        for (size_t i = 0; i < D.size(); ++i)
            S[i] = (D[i > 0 ? i - 1 : i] + D[i] + D[i + 1 < D.size() ? i + 1 : i]) / 3;
        std::vector<double> sorted = S;
        std::sort(sorted.begin(), sorted.end());
        sorted.resize(std::max<size_t>(3, sorted.size() / 3));
        const double dFar = cv::median(sorted);
        const double sFar = std::max(0.03, cv::madSigma(sorted, dFar));
        const int n0 = static_cast<int>(0.3 * mm), n1 = std::max(n0 + 1, static_cast<int>(1.3 * mm));
        std::vector<double> near(S.begin() + n0, S.begin() + std::min<size_t>(S.size(), n1));
        const double dNear = cv::median(near);
        if (dNear - dFar < std::max(0.25, 5.0 * sFar)) {
            out[side] = 0; // nothing distinguishable right outside the seed
            continue;
        }
        const double theta = dFar + 0.5 * std::min(dNear - dFar, 6.0);
        D = S;
        const int run = std::max(3, static_cast<int>(1.0 * mm));
        for (size_t i = 1; i + run < D.size(); ++i) {
            if (D[i] >= theta || D[i - 1] < theta) continue;
            bool stays = true;
            for (int k = 1; k <= run; ++k)
                if (D[i + k] >= theta) stays = false;
            if (!stays) continue;
            const double t = (D[i - 1] - theta) / std::max(1e-9, D[i - 1] - D[i]);
            out[side] = static_cast<double>(i - 1) + t + 0.5;
            break;
        }
    }
    return out;
}

/// True when `seed` has the size of some film type's image area and the growth on every side matches that
/// type's border for one orientation (the faint frame found around a visible picture is plausible).
bool imageAreaConsistent(const cv::RotRect& seed, const std::array<double, 4>& g, double mmpp) {
    const double wmm = seed.w * mmpp, hmm = seed.h * mmpp;
    for (const FilmType& t : builtinFilmTypes()) {
        const auto ub = uprightBorders(t);
        for (int o = 0; o < 4; ++o) {
            const double iw = (o & 1) ? t.imageHeightMm : t.imageWidthMm,
                         ih = (o & 1) ? t.imageWidthMm : t.imageHeightMm;
            if (std::abs(wmm - iw) > 2.5 || std::abs(hmm - ih) > 2.5) continue;
            bool ok = true;
            for (int i = 0; i < 4 && ok; ++i) {
                const double b = ub[((i - o) % 4 + 4) % 4];
                ok = std::abs(g[i] * mmpp - b) <= std::max(1.5, 0.3 * b);
            }
            if (ok) return true;
        }
    }
    return false;
}

cv::RotRect growRect(const cv::RotRect& r, const std::array<double, 4>& g) {
    const Vec2 u{std::cos(r.angle), std::sin(r.angle)}, v{-std::sin(r.angle), std::cos(r.angle)};
    cv::RotRect o = r;
    o.w = r.w + g[1] + g[3];
    o.h = r.h + g[0] + g[2];
    o.c = r.c + u * ((g[1] - g[3]) / 2) + v * ((g[2] - g[0]) / 2);
    return o;
}

double maxBorderMm() {
    double m = 0;
    for (const FilmType& t : builtinFilmTypes()) {
        const auto ub = uprightBorders(t);
        m = std::max(m, *std::max_element(ub.begin(), ub.end()));
    }
    return m;
}

/// True when the scan itself has the size of one card (± a few mm): the user cropped one print in the
/// scanner software.
bool scanIsOneCard(double wmm, double hmm) {
    for (const FilmType& t : builtinFilmTypes())
        for (int o = 0; o < 2; ++o) {
            const double W = o ? t.heightMm : t.widthMm, H = o ? t.widthMm : t.heightMm;
            if (wmm >= W - 8 && wmm <= W + 12 && hmm >= H - 8 && hmm <= H + 12) return true;
        }
    return false;
}

/// Thin background margins along the scan border of a single-print scan (proxy px per side: top, right,
/// bottom, left). A margin is a band at the border whose colour differs from the frame band 3–4 mm inside
/// and that ends within 3 mm.
std::array<double, 4> scanMargins(const LabImage& lab, double mmpp) {
    std::array<double, 4> out{0, 0, 0, 0};
    const double mm = 1.0 / mmpp;
    const int maxD = static_cast<int>(4.0 * mm);
    for (int side = 0; side < 4; ++side) {
        const bool horiz = side == 0 || side == 2;
        const int64_t len = horiz ? lab.w : lab.h;
        std::vector<std::array<double, 3>> P;
        for (int d = 0; d < maxD; ++d) {
            double acc[3] = {0, 0, 0};
            int n = 0;
            for (int64_t k = len / 5; k < len * 4 / 5; k += 2) {
                int64_t x = 0, y = 0;
                switch (side) {
                case 0:
                    x = k;
                    y = d;
                    break;
                case 1:
                    x = lab.w - 1 - d;
                    y = k;
                    break;
                case 2:
                    x = k;
                    y = lab.h - 1 - d;
                    break;
                default:
                    x = d;
                    y = k;
                    break;
                }
                if (x < 0 || y < 0 || x >= lab.w || y >= lab.h) continue;
                const float* c = lab.at(x, y);
                for (int q = 0; q < 3; ++q)
                    acc[q] += c[q];
                ++n;
            }
            if (n == 0) break;
            P.push_back({acc[0] / n, acc[1] / n, acc[2] / n});
        }
        if (P.size() < static_cast<size_t>(3.5 * mm)) continue;
        std::array<double, 3> F{0, 0, 0};
        int nf = 0;
        for (size_t d = static_cast<size_t>(3.0 * mm); d < P.size(); ++d, ++nf)
            for (int q = 0; q < 3; ++q)
                F[q] += P[d][q];
        for (int q = 0; q < 3; ++q)
            F[q] /= std::max(1, nf);
        auto dE = [&](size_t d) {
            const double a = P[d][0] - F[0], b = P[d][1] - F[1], c = P[d][2] - F[2];
            return std::sqrt(a * a + b * b + c * c);
        };
        const double edge = dE(0);
        if (edge < 6.0) continue; // the border is frame coloured: no margin
        for (size_t d = 1; d < static_cast<size_t>(3.0 * mm); ++d)
            if (dE(d) < 0.5 * edge && (d + 1 >= P.size() || dE(d + 1) < 0.5 * edge)) {
                out[side] = static_cast<double>(d);
                break;
            }
    }
    return out;
}

DetectedItem toItem(const Candidate& c, int scale, double mmpp) {
    DetectedItem it;
    const double s = static_cast<double>(scale);
    const Quad pq = c.rect.quad();
    std::array<Vec2, 4> sq;
    for (int i = 0; i < 4; ++i)
        sq[i] = pq.p[i] * s;
    it.outer = Shape::fromQuad(normalizeQuad(sq));
    // inner: local → proxy → scan
    const Mat3 localToProxyM = Mat3::translate(c.rect.c.x, c.rect.c.y) * Mat3::rotate(c.rect.angle) *
                               Mat3::translate(-c.rect.w / 2, -c.rect.h / 2);
    it.inner = transformShape(c.innerLocal, Mat3::scale(s, s) * localToProxyM);
    if (c.type) {
        it.filmTypeId = c.type->id;
        it.cornerRadiusPx = c.type->cornerRadiusMm / mmpp * s;
    }
    it.upright = (4 - c.quarterTurns) % 4;
    it.innerConfidence = c.innerConf;
    it.reasons = c.reasons;
    return it;
}

void orderReading(std::vector<DetectedItem>& items) {
    if (items.empty()) return;
    std::vector<double> hs;
    for (auto& it : items)
        hs.push_back(boundsOf(it.outer).height());
    const double tol = 0.5 * cv::median(hs);
    std::sort(items.begin(), items.end(), [](const DetectedItem& a, const DetectedItem& b) {
        return centroid(a.outer).y < centroid(b.outer).y;
    });
    std::vector<std::vector<DetectedItem>> rows;
    double rowY = 0;
    for (auto& it : items) {
        const double y = centroid(it.outer).y;
        if (rows.empty() || std::abs(y - rowY) > tol) {
            rows.emplace_back();
            rowY = y;
        }
        rows.back().push_back(it);
        double sum = 0;
        for (auto& r : rows.back())
            sum += centroid(r.outer).y;
        rowY = sum / rows.back().size();
    }
    items.clear();
    for (auto& r : rows) {
        std::sort(r.begin(), r.end(), [](const DetectedItem& a, const DetectedItem& b) {
            return centroid(a.outer).x < centroid(b.outer).x;
        });
        for (auto& it : r)
            items.push_back(it);
    }
}

double elapsed(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

} // namespace

Result<DetectionResult> detectPrints(ImageSource& src, const Proxy& proxy, const DetectParams& params,
                                     const CancelToken& cancel, const ProgressFn& progress) {
    const auto t0 = std::chrono::steady_clock::now();
    DetectionResult res;
    res.proxyScale = proxy.scale;
    const ImageInfo& info = src.info();
    double dpi = params.dpiOverride > 0 ? params.dpiOverride : info.dpiX;
    if (dpi <= 0) {
        dpi = std::max(info.width, info.height) / (297.0 / 25.4);
        res.warnings.push_back("dpi_unknown");
    }
    res.dpiUsed = dpi;
    SizeTol tol;
    if (params.dpiOverride <= 0 && info.dpiX <= 0) tol = SizeTol{4.0, 0.03}; // resolution estimated
    double mmpp = proxy.scale * 25.4 / dpi;                                  // mm per proxy pixel
    if (proxy.width < 16 || proxy.height < 16) return makeError(Errc::invalid_argument, "Scan is too small");

    std::vector<Candidate> cands;
    std::vector<cv::Component> leftovers; // see kPossibleMissFraction
    cv::Mask finalMask(0, 0);
    if (params.tmpl) {
        for (const auto& slot : params.tmpl->slots) {
            std::array<Vec2, 4> q;
            for (int i = 0; i < 4; ++i)
                q[i] = Vec2{slot.outer[i].x * proxy.width, slot.outer[i].y * proxy.height};
            const auto hull = cv::convexHull({q.begin(), q.end()});
            Candidate c;
            c.rect = cv::minAreaRect(hull);
            c.rectangularity = 1.0;
            c.reasons.push_back("template");
            cands.push_back(c);
        }
    } else {
        const LabImage lab = detail::toLab(proxy);
        if (cancel.cancelled()) return cancelledError();
        const Background bg = modelBackground(lab);
        cv::ImageF dist(lab.w, lab.h);
        std::vector<float> sample;
        sample.reserve(static_cast<size_t>(lab.w * lab.h / 4 + 1));
        for (int64_t y = 0; y < lab.h; ++y)
            for (int64_t x = 0; x < lab.w; ++x) {
                float b[3];
                bg.at(x, y, b);
                const float d = detail::deltaE(lab.at(x, y), b);
                dist.at(x, y) = d;
                if (((x ^ y) & 1) == 0) sample.push_back(std::min(d, 100.0f));
            }
        const double otsu = cv::otsuThreshold(sample, 100.0);
        const double thr = std::max(6.0 * bg.noise + 3.0, std::min(otsu, 20.0));
        // Hysteresis: weak pixels (low-contrast frames, e.g. a black frame on a dark background) are kept
        // when their cleaned component touches a strong component.
        const double thrWeak = std::min(thr, std::max(3.5 * bg.noise + 1.5, 2.5));
        const int rOpen = std::max(1, static_cast<int>(std::lround(0.3 / mmpp)));
        const int rClose = std::max(1, static_cast<int>(std::lround(0.6 / mmpp)));
        cv::Mask strong(lab.w, lab.h), mask(lab.w, lab.h);
        for (size_t i = 0; i < mask.v.size(); ++i) {
            strong.v[i] = dist.v[i] > thr ? 1 : 0;
            mask.v[i] = dist.v[i] > thrWeak ? 1 : 0;
        }
        cv::openMask(strong, rOpen);
        // weak pixels: close first (a faint frame is speckled with noise holes), then a wide opening (frames
        // are ≥ 4 mm wide; dust and scratches are thin)
        cv::closeMask(mask, rClose);
        cv::openMask(mask, std::max(rOpen, static_cast<int>(std::lround(1.0 / mmpp))));
        {
            std::vector<int32_t> wl;
            const auto wc = cv::connectedComponents(mask, wl);
            std::vector<uint8_t> keep(wc.size() + 1, 0);
            for (size_t i = 0; i < mask.v.size(); ++i)
                if (strong.v[i] && wl[i] > 0) keep[wl[i]] = 1;
            for (size_t i = 0; i < mask.v.size(); ++i)
                mask.v[i] = wl[i] > 0 && keep[wl[i]] ? 1 : 0;
        }
        cv::closeMask(mask, rClose);
        cv::fillHoles(mask);
        finalMask = mask;
        std::vector<int32_t> labels;
        auto comps = cv::connectedComponents(mask, labels);
        double minCard = std::numeric_limits<double>::max();
        for (const auto& t : builtinFilmTypes())
            minCard = std::min(minCard, t.imageWidthMm * t.imageHeightMm);
        const double minArea = 0.6 * minCard / (mmpp * mmpp);
        // Effective resolution: scanner software may scale the output (e.g. 103 %) while the file keeps the
        // nominal DPI. Components with a card's aspect ratio (±2 %) and size (±6 %) measure the scale.
        if (tol.mm < 4.0) {
            std::vector<double> ratios;
            for (const auto& comp : comps) {
                if (comp.area < minArea) continue;
                const cv::RotRect r = cv::minAreaRect(cv::convexHull(comp.boundaryCorners));
                const double a = std::min(r.w, r.h) * mmpp, b = std::max(r.w, r.h) * mmpp;
                double best = 0, bestDev = 1;
                for (const FilmType& t : builtinFilmTypes()) {
                    const double ta = std::min(t.widthMm, t.heightMm), tb = std::max(t.widthMm, t.heightMm);
                    const double aspectErr = std::abs((b / a) / (tb / ta) - 1.0);
                    const double ratio = std::sqrt(a * b / (ta * tb));
                    if (aspectErr < 0.02 && std::abs(ratio - 1.0) < 0.06 && std::abs(ratio - 1.0) < bestDev) {
                        best = ratio;
                        bestDev = std::abs(ratio - 1.0);
                    }
                }
                if (best > 0) ratios.push_back(best);
            }
            if (!ratios.empty()) {
                const double k = cv::median(ratios);
                if (std::abs(k - 1.0) >= 0.015) {
                    char buf[96];
                    std::snprintf(buf, sizeof buf, "dpi_rescaled: tagged %.0f, measured %.0f", dpi, dpi * k);
                    res.warnings.push_back(buf);
                    dpi *= k;
                    res.dpiUsed = dpi;
                    mmpp = proxy.scale * 25.4 / dpi;
                }
            }
        }
        double maxCardLongMm = 0, minCardShortMm = std::numeric_limits<double>::max();
        for (const auto& t : builtinFilmTypes()) {
            maxCardLongMm = std::max(maxCardLongMm, std::max(t.widthMm, t.heightMm));
            minCardShortMm = std::min(minCardShortMm, std::min(t.widthMm, t.heightMm));
        }
        // UX-01 (automatic count): objects too large for dust but too small for a print may be prints whose
        // frame matches the background; they are counted once the candidates are final.
        const double missArea = kPossibleMissFraction * minCard / (mmpp * mmpp);
        for (auto& comp : comps) {
            if (comp.area < minArea) {
                if (comp.area >= missArea) leftovers.push_back(comp);
                continue;
            }
            const auto hull = cv::convexHull(comp.boundaryCorners);
            cv::RotRect r = cv::minAreaRect(hull);
            Candidate c;
            c.rect = r;
            c.rectangularity = comp.area / std::max(1.0, r.area());
            c.touchesEdge = comp.x0 == 0 || comp.y0 == 0 || comp.x1 == lab.w - 1 || comp.y1 == lab.h - 1;
            if (!matchesAnyCard(r, mmpp, tol)) {
                // 1. merged touching prints: split by geometry, then fall back to equal division
                auto parts = splitByHull(comp.boundaryCorners, mmpp, 3, tol);
                if (parts.size() < 2) parts = trySplit(r, mmpp, tol);
                if (parts.size() >= 2) {
                    for (auto& p : parts) {
                        Candidate pc = c;
                        pc.rect = p;
                        pc.split = true;
                        pc.reasons.push_back("touching_split");
                        cands.push_back(pc);
                    }
                    continue;
                }
                // 2. smaller than a card: look for a faint frame around it (line-averaged profiles)
                const bool small = std::max(r.w, r.h) * mmpp < maxCardLongMm - 2.0 ||
                                   std::min(r.w, r.h) * mmpp < minCardShortMm - 2.0;
                if (small) {
                    const double maxGrow = (maxBorderMm() + 4.0) / mmpp;
                    const auto g = growToFrame(lab, bg, r, maxGrow, mmpp);
                    bool all = true;
                    for (double x : g)
                        all = all && !std::isnan(x);
                    if (all && imageAreaConsistent(r, g, mmpp) && matchesAnyCard(growRect(r, g), mmpp, tol)) {
                        c.rect = growRect(r, g);
                        c.reasons.push_back("low_frame_contrast");
                    } else {
                        // 3. frame indistinguishable from the background: expand by the largest border on
                        // every side so that the real card is certainly contained (rule 3); needs review.
                        std::array<double, 4> grow;
                        for (int i = 0; i < 4; ++i)
                            grow[i] = std::max(std::isnan(g[i]) ? 0.0 : g[i], maxBorderMm() / mmpp);
                        c.rect = growRect(r, grow);
                        c.imageOnly = true;
                        c.reasons.push_back("frame_not_visible");
                    }
                }
            }
            if (c.touchesEdge) c.reasons.push_back("touches_scan_edge");
            cands.push_back(c);
        }
        // A scan cropped to one print: the "background" sampled at the border is the frame itself.
        const bool anyCard = std::any_of(cands.begin(), cands.end(), [&](const Candidate& c) {
            return !c.imageOnly && matchesAnyCard(c.rect, mmpp, tol);
        });
        if (!anyCard && scanIsOneCard(static_cast<double>(lab.w) * mmpp, static_cast<double>(lab.h) * mmpp)) {
            const auto m = scanMargins(lab, mmpp);
            Candidate c;
            c.rect.angle = 0;
            c.rect.w = static_cast<double>(lab.w) - m[1] - m[3];
            c.rect.h = static_cast<double>(lab.h) - m[0] - m[2];
            c.rect.c = {m[3] + c.rect.w / 2, m[0] + c.rect.h / 2};
            c.rectangularity = 1.0;
            c.fillsScan = true;
            c.reasons.push_back("print_fills_scan");
            cands.assign(1, c);
        }
        // Expected-count sanity: very small leftovers beyond maxCount are dropped (largest first).
        std::sort(cands.begin(), cands.end(),
                  [](const Candidate& a, const Candidate& b) { return a.rect.area() > b.rect.area(); });
        if (static_cast<int>(cands.size()) > params.maxCount) {
            res.warnings.push_back("too_many_candidates");
            cands.resize(params.maxCount);
        }
        // UX-01: objects that may be prints the detection left out (shown also when counting automatically)
        int missed = countFaintObjects(lab, mmpp, minArea, cands);
        for (const auto& comp : leftovers) {
            const Vec2 p{(comp.x0 + comp.x1) / 2.0, (comp.y0 + comp.y1) / 2.0};
            if (std::none_of(cands.begin(), cands.end(),
                             [&](const Candidate& c) { return insideRect(c.rect, p); }))
                ++missed;
        }
        if (missed > 0) res.warnings.push_back("possible_missed: " + std::to_string(missed));
        // Inner detection, film type and orientation per candidate.
        for (auto& c : cands) {
            // expand by one proxy pixel: partially covered edge pixels may have been below the threshold
            if (!c.fillsScan) {
                c.rect.w += 2.0;
                c.rect.h += 2.0;
            }
            detectInner(lab, c, mmpp);
            if (cancel.cancelled()) return cancelledError();
        }
        report(progress, 0.3, "detect");
    }
    if (params.tmpl) {
        const LabImage lab = detail::toLab(proxy);
        for (auto& c : cands)
            detectInner(lab, c, mmpp);
    }

    for (const auto& c : cands)
        res.items.push_back(toItem(c, proxy.scale, mmpp));
    res.secondsDetect = elapsed(t0);

    // Sides abutting other material (touching neighbours): more than 30 % of the probes 1.5 mm outside the
    // side are foreground in the detection mask.
    std::vector<std::array<bool, 4>> locked(res.items.size(),
                                            std::array<bool, 4>{false, false, false, false});
    std::vector<bool> neighbour(res.items.size(), false);
    const double W = static_cast<double>(info.width), H = static_cast<double>(info.height);
    for (size_t i = 0; i < res.items.size(); ++i) {
        if (!cands[i].fillsScan) continue;
        // sides on the scan border cannot be measured (the frame may continue beyond the scan)
        for (int s = 0; s < 4; ++s) {
            const Vec2 mid = (res.items[i].outer.quad.p[s] + res.items[i].outer.quad.p[(s + 1) % 4]) * 0.5;
            const double tolPx = proxy.scale * 1.5;
            locked[i][s] = mid.x < tolPx || mid.y < tolPx || mid.x > W - tolPx || mid.y > H - tolPx;
        }
    }
    if (finalMask.w > 0) {
        const double off = 1.5 / mmpp;
        for (size_t i = 0; i < res.items.size(); ++i)
            for (int s = 0; s < 4; ++s) {
                if (cands[i].fillsScan) continue;
                const Vec2 a = res.items[i].outer.quad.p[s] / static_cast<double>(proxy.scale),
                           b = res.items[i].outer.quad.p[(s + 1) % 4] / static_cast<double>(proxy.scale);
                const Vec2 out = perp(normalized(b - a)) * -1.0;
                int hit = 0, n = 0;
                for (int k = 0; k < 40; ++k) {
                    const Vec2 p = a + (b - a) * (0.15 + 0.7 * (k + 0.5) / 40) + out * off;
                    const int64_t x = static_cast<int64_t>(p.x), y = static_cast<int64_t>(p.y);
                    if (x < 0 || y < 0 || x >= finalMask.w || y >= finalMask.h) continue;
                    ++n;
                    if (finalMask.at(x, y)) ++hit;
                }
                locked[i][s] = n > 0 && hit > 0.3 * n;
                if (locked[i][s]) {
                    neighbour[i] = true;
                    auto& r = res.items[i].reasons;
                    if (std::find(r.begin(), r.end(), "touching_neighbour") == r.end())
                        r.push_back("touching_neighbour");
                }
            }
    }

    // Stage B
    std::vector<detail::RefineStats> stats(res.items.size());
    if (params.refine && !res.items.empty()) {
        const auto t1 = std::chrono::steady_clock::now();
        ProgressFn sub = [&](double f, std::string_view) { report(progress, 0.3 + 0.7 * f, "refine"); };
        FK_TRY(detail::refineItems(src, res.items, dpi, proxy.scale, params.safetyMarginPx, stats, cancel,
                                   sub, &locked));
        res.secondsRefine = elapsed(t1);
    } else {
        for (auto& it : res.items)
            it.outer = expandShape(it.outer, proxy.scale + params.safetyMarginPx);
    }

    // A single-print scan: the outer cannot extend beyond the scan. A side whose line leaves the scan (the
    // paper continues past the border there) becomes that scan border: clamping its corners instead would
    // turn the side and cut the paper between them (U012, owner's img001).
    for (size_t i = 0; i < res.items.size(); ++i) {
        if (!cands[i].fillsScan) continue;
        auto& q = res.items[i].outer.quad;
        const Line border[4] = {{{0, 0}, {1, 0}}, {{W, 0}, {0, 1}}, {{0, H}, {1, 0}}, {{0, 0}, {0, 1}}};
        std::array<Line, 4> sides;
        for (int s = 0; s < 4; ++s) {
            const Vec2 a = q.p[s], b = q.p[(s + 1) % 4];
            const Vec2 d = b - a;
            // where the side's line meets the scan's extent across it: past its own border → that border
            bool past = false;
            if (s == 1 || s == 3) {
                for (double y : {0.0, H}) {
                    const double x = std::abs(d.y) < 1e-9 ? a.x : a.x + d.x * (y - a.y) / d.y;
                    past = past || (s == 3 ? x < -0.5 : x > W + 0.5);
                }
            } else {
                for (double x : {0.0, W}) {
                    const double y = std::abs(d.x) < 1e-9 ? a.y : a.y + d.y * (x - a.x) / d.x;
                    past = past || (s == 0 ? y < -0.5 : y > H + 0.5);
                }
            }
            sides[s] = past ? border[s] : Line{a, normalized(d)};
        }
        for (int s = 0; s < 4; ++s)
            if (auto c = intersect(sides[(s + 3) % 4], sides[s])) q.p[s] = *c;
        for (auto& p : q.p)
            p = {std::clamp(p.x, 0.0, W), std::clamp(p.y, 0.0, H)};
        if (res.items[i].outer.quad.p[0].x > 0.5 || res.items[i].outer.quad.p[0].y > 0.5 ||
            std::abs(res.items[i].outer.quad.p[2].x - W) > 0.5 ||
            std::abs(res.items[i].outer.quad.p[2].y - H) > 0.5)
            res.items[i].reasons.push_back("scan_margin_trimmed");
    }

    if (params.tmpl) {
        const double far = 1.5 * dpi / 25.4;
        for (auto& it : res.items)
            for (double o : it.refineOffsetPx)
                if (std::abs(o) > far) {
                    it.reasons.push_back("template_snap_far");
                    break;
                }
    }

    // Confidence
    for (size_t i = 0; i < res.items.size(); ++i) {
        DetectedItem& it = res.items[i];
        const Candidate& c = cands[i];
        double oc = 1.0;
        auto has = [&](const char* r) {
            return std::find(it.reasons.begin(), it.reasons.end(), r) != it.reasons.end();
        };
        if (params.refine && stats[i].measured) {
            const auto& st = stats[i];
            it.contrast = st.contrast;
            if (st.contrast < 5.0) {
                oc *= 0.3;
                if (!has("low_contrast")) it.reasons.push_back("low_contrast");
            } else if (st.contrast < 10.0) {
                oc *= 0.75;
            }
            if (st.validFraction < 0.6) {
                oc *= 0.6;
                if (!has("refine_residual")) it.reasons.push_back("refine_residual");
            }
            if (st.residualSigma > 3.0) oc *= 0.8;
            if (st.sidesKept > 0) oc *= 0.6;
        } else {
            oc *= 0.8;
        }
        if (neighbour[i]) oc = std::min(oc, 0.55); // edge against a neighbour: always review
        if (c.rectangularity < 0.9 && !c.imageOnly) oc *= 0.8;
        if (c.split) oc *= 0.55;
        if (c.imageOnly) oc = std::min(oc, 0.3);
        if (c.fillsScan) oc = std::min(oc, 0.8);                // outline = scan border: check recommended
        if (has("low_frame_contrast")) oc = std::min(oc, 0.55); // found by averaging only: always review
        if (c.touchesEdge) oc = std::min(oc, 0.8); // may continue beyond the scan: check recommended
        if (!c.type) oc *= 0.6;
        if (has("template_snap_far")) oc *= 0.6;
        it.outerConfidence = std::clamp(oc, 0.0, 1.0);
        it.confidence = std::min(it.outerConfidence, it.innerConfidence);
    }
    orderReading(res.items);
    if (params.expectedCount > 0 && static_cast<int>(res.items.size()) != params.expectedCount) {
        res.warnings.push_back("count_mismatch: found " + std::to_string(res.items.size()) + ", expected " +
                               std::to_string(params.expectedCount));
        for (auto& it : res.items)
            it.reasons.push_back("count_mismatch");
    }
    if (res.items.empty()) res.warnings.push_back("no_prints_found");
    report(progress, 1.0, "detect");
    return res;
}

Template makeTemplate(const std::vector<DetectedItem>& items, SizeI scanSize, double dpi,
                      const std::string& name) {
    Template t;
    t.name = name;
    t.createdDpi = dpi;
    t.scanSize = scanSize;
    const double W = static_cast<double>(scanSize.w), H = static_cast<double>(scanSize.h);
    const Mat3 toRel = Mat3::scale(1.0 / W, 1.0 / H);
    int idx = 1;
    for (const auto& it : items) {
        TemplateSlot s;
        s.index = idx++;
        s.filmType = it.filmTypeId;
        for (int i = 0; i < 4; ++i)
            s.outer[i] = toRel.apply(it.outer.quad.p[i]);
        s.inner = it.inner;
        if (s.inner.kind == ShapeKind::quad) {
            for (auto& p : s.inner.quad.p)
                p = toRel.apply(p);
        } else {
            s.inner.ellipse.c = toRel.apply(s.inner.ellipse.c);
            s.inner.ellipse.rx /= W;
            s.inner.ellipse.ry /= W;
        }
        t.slots.push_back(s);
    }
    return t;
}

} // namespace fk
