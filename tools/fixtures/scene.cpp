// fkfixture — scene construction (film types, appearance, layout) and ground truth.
#include "scene.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdio>

namespace fkfx {

// ---------------------------------------------------------------------------------------------
// Catalogues (docs/film-types.md; inner offsets are the fixture's fixed choice)
// ---------------------------------------------------------------------------------------------
const std::vector<FilmType>& filmTypes() {
    static const std::vector<FilmType> kTypes = {
        {"polaroid_600", 88.0, 107.0, InnerKind::Quad, 4.5, 6.0, 79.0, 79.0, 1.0},
        // circle diameter 75 centred at (44.0, 45.5): bounding square (6.5, 8.0) + 75
        {"polaroid_round", 88.0, 107.0, InnerKind::Ellipse, 6.5, 8.0, 75.0, 75.0, 1.0},
        {"instax_mini", 54.0, 86.0, InnerKind::Quad, 4.0, 6.0, 46.0, 62.0, 3.0},
        {"instax_square", 72.0, 85.6, InnerKind::Quad, 5.0, 7.0, 62.0, 62.0, 3.0},
        {"instax_wide", 108.0, 86.0, InnerKind::Quad, 4.5, 6.5, 99.0, 62.0, 3.0},
    };
    return kTypes;
}

const std::vector<FrameDef>& frameDefs() {
    static const std::vector<FrameDef> kFrames = {
        {"white", {0.93, 0.93, 0.91}}, {"cream", {0.92, 0.89, 0.80}}, {"aged", {0.88, 0.82, 0.62}},
        {"gold", {0.78, 0.64, 0.30}},  {"black", {0.07, 0.07, 0.07}}, {"colour", {0.20, 0.40, 0.80}},
    };
    return kFrames;
}

const char* contentName(Content c) {
    switch (c) {
    case Content::Normal:
        return "normal";
    case Content::Dark:
        return "dark";
    case Content::Bright:
        return "bright";
    }
    return "normal";
}

std::vector<std::string> featureNames(unsigned mask) {
    static const char* kNames[] = {"handwriting", "stain", "glare", "shadow", "dust", "scratch"};
    std::vector<std::string> out;
    for (unsigned i = 0; i < 6; ++i)
        if (mask & (1u << i)) out.emplace_back(kNames[i]);
    return out;
}

const char* compressionName(Compression c) {
    switch (c) {
    case Compression::None:
        return "none";
    case Compression::Lzw:
        return "lzw";
    case Compression::Deflate:
        return "deflate";
    case Compression::PackBits:
        return "packbits";
    }
    return "none";
}
const char* layoutName(Layout l) {
    switch (l) {
    case Layout::Strips:
        return "strips";
    case Layout::SingleStrip:
        return "single-strip";
    case Layout::Tiles:
        return "tiles";
    }
    return "strips";
}
const char* photometricName(Photometric p) {
    switch (p) {
    case Photometric::Rgb:
        return "rgb";
    case Photometric::MinIsBlack:
        return "minisblack";
    case Photometric::MinIsWhite:
        return "miniswhite";
    }
    return "rgb";
}

Vec2 Print::toScan(double u, double v) const {
    const double U = (u - W * 0.5) * s, V = (v - H * 0.5) * s;
    return {cx + cosp * U - sinp * V, cy + sinp * U + cosp * V};
}

namespace {

// RNG stream ids (independent decisions use independent streams).
constexpr std::uint64_t kStreamPrints = 1;
constexpr std::uint64_t kStreamLayout = 2;
constexpr std::uint64_t kStreamDust = 3;
constexpr std::uint64_t kStreamScratch = 4;
constexpr std::uint64_t kStreamBackground = 5;
constexpr std::uint64_t kStreamAppearance = 1000;

constexpr double kGapMm = 3.0;    // minimum gap between rotated bounding boxes
constexpr double kMarginMm = 3.0; // minimum distance from the scan edge

double roundHalfUp(double v) {
    return std::floor(v + 0.5);
}

Rgb randomColour(Pcg32& rng, double vlo, double vhi) {
    const double v = rng.uniform(vlo, vhi);
    const double sat = rng.uniform(0.0, 0.35);
    Rgb c{v + sat * (rng.uniform() - 0.5) * 2.0, v + sat * (rng.uniform() - 0.5) * 2.0,
          v + sat * (rng.uniform() - 0.5) * 2.0};
    c.r = clampd(c.r, 0.02, 0.97);
    c.g = clampd(c.g, 0.02, 0.97);
    c.b = clampd(c.b, 0.02, 0.97);
    return c;
}

void makeContent(Print& p, Content kind, Pcg32& rng) {
    ContentParams& c = p.content;
    c.kind = kind;
    c.key = rng.next64();
    const double iw = p.ix1 - p.ix0, ih = p.iy1 - p.iy0;
    if (kind == Content::Normal) {
        c.sky0 = randomColour(rng, 0.45, 0.95);
        c.sky1 = randomColour(rng, 0.30, 0.85);
        c.ground0 = randomColour(rng, 0.05, 0.60);
        c.ground1 = randomColour(rng, 0.10, 0.70);
        c.horizon = rng.uniform(0.3, 0.7);
        c.horizonSlope = rng.uniform(-0.25, 0.25);
        c.horizonWave = rng.uniform(0.0, 0.06);
        c.horizonFreq = rng.uniform(1.0, 4.0);
        const int nd = 2 + rng.index(4);
        for (int i = 0; i < nd; ++i) {
            Disc d{};
            d.cx = rng.uniform(0.0, iw);
            d.cy = rng.uniform(0.0, ih);
            d.r = rng.uniform(0.05, 0.22) * std::min(iw, ih);
            d.soft = rng.uniform(0.1, 1.2);
            d.col = randomColour(rng, 0.03, 0.97);
            d.shade = rng.uniform(0.0, 0.35);
            c.discs.push_back(d);
        }
        c.stripes = rng.chance(0.6);
        c.stripeX0 = rng.uniform(0.0, 0.6) * iw;
        c.stripeX1 = c.stripeX0 + rng.uniform(0.2, 0.4) * iw;
        c.stripeY0 = rng.uniform(0.0, 0.6) * ih;
        c.stripeY1 = c.stripeY0 + rng.uniform(0.2, 0.4) * ih;
        sinCos(rng.uniform(0.0, kPi), c.stripeSin, c.stripeCos);
        c.stripePeriod = rng.uniform(1.5, 6.0);
        c.stripeA = randomColour(rng, 0.05, 0.40);
        c.stripeB = randomColour(rng, 0.60, 0.95);
        c.noiseScale = rng.uniform(4.0, 12.0);
        c.noiseAmp = rng.uniform(0.06, 0.16);
    } else {
        if (kind == Content::Dark) {
            c.lo = 0.02;
            c.hi = 0.08;
        } else {
            c.lo = 0.85;
            c.hi = 0.97;
        }
        c.tint = {rng.uniform(-0.15, 0.15), rng.uniform(-0.15, 0.15), rng.uniform(-0.15, 0.15)};
        c.noiseScale = rng.uniform(5.0, 15.0);
    }
}

void buildHandwritingGrid(Handwriting& hw) {
    if (hw.segs.empty()) return;
    const double m = hw.halfWidth + 0.05;
    double u0 = 1e300, v0 = 1e300, u1 = -1e300, v1 = -1e300;
    for (const Segment& sg : hw.segs) {
        u0 = std::min({u0, sg.ax, sg.bx});
        v0 = std::min({v0, sg.ay, sg.by});
        u1 = std::max({u1, sg.ax, sg.bx});
        v1 = std::max({v1, sg.ay, sg.by});
    }
    hw.u0 = u0 - m;
    hw.v0 = v0 - m;
    hw.cell = 1.0;
    hw.gw = static_cast<int>((u1 + m - hw.u0) / hw.cell) + 1;
    hw.gh = static_cast<int>((v1 + m - hw.v0) / hw.cell) + 1;
    const std::size_t ncell = static_cast<std::size_t>(hw.gw) * static_cast<std::size_t>(hw.gh);
    std::vector<std::uint32_t> counts(ncell + 1, 0);
    auto forCells = [&](const Segment& sg, auto&& fn) {
        const int cu0 = static_cast<int>((std::min(sg.ax, sg.bx) - m - hw.u0) / hw.cell);
        const int cu1 = static_cast<int>((std::max(sg.ax, sg.bx) + m - hw.u0) / hw.cell);
        const int cv0 = static_cast<int>((std::min(sg.ay, sg.by) - m - hw.v0) / hw.cell);
        const int cv1 = static_cast<int>((std::max(sg.ay, sg.by) + m - hw.v0) / hw.cell);
        for (int cv = std::max(cv0, 0); cv <= std::min(cv1, hw.gh - 1); ++cv)
            for (int cu = std::max(cu0, 0); cu <= std::min(cu1, hw.gw - 1); ++cu)
                fn(static_cast<std::size_t>(cv) * static_cast<std::size_t>(hw.gw) +
                   static_cast<std::size_t>(cu));
    };
    for (const Segment& sg : hw.segs)
        forCells(sg, [&](std::size_t ci) { ++counts[ci + 1]; });
    for (std::size_t i = 1; i <= ncell; ++i)
        counts[i] += counts[i - 1];
    hw.cellStart = counts;
    hw.cellItems.assign(counts[ncell], 0);
    std::vector<std::uint32_t> fill(counts.begin(), counts.end() - 1);
    for (std::uint32_t si = 0; si < hw.segs.size(); ++si)
        forCells(hw.segs[si], [&](std::size_t ci) { hw.cellItems[fill[ci]++] = si; });
}

void makeHandwriting(Print& p, Pcg32& rng) {
    Handwriting& hw = p.hw;
    const double innerBottom = p.ellipse ? p.ccy + p.cr : p.iy1;
    const double vb0 = innerBottom + 1.5, vb1 = p.H - 2.0;
    const double band = vb1 - vb0;
    double xh = std::min(3.5, band * 0.24) * rng.uniform(0.8, 1.1);
    const double asc = 2.0, desc = 0.8;
    if ((asc + desc + 0.1) * xh > band) xh = band / (asc + desc + 0.1);
    const double bLo = vb0 + asc * xh + 0.02, bHi = vb1 - desc * xh - 0.02;
    const double baseline0 = rng.uniform(bLo, std::max(bLo, bHi));
    hw.halfWidth = rng.uniform(0.35, 0.5) * 0.5;
    hw.ink = rng.chance(0.6) ? Rgb{0.12, 0.16, 0.42} : Rgb{0.06, 0.06, 0.08};
    hw.opacity = rng.uniform(0.85, 0.95);
    const double margin = 5.0;
    const double avail = p.W - 2.0 * margin;
    const double budget = avail * rng.uniform(0.45, 0.85);
    double x = margin + rng.uniform(0.0, avail - budget);
    const double xEnd = x + budget;
    const double slant = rng.uniform(0.15, 0.4);
    const double baseTilt = rng.uniform(-0.03, 0.03);
    const double xStart = x;
    const int words = 1 + rng.index(3);
    for (int w = 0; w < words; ++w) {
        const int letters = 3 + rng.index(5);
        const double lw = xh * rng.uniform(0.55, 0.8);
        if (x + letters * lw > xEnd) break;
        const double R = lw / kTwoPi;
        const double d = R * rng.uniform(1.15, 1.6);
        std::vector<double> amps(static_cast<std::size_t>(letters));
        for (double& a : amps) {
            const double t = rng.uniform();
            a = t < 0.6 ? rng.uniform(0.85, 1.1) : (t < 0.82 ? rng.uniform(1.7, 2.0) : -desc);
        }
        const int perLetter = 14;
        bool havePrev = false;
        double px = 0, py = 0;
        for (int k = 0; k <= letters * perLetter; ++k) {
            const double th = kTwoPi * k / perLetter;
            const int li = std::min(k / perLetter, letters - 1);
            double sn, cs;
            sinCos(th, sn, cs);
            double X = x + R * th - d * sn;
            const double baseline = baseline0 + baseTilt * (X - xStart);
            const double Y = baseline - amps[static_cast<std::size_t>(li)] * xh * (1.0 - cs) * 0.5;
            X += slant * (baseline - Y);
            if (havePrev) hw.segs.push_back({px, py, X, Y});
            px = X;
            py = Y;
            havePrev = true;
        }
        x += letters * lw + lw * rng.uniform(0.8, 1.6);
    }
    buildHandwritingGrid(hw);
}

void makeAppearance(Print& p, Content content, Pcg32& rng) {
    p.key = rng.next64();
    switch (rng.index(4)) { // aged: yellowing toward one card edge
    case 0:
        p.ageDirU = 0;
        p.ageDirV = 1.0 / p.H;
        p.ageOff = 0;
        break;
    case 1:
        p.ageDirU = 0;
        p.ageDirV = -1.0 / p.H;
        p.ageOff = 1;
        break;
    case 2:
        p.ageDirU = 1.0 / p.W;
        p.ageDirV = 0;
        p.ageOff = 0;
        break;
    default:
        p.ageDirU = -1.0 / p.W;
        p.ageDirV = 0;
        p.ageOff = 1;
        break;
    }
    makeContent(p, content, rng);

    // Each feature draws from its own sub-stream so enabling one never changes another.
    if (p.features & kHandwriting) {
        Pcg32 r(rng.next64(), 11);
        makeHandwriting(p, r);
    } else {
        rng.next64();
    }
    {
        Pcg32 r(rng.next64(), 12);
        if (p.features & kStain) {
            const int n = 2 + r.index(2);
            for (int i = 0; i < n; ++i) {
                Stain st{};
                st.radius = r.uniform(2.5, 6.5);
                for (int tries = 0; tries < 200; ++tries) {
                    st.cu = r.uniform(1.5, p.W - 1.5);
                    st.cv = r.uniform(1.5, p.H - 1.5);
                    if (sdInner(p, st.cu, st.cv) > 0.3 * st.radius) break; // mostly on the frame
                }
                st.alpha = r.uniform(0.18, 0.38);
                st.ring = r.uniform(0.05, 0.2);
                st.tint = {r.uniform(0.78, 0.86), r.uniform(0.60, 0.70), r.uniform(0.38, 0.50)};
                st.key = r.next64();
                p.stains.push_back(st);
            }
        }
    }
    {
        Pcg32 r(rng.next64(), 13);
        Glare& g = p.glare;
        g.cu = r.uniform(0.2, 0.8) * p.W;
        g.cv = r.uniform(0.15, 0.7) * p.H;
        sinCos(r.uniform(0.0, kPi), g.dv, g.du);
        g.halfWidth = r.uniform(3.0, 10.0);
        g.halfLength = r.uniform(15.0, 45.0);
        g.amount = r.uniform(0.25, 0.5);
    }
    {
        Pcg32 r(rng.next64(), 14);
        Shadow& sh = p.shadow;
        const double mag = r.uniform(0.3, 0.8);
        if (r.chance(0.5)) { // one edge
            const int e = r.index(4);
            sh.du = (e == 0 ? 1.0 : (e == 1 ? -1.0 : 0.0)) * mag;
            sh.dv = (e == 2 ? 1.0 : (e == 3 ? -1.0 : 0.0)) * mag;
        } else { // two adjacent edges
            sh.du = (r.chance(0.5) ? 1.0 : -1.0) * mag * 0.70710678118654752;
            sh.dv = (r.chance(0.5) ? 1.0 : -1.0) * mag * 0.70710678118654752;
        }
        sh.strength = r.uniform(0.35, 0.6);
        sh.falloff = r.uniform(0.8, 1.2);
    }
}

// ----- Layout ---------------------------------------------------------------------------------
struct Item {
    std::vector<int> prints;
    std::vector<Vec2> offsets; // print centre relative to the item AABB top-left (mm)
    double w = 0, h = 0;
};

void rotationOf(int q, double tiltDeg, double& c, double& s) {
    double st, ct;
    sinCos(degToRad(tiltDeg), st, ct);
    switch (q & 3) {
    case 0:
        c = ct;
        s = st;
        break;
    case 1:
        c = -st;
        s = ct;
        break;
    case 2:
        c = -ct;
        s = -st;
        break;
    default:
        c = st;
        s = -ct;
        break;
    }
}

Item makeItem(const std::vector<Print>& prints, const std::vector<int>& members, double dvShift) {
    Item it;
    it.prints = members;
    const Print& a = prints[static_cast<std::size_t>(members[0])];
    double c, s;
    rotationOf(a.quarterTurns, a.tiltDeg, c, s);
    std::vector<Vec2> centres;
    centres.push_back({0, 0});
    if (members.size() == 2) { // touching pair: B's left edge on A's right edge (card frame)
        const Print& b = prints[static_cast<std::size_t>(members[1])];
        const double du = (a.W + b.W) * 0.5, dv = (b.H - a.H) * 0.5 + dvShift;
        centres.push_back({c * du - s * dv, s * du + c * dv});
    }
    double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300;
    for (std::size_t i = 0; i < members.size(); ++i) {
        const Print& p = prints[static_cast<std::size_t>(members[i])];
        for (int k = 0; k < 4; ++k) {
            const double U = (k == 1 || k == 2 ? 0.5 : -0.5) * p.W, V = (k >= 2 ? 0.5 : -0.5) * p.H;
            const double X = centres[i].x + c * U - s * V, Y = centres[i].y + s * U + c * V;
            x0 = std::min(x0, X);
            y0 = std::min(y0, Y);
            x1 = std::max(x1, X);
            y1 = std::max(y1, Y);
        }
    }
    it.w = x1 - x0;
    it.h = y1 - y0;
    for (const Vec2& ce : centres)
        it.offsets.push_back({ce.x - x0, ce.y - y0});
    return it;
}

bool rowsFit(const std::vector<Item>& items, const std::vector<std::vector<int>>& rows, double availW,
             double availH) {
    double total = 0;
    for (const auto& row : rows) {
        double w = 0, h = 0;
        for (int i : row) {
            w += items[static_cast<std::size_t>(i)].w;
            h = std::max(h, items[static_cast<std::size_t>(i)].h);
        }
        w += kGapMm * static_cast<double>(row.size() - 1);
        if (w > availW) return false;
        total += h;
    }
    total += kGapMm * static_cast<double>(rows.size() - 1);
    return total <= availH;
}

std::optional<std::vector<std::vector<int>>> partition(const std::vector<Item>& items, double availW,
                                                       double availH) {
    const int n = static_cast<int>(items.size());
    // Even split into R rows, smallest R first.
    for (int R = 1; R <= n; ++R) {
        std::vector<std::vector<int>> rows(static_cast<std::size_t>(R));
        int k = 0;
        for (int r = 0; r < R; ++r) {
            const int cnt = n / R + (r < n % R ? 1 : 0);
            for (int j = 0; j < cnt; ++j)
                rows[static_cast<std::size_t>(r)].push_back(k++);
        }
        if (rowsFit(items, rows, availW, availH)) return rows;
    }
    // Greedy shelf fill in the given order, then in order of decreasing height (FFDH-like).
    auto greedy = [&](const std::vector<int>& order) {
        std::vector<std::vector<int>> rows(1);
        double w = 0;
        for (int i : order) {
            const double iw = items[static_cast<std::size_t>(i)].w;
            if (!rows.back().empty() && w + kGapMm + iw > availW) {
                rows.emplace_back();
                w = 0;
            }
            w += (rows.back().empty() ? 0.0 : kGapMm) + iw;
            rows.back().push_back(i);
        }
        return rows;
    };
    std::vector<int> order(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
        order[static_cast<std::size_t>(i)] = i;
    auto rows = greedy(order);
    if (rowsFit(items, rows, availW, availH)) return rows;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        return items[static_cast<std::size_t>(a)].h > items[static_cast<std::size_t>(b)].h;
    });
    rows = greedy(order);
    if (rowsFit(items, rows, availW, availH)) return rows;
    return std::nullopt;
}

std::vector<double> jitteredSplit(double total, std::size_t parts, Pcg32& rng) {
    std::vector<double> w(parts);
    double sum = 0;
    for (double& v : w) {
        v = 1.0 + 0.7 * (2.0 * rng.uniform() - 1.0);
        sum += v;
    }
    for (double& v : w)
        v = total * v / sum;
    return w;
}

bool layoutPrints(const Options& opt, std::vector<Print>& prints, Pcg32& rng) {
    const double availW = opt.scanWmm - 2.0 * kMarginMm, availH = opt.scanHmm - 2.0 * kMarginMm;
    const int n = static_cast<int>(prints.size());
    for (int attempt = 0; attempt < 400; ++attempt) {
        for (Print& p : prints) {
            p.quarterTurns = opt.quarterTurns < 0 ? rng.index(4) : opt.quarterTurns;
            p.tiltDeg = opt.tiltMax > 0 ? rng.uniform(-opt.tiltMax, opt.tiltMax) : 0.0;
        }
        double dvShift = 0;
        if (opt.touching && n >= 2) {
            prints[1].quarterTurns = prints[0].quarterTurns;
            prints[1].tiltDeg = prints[0].tiltDeg;
            dvShift = rng.uniform(0.0, 0.15) * std::min(prints[0].H, prints[1].H);
        }
        std::vector<Item> items;
        int first = 0;
        if (opt.touching && n >= 2) {
            items.push_back(makeItem(prints, {0, 1}, dvShift));
            first = 2;
        }
        for (int i = first; i < n; ++i)
            items.push_back(makeItem(prints, {i}, 0.0));
        for (std::size_t i = items.size(); i > 1; --i) // Fisher-Yates
            std::swap(items[i - 1], items[static_cast<std::size_t>(rng.index(static_cast<int>(i)))]);
        const auto rows = partition(items, availW, availH);
        if (!rows) continue;

        // Distribute free space with jitter.
        std::vector<double> rowH;
        double sumH = 0;
        for (const auto& row : *rows) {
            double h = 0;
            for (int i : row)
                h = std::max(h, items[static_cast<std::size_t>(i)].h);
            rowH.push_back(h);
            sumH += h;
        }
        const double freeV = availH - sumH - kGapMm * static_cast<double>(rows->size() - 1);
        const auto vparts = jitteredSplit(freeV, rows->size() + 1, rng);
        double y = kMarginMm + vparts[0];
        for (std::size_t r = 0; r < rows->size(); ++r) {
            const auto& row = (*rows)[r];
            double sumW = 0;
            for (int i : row)
                sumW += items[static_cast<std::size_t>(i)].w;
            const double freeH = availW - sumW - kGapMm * static_cast<double>(row.size() - 1);
            const auto hparts = jitteredSplit(freeH, row.size() + 1, rng);
            double x = kMarginMm + hparts[0];
            for (std::size_t j = 0; j < row.size(); ++j) {
                const Item& it = items[static_cast<std::size_t>(row[j])];
                const double top = y + (rowH[r] - it.h) * clamp01(0.5 + 0.8 * (rng.uniform() - 0.5));
                for (std::size_t m = 0; m < it.prints.size(); ++m) {
                    Print& p = prints[static_cast<std::size_t>(it.prints[m])];
                    p.cx = (x + it.offsets[m].x) * p.s;
                    p.cy = (top + it.offsets[m].y) * p.s;
                }
                x += it.w + kGapMm + hparts[j + 1];
            }
            y += rowH[r] + kGapMm + vparts[r + 1];
        }
        return true;
    }
    return false;
}

void finalisePlacement(Print& p) {
    rotationOf(p.quarterTurns, p.tiltDeg, p.cosp, p.sinp);
    const double c = p.cosp, s = p.sinp;
    p.a11 = c / p.s;
    p.a12 = s / p.s;
    p.a13 = -(c * p.cx + s * p.cy) / p.s + p.W * 0.5;
    p.a21 = -s / p.s;
    p.a22 = c / p.s;
    p.a23 = (s * p.cx - c * p.cy) / p.s + p.H * 0.5;
    p.aabbW = p.W * std::fabs(c) + p.H * std::fabs(s);
    p.aabbH = p.W * std::fabs(s) + p.H * std::fabs(c);
    double marginMm = 0.0;
    if (p.features & kShadow)
        marginMm = std::sqrt(p.shadow.du * p.shadow.du + p.shadow.dv * p.shadow.dv) + p.shadow.falloff + 0.2;
    const double hx = (p.aabbW * 0.5 + marginMm) * p.s + 3.0, hy = (p.aabbH * 0.5 + marginMm) * p.s + 3.0;
    p.bx0 = p.cx - hx;
    p.bx1 = p.cx + hx;
    p.by0 = p.cy - hy;
    p.by1 = p.cy + hy;
}

std::vector<Vec2> orderFromMinSum(std::vector<Vec2> pts) {
    // pts are already clockwise on screen (card corners TL, TR, BR, BL; rotation keeps orientation).
    std::size_t best = 0;
    for (std::size_t i = 1; i < pts.size(); ++i) {
        const double si = pts[i].x + pts[i].y, sb = pts[best].x + pts[best].y;
        if (si < sb - 1e-9 || (si <= sb + 1e-9 && pts[i].y < pts[best].y)) best = i;
    }
    std::rotate(pts.begin(), pts.begin() + static_cast<std::ptrdiff_t>(best), pts.end());
    return pts;
}

int minSumCornerIndex(const Print& p) {
    const Vec2 c[4] = {p.toScan(0, 0), p.toScan(p.W, 0), p.toScan(p.W, p.H), p.toScan(0, p.H)};
    int best = 0;
    for (int i = 1; i < 4; ++i) {
        const double si = c[i].x + c[i].y, sb = c[best].x + c[best].y;
        if (si < sb - 1e-9 || (si <= sb + 1e-9 && c[i].y < c[best].y)) best = i;
    }
    return best;
}

nlohmann::ordered_json arr2(double a, double b) {
    return nlohmann::ordered_json::array({a, b});
}

} // namespace

