#include "cv.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <numeric>

namespace fk::cv {

double median(std::vector<double> v) {
    if (v.empty()) return 0;
    const size_t n = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + n, v.end());
    double m = v[n];
    if (v.size() % 2 == 0) {
        const double lo = *std::max_element(v.begin(), v.begin() + n);
        m = 0.5 * (m + lo);
    }
    return m;
}

double madSigma(const std::vector<double>& v, double med) {
    std::vector<double> d(v.size());
    for (size_t i = 0; i < v.size(); ++i)
        d[i] = std::abs(v[i] - med);
    return 1.4826 * median(std::move(d));
}

double quantile(std::vector<double> v, double q) {
    if (v.empty()) return 0;
    const size_t k = std::min(v.size() - 1, static_cast<size_t>(std::clamp(q, 0.0, 1.0) * (v.size() - 1)));
    std::nth_element(v.begin(), v.begin() + k, v.end());
    return v[k];
}

double otsuThreshold(const std::vector<float>& values, double maxVal, int bins) {
    std::vector<double> hist(bins, 0.0);
    for (float f : values) {
        int b = static_cast<int>(f / maxVal * (bins - 1));
        hist[std::clamp(b, 0, bins - 1)] += 1;
    }
    const double total = static_cast<double>(values.size());
    double sum = 0;
    for (int i = 0; i < bins; ++i)
        sum += i * hist[i];
    double sumB = 0, wB = 0, best = -1;
    int bestT = 0;
    for (int t = 0; t < bins; ++t) {
        wB += hist[t];
        if (wB == 0) continue;
        const double wF = total - wB;
        if (wF == 0) break;
        sumB += t * hist[t];
        const double mB = sumB / wB, mF = (sum - sumB) / wF;
        const double between = wB * wF * (mB - mF) * (mB - mF);
        if (between > best) {
            best = between;
            bestT = t;
        }
    }
    return (bestT + 0.5) / (bins - 1) * maxVal;
}

namespace {
// 1-D running OR (dilate) / AND (erode) over a line with window ±r, via counts.
void morphLine(const uint8_t* in, uint8_t* out, int64_t n, int64_t stride, int r, bool dilateOp) {
    // count of ones in window
    int64_t count = 0;
    for (int64_t i = 0; i <= std::min<int64_t>(r, n - 1); ++i)
        count += in[i * stride] ? 1 : 0;
    for (int64_t i = 0; i < n; ++i) {
        const int64_t lo = i - r, hi = i + r;
        const int64_t windowLen = std::min(hi, n - 1) - std::max<int64_t>(lo, 0) + 1;
        if (dilateOp)
            out[i * stride] = count > 0 ? 1 : 0;
        else
            out[i * stride] =
                count == windowLen ? 1 : 0; // outside the image counts as "1" for erosion of edges
        // slide
        if (hi + 1 < n) count += in[(hi + 1) * stride] ? 1 : 0;
        if (lo >= 0) count -= in[lo * stride] ? 1 : 0;
    }
}

void morph(Mask& m, int r, bool dilateOp) {
    if (r <= 0) return;
    std::vector<uint8_t> tmp(m.v.size());
    for (int64_t y = 0; y < m.h; ++y)
        morphLine(m.v.data() + y * m.w, tmp.data() + y * m.w, m.w, 1, r, dilateOp);
    for (int64_t x = 0; x < m.w; ++x)
        morphLine(tmp.data() + x, m.v.data() + x, m.h, m.w, r, dilateOp);
}
} // namespace

void dilate(Mask& m, int r) {
    morph(m, r, true);
}
void erode(Mask& m, int r) {
    morph(m, r, false);
}

void fillHoles(Mask& m) {
    // 0 = unvisited background, 1 = foreground, 2 = background reached from the border
    std::vector<int64_t> stack;
    auto push = [&](int64_t x, int64_t y) {
        uint8_t& v = m.at(x, y);
        if (v == 0) {
            v = 2;
            stack.push_back(y * m.w + x);
        }
    };
    for (int64_t x = 0; x < m.w; ++x) {
        push(x, 0);
        push(x, m.h - 1);
    }
    for (int64_t y = 0; y < m.h; ++y) {
        push(0, y);
        push(m.w - 1, y);
    }
    while (!stack.empty()) {
        const int64_t i = stack.back();
        stack.pop_back();
        const int64_t x = i % m.w, y = i / m.w;
        if (x > 0) push(x - 1, y);
        if (x + 1 < m.w) push(x + 1, y);
        if (y > 0) push(x, y - 1);
        if (y + 1 < m.h) push(x, y + 1);
    }
    for (auto& v : m.v)
        v = (v == 2) ? 0 : 1;
}

