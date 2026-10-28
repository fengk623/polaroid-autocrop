// fkfixture — per-row scene renderer (pure function of scene + row: thread-safe, deterministic).
#include "scene.hpp"

#include <algorithm>

namespace fkfx {
namespace {

constexpr double kAaPx = 1.5; // supersample pixels whose centre is this close to a boundary
constexpr int kMaxCand = 16;
constexpr std::uint64_t kKeyGold = 0x6A09E667F3BCC909ull;
constexpr std::uint64_t kKeySparkle = 0xBB67AE8584CAA73Bull;
constexpr std::uint64_t kKeyFibreFine = 0x3C6EF372FE94F82Bull;
constexpr std::uint64_t kKeyFibreCoarse = 0xA54FF53A5F1D36F1ull;

double segDist2(const Segment& sg, double x, double y) {
    const double dx = sg.bx - sg.ax, dy = sg.by - sg.ay;
    const double l2 = dx * dx + dy * dy;
    double t = l2 > 0.0 ? ((x - sg.ax) * dx + (y - sg.ay) * dy) / l2 : 0.0;
    t = clamp01(t);
    const double ex = sg.ax + t * dx - x, ey = sg.ay + t * dy - y;
    return ex * ex + ey * ey;
}

Rgb contentColour(const Print& p, double u, double v) {
    const ContentParams& c = p.content;
    const double x = u - p.ix0, y = v - p.iy0;
    if (c.kind != Content::Normal) {
        const double n = fbm(c.key, x / c.noiseScale, y / c.noiseScale, 3);
        const double val = lerp(c.lo, c.hi, n);
        return {clampd(val * (1.0 + c.tint.r), c.lo, c.hi), clampd(val * (1.0 + c.tint.g), c.lo, c.hi),
                clampd(val * (1.0 + c.tint.b), c.lo, c.hi)};
    }
    const double iw = p.ix1 - p.ix0, ih = p.iy1 - p.iy0;
    const double a = x / iw, b = y / ih;
    const double hz =
        clampd(c.horizon + c.horizonSlope * (a - 0.5) + c.horizonWave * (triWave(a * c.horizonFreq) - 0.5),
               0.05, 0.95);
    Rgb col = b < hz ? mix(c.sky0, c.sky1, clamp01(b / hz))
                     : mix(c.ground0, c.ground1, clamp01((b - hz) / (1.0 - hz)));
    col = add(col, (fbm(c.key, x / c.noiseScale, y / c.noiseScale, 3) - 0.5) * 2.0 * c.noiseAmp);
    if (c.stripes) {
        const double cov = smooth01((x - c.stripeX0) / 0.3) * smooth01((c.stripeX1 - x) / 0.3) *
                           smooth01((y - c.stripeY0) / 0.3) * smooth01((c.stripeY1 - y) / 0.3);
        if (cov > 0.0) {
            const double w = triWave((x * c.stripeCos + y * c.stripeSin) / c.stripePeriod);
            col = mix(col, mix(c.stripeA, c.stripeB, smooth01((w - 0.5) * 4.0 + 0.5)), cov);
        }
    }
    for (const Disc& d : c.discs) {
        const double dx = x - d.cx, dy = y - d.cy;
        const double rr = d.r + d.soft;
        const double d2 = dx * dx + dy * dy;
        if (d2 >= rr * rr) continue;
        const double cov = smooth01((d.r - std::sqrt(d2)) / d.soft + 0.5);
        const double k = 1.0 - d.shade * (dx + dy) / (2.0 * d.r);
        col = mix(col, scale(d.col, k), cov);
    }
    return {clampd(col.r, 0.02, 0.97), clampd(col.g, 0.02, 0.97), clampd(col.b, 0.02, 0.97)};
}

Rgb frameColour(const Print& p, double u, double v, std::int64_t pixX, std::int64_t pixY) {
    Rgb c = p.frameColor;
    const char* id = frameDefs()[static_cast<std::size_t>(p.frame)].id;
    if (id[0] == 'a') { // aged: gentle yellowing toward one edge
        const double t = clamp01(p.ageDirU * u + p.ageDirV * v + p.ageOff);
        c = mix(c, Rgb{0.80, 0.68, 0.42}, 0.5 * t * t);
    } else if (id[0] == 'g') { // gold: brushed metallic streaks + sparkle
        const double n = valueNoise(p.key ^ kKeyGold, u * 4.0, v * 0.25);
        c = scale(c, 0.92 + 0.16 * n);
        c = add(c, 0.012 * hashToGauss(hash2(p.key ^ kKeySparkle, pixX, pixY)));
    }
    // Paper fibre (card-space texture; with sensor noise ~0.01 total).
    const double fib = (valueNoise(p.key ^ kKeyFibreFine, u * 4.0, v * 4.0) - 0.5) * 0.045 +
                       (valueNoise(p.key ^ kKeyFibreCoarse, u * 0.8, v * 0.8) - 0.5) * 0.025;
    c = add(c, fib);
    if (p.features & kStain) {
        for (const Stain& st : p.stains) {
            const double du = u - st.cu, dv = v - st.cv, lim = 1.4 * st.radius;
            if (std::fabs(du) > lim || std::fabs(dv) > lim) continue;
            const double d = std::sqrt(du * du + dv * dv) / st.radius +
                             0.35 * (valueNoise(st.key, u / st.radius * 1.5, v / st.radius * 1.5) - 0.5);
            const double a =
                st.alpha * (1.0 - smooth01((d - 0.75) / 0.25)) + st.ring * bump((d - 0.93) / 0.06);
            if (a <= 0.0) continue;
            c = {c.r * (1.0 - a * (1.0 - st.tint.r)), c.g * (1.0 - a * (1.0 - st.tint.g)),
                 c.b * (1.0 - a * (1.0 - st.tint.b))};
        }
    }
    if ((p.features & kHandwriting) && !p.hw.segs.empty()) {
        const Handwriting& hw = p.hw;
        const double gu = (u - hw.u0) / hw.cell, gv = (v - hw.v0) / hw.cell;
        if (gu >= 0.0 && gv >= 0.0 && gu < hw.gw && gv < hw.gh) {
            const std::size_t ci =
                static_cast<std::size_t>(gv) * static_cast<std::size_t>(hw.gw) + static_cast<std::size_t>(gu);
            double best = 1e300;
            for (std::uint32_t k = hw.cellStart[ci]; k < hw.cellStart[ci + 1]; ++k)
                best = std::min(best, segDist2(hw.segs[hw.cellItems[k]], u, v));
            if (best < 1e299) {
                const double cov = clamp01((hw.halfWidth - std::sqrt(best)) * p.s + 0.5);
                if (cov > 0.0) c = mix(c, hw.ink, cov * hw.opacity);
            }
        }
    }
    return c;
}

Rgb printColour(const Print& p, double u, double v, std::int64_t pixX, std::int64_t pixY) {
    Rgb c = sdInner(p, u, v) <= 0.0 ? contentColour(p, u, v) : frameColour(p, u, v, pixX, pixY);
    if (p.features & kGlare) {
        const Glare& g = p.glare;
        const double du = u - g.cu, dv = v - g.cv;
        const double along = du * g.du + dv * g.dv, across = -du * g.dv + dv * g.du;
        const double k = g.amount * bump(across / g.halfWidth) * bump(along / g.halfLength);
        if (k > 0.0) c = {c.r + (1.0 - c.r) * k, c.g + (1.0 - c.g) * k, c.b + (1.0 - c.b) * k};
    }
    return c;
}

struct RowCtx {
    const Scene* sc;
    const Print* cand[kMaxCand];
    int nc = 0;
    const Scratch* scratch[16];
    int ns = 0;
};

Rgb sampleScene(const RowCtx& ctx, double px, double py, std::int64_t pixX, std::int64_t pixY) {
    for (int i = 0; i < ctx.nc; ++i) {
        const Print& p = *ctx.cand[i];
        const double u = p.a11 * px + p.a12 * py + p.a13, v = p.a21 * px + p.a22 * py + p.a23;
        if (sdCard(p, u, v) <= 0.0) return printColour(p, u, v, pixX, pixY);
    }
    const Scene& sc = *ctx.sc;
    const double t = (px * sc.gx + py * sc.gy - sc.gMin) * sc.gInv;
    double lvl = sc.bgLevel + sc.bgGradient * (t - 0.5);
    const double ix = px - sc.illumCx, iy = py - sc.illumCy;
    lvl *= 1.0 - sc.illumAmp * (ix * ix + iy * iy) * sc.illumInvR2;
    for (int i = 0; i < ctx.nc; ++i) {
        const Print& p = *ctx.cand[i];
        if (!(p.features & kShadow)) continue;
        const double u = p.a11 * px + p.a12 * py + p.a13, v = p.a21 * px + p.a22 * py + p.a23;
        const double d = sdCard(p, u - p.shadow.du, v - p.shadow.dv);
        double amt = 0.0;
        if (d <= 0.0) {
            amt = 1.0;
        } else if (d < p.shadow.falloff) {
            const double k = 1.0 - d / p.shadow.falloff;
            amt = k * k;
        }
        lvl *= 1.0 - p.shadow.strength * amt;
    }
    for (int i = 0; i < ctx.ns; ++i) {
        const Scratch& s = *ctx.scratch[i];
        const double dx = s.bx - s.ax, dy = s.by - s.ay;
        const double tt = clamp01(((px - s.ax) * dx + (py - s.ay) * dy) / (dx * dx + dy * dy));
        const double ex = s.ax + tt * dx - px, ey = s.ay + tt * dy - py;
        const double cov = clamp01(s.halfWidth + 0.5 - std::sqrt(ex * ex + ey * ey));
        if (cov <= 0.0) continue;
        const double e = 2.0 * tt - 1.0, e2 = e * e;
        lvl = lerp(lvl, s.level, cov * (1.0 - e2 * e2));
    }
    return {lvl, lvl, lvl};
}

} // namespace

void renderRow(const Scene& sc, int y, float* out) {
    RowCtx ctx;
    ctx.sc = &sc;
    const double y0 = y, y1 = y + 1.0;
    const Print* active[kMaxCand];
    int na = 0;
    for (const Print& p : sc.prints)
        if (p.by0 <= y1 && p.by1 >= y0 && na < kMaxCand) active[na++] = &p;
    for (const Scratch& s : sc.scratches)
        if (s.y0 <= y1 && s.y1 >= y0 && ctx.ns < 16) ctx.scratch[ctx.ns++] = &s;

    const double py = y + 0.5;
    for (int x = 0; x < sc.width; ++x) {
        const double px = x + 0.5;
        ctx.nc = 0;
        bool ss = false;
        for (int i = 0; i < na; ++i) {
            const Print& p = *active[i];
            if (p.bx0 > x + 1.0 || p.bx1 < x) continue;
            ctx.cand[ctx.nc++] = &p;
            const double u = p.a11 * px + p.a12 * py + p.a13, v = p.a21 * px + p.a22 * py + p.a23;
            const double dc = sdCard(p, u, v) * p.s;
            if (std::fabs(dc) < kAaPx) {
                ss = true;
            } else if (dc < 0.0 && std::fabs(sdInner(p, u, v) * p.s) < kAaPx) {
                ss = true;
            }
        }
        Rgb c;
        if (ss) {
            double r = 0, g = 0, b = 0;
            for (int j = 0; j < 4; ++j) {
                for (int i = 0; i < 4; ++i) {
                    const Rgb s = sampleScene(ctx, x + (i + 0.5) * 0.25, y + (j + 0.5) * 0.25, x, y);
                    r += s.r;
                    g += s.g;
                    b += s.b;
                }
            }
            c = {r / 16.0, g / 16.0, b / 16.0};
        } else {
            c = sampleScene(ctx, px, py, x, y);
        }
        out[3 * x + 0] = static_cast<float>(c.r);
        out[3 * x + 1] = static_cast<float>(c.g);
        out[3 * x + 2] = static_cast<float>(c.b);
    }

    // Dust specks (on the glass: over background and prints), analytic coverage.
    if (!sc.dust.empty()) {
        auto it = std::lower_bound(sc.dust.begin(), sc.dust.end(), y0 - 3.0,
                                   [](const Dust& d, double v) { return d.y < v; });
        for (; it != sc.dust.end() && it->y <= y1 + 3.0; ++it) {
            const Dust& d = *it;
            const double dy = py - d.y;
            if (std::fabs(dy) > d.r + 1.0) continue;
            const int xa = std::max(0, static_cast<int>(std::floor(d.x - d.r - 1.0)));
            const int xb = std::min(sc.width - 1, static_cast<int>(std::floor(d.x + d.r + 1.0)));
            for (int x = xa; x <= xb; ++x) {
                const double dx = x + 0.5 - d.x;
                const double cov = clamp01(d.r + 0.5 - std::sqrt(dx * dx + dy * dy));
                if (cov <= 0.0) continue;
                for (int ch = 0; ch < 3; ++ch) {
                    float& o = out[3 * x + ch];
                    o = static_cast<float>(lerp(o, d.level, cov));
                }
            }
        }
    }

    // Sensor noise (per pixel, per channel, stateless hash).
    const double sigma = sc.opt.noise;
    if (sigma > 0.0) {
        for (int x = 0; x < sc.width; ++x) {
            const std::uint64_t h = hash2(sc.noiseKey, x, y);
            for (int ch = 0; ch < 3; ++ch) {
                float& o = out[3 * x + ch];
                o = static_cast<float>(o +
                                       sigma * hashToGauss(splitmix64(h + static_cast<std::uint64_t>(ch))));
            }
        }
    }
}

} // namespace fkfx