std::vector<Vec2> outerQuad(const Print& p) {
    return orderFromMinSum({p.toScan(0, 0), p.toScan(p.W, 0), p.toScan(p.W, p.H), p.toScan(0, p.H)});
}

std::vector<Vec2> innerQuad(const Print& p) {
    return orderFromMinSum(
        {p.toScan(p.ix0, p.iy0), p.toScan(p.ix1, p.iy0), p.toScan(p.ix1, p.iy1), p.toScan(p.ix0, p.iy1)});
}

// n points, uniform in arc length along the rounded rectangle, clockwise on screen, starting at the
// middle of the corner arc that belongs to outerQuad()[0].
std::vector<Vec2> outerBoundary(const Print& p, int n) {
    const double r = p.r, sw = p.W - 2.0 * r, sh = p.H - 2.0 * r, qa = r * kPi * 0.5;
    const double perim = 2.0 * sw + 2.0 * sh + 4.0 * qa;
    // Clockwise sequence starting at the start of the top edge: top, TR arc, right, BR arc,
    // bottom, BL arc, left, TL arc. Arc mid offsets per corner (TL, TR, BR, BL):
    const double mids[4] = {perim - qa * 0.5, sw + qa * 0.5, sw + qa + sh + qa * 0.5,
                            2.0 * sw + 2.0 * qa + sh + qa * 0.5};
    const double t0 = mids[minSumCornerIndex(p)];
    std::vector<Vec2> out;
    out.reserve(static_cast<std::size_t>(n));
    for (int k = 0; k < n; ++k) {
        double t = t0 + perim * k / n;
        if (t >= perim) t -= perim;
        double u = 0, v = 0;
        auto arc = [&](double cu, double cv, double a0, double tt) {
            double sn, cs;
            sinCos(a0 + tt / r, sn, cs);
            u = cu + r * cs;
            v = cv + r * sn;
        };
        if (t < sw) {
            u = r + t;
            v = 0;
        } else if ((t -= sw) < qa) {
            arc(p.W - r, r, -kPi * 0.5, t);
        } else if ((t -= qa) < sh) {
            u = p.W;
            v = r + t;
        } else if ((t -= sh) < qa) {
            arc(p.W - r, p.H - r, 0.0, t);
        } else if ((t -= qa) < sw) {
            u = p.W - r - t;
            v = p.H;
        } else if ((t -= sw) < qa) {
            arc(r, p.H - r, kPi * 0.5, t);
        } else if ((t -= qa) < sh) {
            u = 0;
            v = p.H - r - t;
        } else {
            t -= sh;
            arc(r, r, kPi, std::min(t, qa));
        }
        out.push_back(p.toScan(u, v));
    }
    return out;
}