namespace {
int32_t findRoot(std::vector<int32_t>& parent, int32_t a) {
    while (parent[a] != a) {
        parent[a] = parent[parent[a]];
        a = parent[a];
    }
    return a;
}
} // namespace

std::vector<Component> connectedComponents(const Mask& m, std::vector<int32_t>& labels) {
    labels.assign(m.v.size(), 0);
    std::vector<int32_t> parent{0};
    for (int64_t y = 0; y < m.h; ++y)
        for (int64_t x = 0; x < m.w; ++x) {
            if (!m.at(x, y)) continue;
            int32_t best = 0;
            const int64_t nb[4][2] = {{x - 1, y}, {x - 1, y - 1}, {x, y - 1}, {x + 1, y - 1}};
            for (auto& p : nb) {
                if (p[0] < 0 || p[1] < 0 || p[0] >= m.w) continue;
                const int32_t l = labels[p[1] * m.w + p[0]];
                if (!l) continue;
                if (!best)
                    best = findRoot(parent, l);
                else {
                    const int32_t a = findRoot(parent, best), b = findRoot(parent, l);
                    if (a != b) parent[std::max(a, b)] = std::min(a, b);
                    best = std::min(a, b);
                }
            }
            if (!best) {
                best = static_cast<int32_t>(parent.size());
                parent.push_back(best);
            }
            labels[y * m.w + x] = best;
        }
    // compact labels
    std::vector<int32_t> remap(parent.size(), 0);
    int32_t next = 0;
    for (size_t i = 1; i < parent.size(); ++i) {
        const int32_t r = findRoot(parent, static_cast<int32_t>(i));
        if (!remap[r]) remap[r] = ++next;
        remap[i] = remap[r];
    }
    std::vector<Component> comps(next);
    for (int32_t i = 0; i < next; ++i) {
        comps[i].label = i + 1;
        comps[i].x0 = comps[i].y0 = std::numeric_limits<int64_t>::max();
        comps[i].x1 = comps[i].y1 = -1;
    }
    for (int64_t y = 0; y < m.h; ++y)
        for (int64_t x = 0; x < m.w; ++x) {
            int32_t& l = labels[y * m.w + x];
            if (!l) continue;
            l = remap[l];
            Component& c = comps[l - 1];
            c.area++;
            c.x0 = std::min(c.x0, x);
            c.y0 = std::min(c.y0, y);
            c.x1 = std::max(c.x1, x);
            c.y1 = std::max(c.y1, y);
        }
    for (int64_t y = 0; y < m.h; ++y)
        for (int64_t x = 0; x < m.w; ++x) {
            const int32_t l = labels[y * m.w + x];
            if (!l) continue;
            const bool edge = x == 0 || y == 0 || x == m.w - 1 || y == m.h - 1 ||
                              labels[y * m.w + x - 1] != l || labels[y * m.w + x + 1] != l ||
                              labels[(y - 1) * m.w + x] != l || labels[(y + 1) * m.w + x] != l;
            if (!edge) continue;
            auto& bc = comps[l - 1].boundaryCorners;
            const double fx = static_cast<double>(x), fy = static_cast<double>(y);
            bc.push_back({fx, fy});
            bc.push_back({fx + 1, fy});
            bc.push_back({fx + 1, fy + 1});
            bc.push_back({fx, fy + 1});
        }
    return comps;
}