std::optional<std::string> buildScene(const Options& opt, Scene& sc) {
    sc = Scene{};
    sc.opt = opt;
    sc.s = opt.dpi / 25.4;
    sc.width = static_cast<int>(roundHalfUp(opt.scanWmm * sc.s));
    sc.height = static_cast<int>(roundHalfUp(opt.scanHmm * sc.s));
    if (sc.width < 16 || sc.height < 16) return "scan too small";
    const auto& types = filmTypes();
    const auto& frames = frameDefs();

    // Print attributes.
    Pcg32 rngPrints(opt.seed, kStreamPrints);
    std::vector<Print> prints(static_cast<std::size_t>(opt.count));
    for (int i = 0; i < opt.count; ++i) {
        Print& p = prints[static_cast<std::size_t>(i)];
        p.film = opt.types[static_cast<std::size_t>(rngPrints.index(static_cast<int>(opt.types.size())))];
        p.frame = opt.frames[static_cast<std::size_t>(rngPrints.index(static_cast<int>(opt.frames.size())))];
        const Content content =
            opt.contents[static_cast<std::size_t>(rngPrints.index(static_cast<int>(opt.contents.size())))];
        unsigned f = 0;
        for (unsigned bit : {kHandwriting, kStain, kGlare, kShadow}) {
            const bool hit = rngPrints.chance(opt.featureProb);
            if ((opt.features & bit) && hit) f |= bit;
        }
        p.features = f;
        const FilmType& ft = types[static_cast<std::size_t>(p.film)];
        p.frameColor = frames[static_cast<std::size_t>(p.frame)].color;
        p.W = ft.cardW;
        p.H = ft.cardH;
        p.r = ft.radius;
        p.ellipse = ft.inner == InnerKind::Ellipse;
        p.ix0 = ft.imgX;
        p.iy0 = ft.imgY;
        p.ix1 = ft.imgX + ft.imgW;
        p.iy1 = ft.imgY + ft.imgH;
        p.ccx = ft.imgX + ft.imgW * 0.5;
        p.ccy = ft.imgY + ft.imgH * 0.5;
        p.cr = ft.imgW * 0.5;
        p.s = sc.s;
        Pcg32 rngApp(opt.seed, kStreamAppearance + static_cast<std::uint64_t>(i));
        makeAppearance(p, content, rngApp);
    }

    Pcg32 rngLayout(opt.seed, kStreamLayout);
    if (!layoutPrints(opt, prints, rngLayout)) {
        std::string list;
        for (const Print& p : prints)
            list += std::string(list.empty() ? "" : ",") + types[static_cast<std::size_t>(p.film)].id;
        char buf[256];
        std::snprintf(buf, sizeof buf, "cannot place %d prints on a %gx%g mm scan with >= %g mm gaps",
                      opt.count, opt.scanWmm, opt.scanHmm, kGapMm);
        return std::string(buf) + " (" + list +
               "; try fewer prints, other --types, --scan-mm or --quarter-turns 0)";
    }
    for (Print& p : prints)
        finalisePlacement(p);

    // Reading order: rows by centre y (tolerance: half the row anchor's height), then x.
    std::vector<int> order(prints.size());
    for (std::size_t i = 0; i < order.size(); ++i)
        order[i] = static_cast<int>(i);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        return prints[static_cast<std::size_t>(a)].cy < prints[static_cast<std::size_t>(b)].cy;
    });
    std::vector<std::vector<int>> rows;
    double anchorY = 0, anchorH = 0;
    for (int i : order) {
        const Print& p = prints[static_cast<std::size_t>(i)];
        if (rows.empty() || p.cy - anchorY > 0.5 * anchorH) {
            rows.emplace_back();
            anchorY = p.cy;
            anchorH = p.aabbH * p.s;
        }
        rows.back().push_back(i);
    }
    std::vector<int> genToSorted(prints.size());
    for (auto& row : rows) {
        std::stable_sort(row.begin(), row.end(), [&](int a, int b) {
            return prints[static_cast<std::size_t>(a)].cx < prints[static_cast<std::size_t>(b)].cx;
        });
        for (int i : row) {
            genToSorted[static_cast<std::size_t>(i)] = static_cast<int>(sc.prints.size());
            sc.prints.push_back(prints[static_cast<std::size_t>(i)]);
        }
    }
    if (opt.touching && opt.count >= 2) {
        sc.touchingA = std::min(genToSorted[0], genToSorted[1]);
        sc.touchingB = std::max(genToSorted[0], genToSorted[1]);
    }

    // Background.
    Pcg32 rngBg(opt.seed, kStreamBackground);
    sc.bgLevel = opt.background;
    sc.bgGradient = opt.bgGradient;
    sinCos(rngBg.uniform(0.0, kTwoPi), sc.gy, sc.gx);
    {
        const double W = sc.width, H = sc.height;
        const double pr[4] = {0.0, W * sc.gx, H * sc.gy, W * sc.gx + H * sc.gy};
        const double lo = std::min({pr[0], pr[1], pr[2], pr[3]}), hi = std::max({pr[0], pr[1], pr[2], pr[3]});
        sc.gMin = lo;
        sc.gInv = 1.0 / (hi - lo);
        sc.illumCx = W * rngBg.uniform(0.35, 0.65);
        sc.illumCy = H * rngBg.uniform(0.35, 0.65);
        sc.illumInvR2 = 1.0 / (W * W * 0.25 + H * H * 0.25);
        sc.illumAmp = 0.12;
    }
    sc.noiseKey = splitmix64(opt.seed ^ 0x5EEDF00D5EEDF00Dull);

    // Dust (scan px).
    if (opt.features & kDust) {
        Pcg32 r(opt.seed, kStreamDust);
        const double areaCm2 = opt.scanWmm * opt.scanHmm / 100.0;
        const int n = static_cast<int>(roundHalfUp(opt.dustDensity * areaCm2 * r.uniform(0.8, 1.2)));
        for (int i = 0; i < n; ++i) {
            Dust d{};
            d.x = r.uniform(0.0, sc.width);
            d.y = r.uniform(0.0, sc.height);
            d.r = r.uniform(0.5, 1.5);
            d.level = r.uniform(0.55, 0.95);
            sc.dust.push_back(d);
        }
        std::stable_sort(sc.dust.begin(), sc.dust.end(),
                         [](const Dust& a, const Dust& b) { return a.y < b.y; });
        for (Print& p : sc.prints) {
            for (const Dust& d : sc.dust) {
                const double u = p.a11 * d.x + p.a12 * d.y + p.a13, v = p.a21 * d.x + p.a22 * d.y + p.a23;
                if (sdCard(p, u, v) <= 0.0) ++p.dustSpecks;
            }
        }
    }
    // Scratches (scan px, rendered on the background only).
    if (opt.features & kScratch) {
        Pcg32 r(opt.seed, kStreamScratch);
        const int n = 1 + r.index(3);
        for (int i = 0; i < n; ++i) {
            Scratch sct{};
            const double len = r.uniform(15.0, 80.0) * sc.s;
            double sn, cs;
            sinCos(r.uniform(0.0, kPi), sn, cs);
            const double mx = r.uniform(0.0, sc.width), my = r.uniform(0.0, sc.height);
            sct.ax = mx - cs * len * 0.5;
            sct.ay = my - sn * len * 0.5;
            sct.bx = mx + cs * len * 0.5;
            sct.by = my + sn * len * 0.5;
            sct.halfWidth = r.uniform(0.4, 1.0);
            sct.level = r.uniform(0.25, 0.55);
            sct.y0 = std::min(sct.ay, sct.by) - sct.halfWidth - 2.0;
            sct.y1 = std::max(sct.ay, sct.by) + sct.halfWidth + 2.0;
            sc.scratches.push_back(sct);
        }
    }
    return std::nullopt;
}

nlohmann::ordered_json groundTruth(const Scene& sc) {
    using oj = nlohmann::ordered_json;
    const Options& o = sc.opt;
    oj gt;
    gt["schema"] = "fk.fixture-gt/1";
    gt["seed"] = o.seed;
    gt["dpi"] = o.dpi;
    gt["width"] = sc.width;
    gt["height"] = sc.height;
    gt["bits"] = o.bits;
    gt["channels"] = o.channels;
    gt["compression"] = compressionName(o.compression);
    gt["predictor"] = o.predictor;
    gt["layout"] = layoutName(o.layout);
    if (o.layout == Layout::Tiles) {
        gt["rowsPerStrip"] = nullptr;
        gt["tileSize"] = o.tile;
    } else {
        gt["rowsPerStrip"] =
            o.layout == Layout::SingleStrip ? sc.height : std::min(o.rowsPerStrip, sc.height);
    }
    gt["bigtiff"] = o.bigtiff;
    gt["icc"] = o.iccSrgb ? "srgb" : "none";
    gt["photometric"] = photometricName(o.photometric());
    gt["scanMm"] = arr2(o.scanWmm, o.scanHmm);
    gt["background"] = {{"level", sc.bgLevel},
                        {"gradient", sc.bgGradient},
                        {"noise", o.noise},
                        {"gradientDirection", arr2(sc.gx, sc.gy)}};
    gt["features"] = featureNames(o.features & (kDust | kScratch));
    gt["touching"] = sc.touchingA >= 0 ? oj::array({sc.touchingA + 1, sc.touchingB + 1}) : oj(nullptr);

    oj prints = oj::array();
    const auto& types = filmTypes();
    const auto& frames = frameDefs();
    for (std::size_t i = 0; i < sc.prints.size(); ++i) {
        const Print& p = sc.prints[i];
        oj jp;
        jp["index"] = static_cast<int>(i) + 1;
        jp["filmType"] = types[static_cast<std::size_t>(p.film)].id;
        jp["frame"] = frames[static_cast<std::size_t>(p.frame)].id;
        jp["frameColor"] = oj::array({p.frameColor.r, p.frameColor.g, p.frameColor.b});
        jp["content"] = contentName(p.content.kind);
        unsigned f = p.features;
        if (p.dustSpecks > 0) f |= kDust;
        jp["features"] = featureNames(f);
        jp["centerPx"] = arr2(p.cx, p.cy);
        jp["quarterTurns"] = p.quarterTurns;
        jp["tiltDeg"] = p.tiltDeg;
        jp["rotationDeg"] = p.rotationDeg();
        jp["upright"] = (4 - p.quarterTurns) % 4;
        jp["cardSizeMm"] = arr2(p.W, p.H);
        oj q = oj::array();
        for (const Vec2& v : outerQuad(p))
            q.push_back(arr2(v.x, v.y));
        jp["outerQuad"] = q;
        jp["outerCornerRadiusPx"] = p.r * p.s;
        oj b = oj::array();
        for (const Vec2& v : outerBoundary(p, 256))
            b.push_back(arr2(v.x, v.y));
        jp["outerBoundary"] = b;
        oj inner;
        if (p.ellipse) {
            const Vec2 c = p.toScan(p.ccx, p.ccy);
            inner["kind"] = "ellipse";
            inner["center"] = arr2(c.x, c.y);
            inner["rx"] = p.cr * p.s;
            inner["ry"] = p.cr * p.s;
            inner["angle"] = degToRad(p.rotationDeg());
        } else {
            inner["kind"] = "quad";
            oj pts = oj::array();
            for (const Vec2& v : innerQuad(p))
                pts.push_back(arr2(v.x, v.y));
            inner["points"] = pts;
        }
        jp["inner"] = inner;
        jp["dustSpecks"] = p.dustSpecks;
        prints.push_back(jp);
    }
    gt["prints"] = prints;
    return gt;
}

} // namespace fkfx