std::vector<Vec2> convexHull(std::vector<Vec2> pts) {
    std::sort(pts.begin(), pts.end(), [](Vec2 a, Vec2 b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    pts.erase(std::unique(pts.begin(), pts.end()), pts.end());
    if (pts.size() < 3) return pts;
    std::vector<Vec2> h(2 * pts.size());
    size_t k = 0;
    for (size_t i = 0; i < pts.size(); ++i) {
        while (k >= 2 && cross(h[k - 1] - h[k - 2], pts[i] - h[k - 2]) <= 0)
            --k;
        h[k++] = pts[i];
    }
    for (size_t i = pts.size() - 1, t = k + 1; i > 0; --i) {
        while (k >= t && cross(h[k - 1] - h[k - 2], pts[i - 1] - h[k - 2]) <= 0)
            --k;
        h[k++] = pts[i - 1];
    }
    h.resize(k - 1);
    return h;
}

Quad RotRect::quad() const {
    const Vec2 u{std::cos(angle), std::sin(angle)}, v{-std::sin(angle), std::cos(angle)};
    const Vec2 a = u * (w / 2), b = v * (h / 2);
    return normalizeQuad({c - a - b, c + a - b, c + a + b, c - a + b});
}

RotRect minAreaRect(const std::vector<Vec2>& hull) {
    RotRect best;
    best.w = best.h = std::numeric_limits<double>::max() / 4;
    if (hull.size() < 3) return RotRect{};
    for (size_t i = 0; i < hull.size(); ++i) {
        const Vec2 e = normalized(hull[(i + 1) % hull.size()] - hull[i]);
        if (norm(e) == 0) continue;
        const Vec2 n = perp(e);
        double minU = std::numeric_limits<double>::max(), maxU = -minU, minV = minU, maxV = -minU;
        for (const Vec2& p : hull) {
            const double u = dot(p, e), v = dot(p, n);
            minU = std::min(minU, u);
            maxU = std::max(maxU, u);
            minV = std::min(minV, v);
            maxV = std::max(maxV, v);
        }
        const double area = (maxU - minU) * (maxV - minV);
        if (area < best.w * best.h) {
            best.w = maxU - minU;
            best.h = maxV - minV;
            best.c = e * ((minU + maxU) / 2) + n * ((minV + maxV) / 2);
            best.angle = std::atan2(e.y, e.x);
        }
    }
    // normalise angle to (-π/4, π/4], swapping w/h for quarter turns
    constexpr double kPi = std::numbers::pi;
    while (best.angle <= -kPi / 4) {
        best.angle += kPi / 2;
        std::swap(best.w, best.h);
    }
    while (best.angle > kPi / 4) {
        best.angle -= kPi / 2;
        std::swap(best.w, best.h);
    }
    return best;
}

std::optional<Line> fitLineRobust(const std::vector<Vec2>& pts, std::vector<double>* weightsOut) {
    if (pts.size() < 2) return std::nullopt;
    std::vector<double> w(pts.size(), 1.0);
    Line line{};
    // Least-median-of-squares initialisation over deterministic point pairs: IRLS started from a plain
    // least-squares fit can settle on a compromise when gross outliers pull the first estimate.
    if (pts.size() >= 6) {
        const size_t n = pts.size();
        double bestMed = std::numeric_limits<double>::max();
        Line best{};
        std::vector<double> absr(n);
        const size_t K = std::min<size_t>(64, n);
        for (size_t k = 0; k < K; ++k) {
            const size_t i = k * n / K;
            for (const size_t off : {n / 2, n / 3, n / 5 + 1}) {
                const size_t j = (i + off) % n;
                const Vec2 d = pts[j] - pts[i];
                const double len = std::hypot(d.x, d.y);
                if (len < 1e-9) continue;
                const Line cand{pts[i], {d.x / len, d.y / len}};
                for (size_t q = 0; q < n; ++q)
                    absr[q] = std::abs(signedDistance(cand, pts[q]));
                const double med = median(absr);
                if (med < bestMed) {
                    bestMed = med;
                    best = cand;
                }
            }
        }
        if (bestMed < std::numeric_limits<double>::max()) {
            const double s0 = std::max(0.25, 1.4826 * bestMed), c0 = 4.685 * s0;
            for (size_t q = 0; q < n; ++q) {
                const double u = signedDistance(best, pts[q]) / c0;
                w[q] = std::abs(u) < 1 ? (1 - u * u) * (1 - u * u) : 0.0;
            }
        }
    }
    for (int iter = 0; iter < 12; ++iter) {
        double sw = 0, mx = 0, my = 0;
        for (size_t i = 0; i < pts.size(); ++i) {
            sw += w[i];
            mx += w[i] * pts[i].x;
            my += w[i] * pts[i].y;
        }
        if (sw <= 0) return std::nullopt;
        mx /= sw;
        my /= sw;
        double sxx = 0, sxy = 0, syy = 0;
        for (size_t i = 0; i < pts.size(); ++i) {
            const double dx = pts[i].x - mx, dy = pts[i].y - my;
            sxx += w[i] * dx * dx;
            sxy += w[i] * dx * dy;
            syy += w[i] * dy * dy;
        }
        const double theta = 0.5 * std::atan2(2 * sxy, sxx - syy);
        line = Line{{mx, my}, {std::cos(theta), std::sin(theta)}};
        std::vector<double> res(pts.size());
        for (size_t i = 0; i < pts.size(); ++i)
            res[i] = signedDistance(line, pts[i]);
        std::vector<double> absr(res.size());
        for (size_t i = 0; i < res.size(); ++i)
            absr[i] = std::abs(res[i]);
        const double s = std::max(0.25, 1.4826 * median(absr));
        const double c = 4.685 * s;
        for (size_t i = 0; i < pts.size(); ++i) {
            const double u = res[i] / c;
            w[i] = std::abs(u) < 1 ? (1 - u * u) * (1 - u * u) : 0.0;
        }
    }
    if (weightsOut) *weightsOut = w;
    return line;
}

std::optional<Circle> fitCircleRobust(const std::vector<Vec2>& pts) {
    if (pts.size() < 3) return std::nullopt;
    std::vector<double> w(pts.size(), 1.0);
    Circle c{};
    for (int iter = 0; iter < 12; ++iter) {
        // Kasa: x² + y² + D x + E y + F = 0, weighted least squares (3×3 normal equations)
        double a[3][4] = {{0}};
        for (size_t i = 0; i < pts.size(); ++i) {
            const double x = pts[i].x, y = pts[i].y, z = -(x * x + y * y);
            const double row[3] = {x, y, 1.0};
            for (int r = 0; r < 3; ++r) {
                for (int k = 0; k < 3; ++k)
                    a[r][k] += w[i] * row[r] * row[k];
                a[r][3] += w[i] * row[r] * z;
            }
        }
        for (int col = 0; col < 3; ++col) {
            int piv = col;
            for (int r = col + 1; r < 3; ++r)
                if (std::abs(a[r][col]) > std::abs(a[piv][col])) piv = r;
            if (std::abs(a[piv][col]) < 1e-12) return std::nullopt;
            for (int k = 0; k < 4; ++k)
                std::swap(a[col][k], a[piv][k]);
            for (int r = 0; r < 3; ++r) {
                if (r == col) continue;
                const double f = a[r][col] / a[col][col];
                for (int k = col; k < 4; ++k)
                    a[r][k] -= f * a[col][k];
            }
        }
        const double D = a[0][3] / a[0][0], E = a[1][3] / a[1][1], F = a[2][3] / a[2][2];
        c.c = {-D / 2, -E / 2};
        const double r2 = c.c.x * c.c.x + c.c.y * c.c.y - F;
        if (r2 <= 0) return std::nullopt;
        c.r = std::sqrt(r2);
        std::vector<double> res(pts.size()), absr(pts.size());
        for (size_t i = 0; i < pts.size(); ++i) {
            res[i] = norm(pts[i] - c.c) - c.r;
            absr[i] = std::abs(res[i]);
        }
        const double s = std::max(0.25, 1.4826 * median(absr));
        const double k = 4.685 * s;
        for (size_t i = 0; i < pts.size(); ++i) {
            const double u = res[i] / k;
            w[i] = std::abs(u) < 1 ? (1 - u * u) * (1 - u * u) : 0.0;
        }
    }
    return c;
}

std::array<double, 6> fitPoly2(const std::vector<std::array<double, 3>>& xyz) {
    double a[6][7] = {{0}};
    for (const auto& p : xyz) {
        const double x = p[0], y = p[1];
        const double row[6] = {1, x, y, x * x, x * y, y * y};
        for (int r = 0; r < 6; ++r) {
            for (int k = 0; k < 6; ++k)
                a[r][k] += row[r] * row[k];
            a[r][6] += row[r] * p[2];
        }
    }
    for (int r = 0; r < 6; ++r)
        a[r][r] += 1e-9; // ridge for degenerate samples
    for (int col = 0; col < 6; ++col) {
        int piv = col;
        for (int r = col + 1; r < 6; ++r)
            if (std::abs(a[r][col]) > std::abs(a[piv][col])) piv = r;
        for (int k = 0; k < 7; ++k)
            std::swap(a[col][k], a[piv][k]);
        if (std::abs(a[col][col]) < 1e-15) continue;
        for (int r = 0; r < 6; ++r) {
            if (r == col) continue;
            const double f = a[r][col] / a[col][col];
            for (int k = col; k < 7; ++k)
                a[r][k] -= f * a[col][k];
        }
    }
    std::array<double, 6> c{};
    for (int i = 0; i < 6; ++i)
        c[i] = std::abs(a[i][i]) > 1e-15 ? a[i][6] / a[i][i] : 0.0;
    return c;
}

} // namespace fk::cv
