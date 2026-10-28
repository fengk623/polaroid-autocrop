// fkfixture — deterministic math, hashing and RNG.
//
// Everything the renderer computes per pixel uses only + - * /, sqrt and floor, which are exactly
// specified by IEEE 754, so output is bit-identical on every platform (FMA contraction is disabled
// in CMake). sin/cos are implemented here with range reduction + Taylor polynomials instead of
// relying on the C library, whose last-bit accuracy differs between glibc, MSVC and Apple libm.
#pragma once

#include <cmath>
#include <cstdint>

namespace fkfx {

inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kTwoPi = 6.28318530717958647692;
inline constexpr double kSqrt3 = 1.7320508075688772935;

// ---------------------------------------------------------------------------------------------
// Hashing and RNG
// ---------------------------------------------------------------------------------------------
inline std::uint64_t splitmix64(std::uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

inline std::uint64_t hash2(std::uint64_t key, std::int64_t a, std::int64_t b) {
    std::uint64_t h = splitmix64(key ^ (static_cast<std::uint64_t>(a) * 0xD6E8FEB86659FD93ull));
    return splitmix64(h ^ (static_cast<std::uint64_t>(b) * 0xA0761D6478BD642Full));
}

// Uniform [0,1) from the top 53 bits.
inline double hashToUnit(std::uint64_t h) {
    return static_cast<double>(h >> 11) * 0x1.0p-53;
}

// Approximately standard-normal value (mean 0, sigma 1) from 64 hash bits: Irwin-Hall sum of four
// 16-bit uniforms, rescaled. Range is bounded (|z| <= 2*sqrt(3)), which suits sensor noise.
inline double hashToGauss(std::uint64_t h) {
    const double s = static_cast<double>(h & 0xFFFFu) + static_cast<double>((h >> 16) & 0xFFFFu) +
                     static_cast<double>((h >> 32) & 0xFFFFu) + static_cast<double>(h >> 48);
    return ((s + 2.0) * (1.0 / 65536.0) - 2.0) * kSqrt3;
}

// PCG32 (O'Neill, pcg-random.org, "pcg32_random_r"); explicit streams keep independent decisions
// independent (changing one feature does not shift the layout).
class Pcg32 {
  public:
    Pcg32(std::uint64_t seed, std::uint64_t stream) : inc_((stream << 1u) | 1u) {
        next();
        state_ += splitmix64(seed);
        next();
    }
    std::uint32_t next() {
        const std::uint64_t old = state_;
        state_ = old * 6364136223846793005ull + inc_;
        const auto xorshifted = static_cast<std::uint32_t>(((old >> 18u) ^ old) >> 27u);
        const auto rot = static_cast<std::uint32_t>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
    }
    std::uint64_t next64() { return (static_cast<std::uint64_t>(next()) << 32) | next(); }
    double uniform() { return static_cast<double>(next()) * (1.0 / 4294967296.0); }
    double uniform(double lo, double hi) { return lo + (hi - lo) * uniform(); }
    int index(int n) {
        const int i = static_cast<int>(uniform() * n);
        return i < n ? i : n - 1;
    }
    bool chance(double p) { return uniform() < p; }

  private:
    std::uint64_t state_ = 0;
    std::uint64_t inc_;
};

// ---------------------------------------------------------------------------------------------
// Deterministic trigonometry
// ---------------------------------------------------------------------------------------------
inline void sinCos(double x, double& s, double& c) {
    // Cody-Waite reduction by pi/2 (fdlibm constants), then Taylor series on |r| <= pi/4
    // (truncation error < 1e-19, i.e. below double rounding).
    constexpr double kPio2Hi = 1.57079632673412561417e+00;
    constexpr double kPio2Lo = 6.07710050650619224932e-11;
    constexpr double kTwoOverPi = 6.36619772367581382433e-01;
    const double k = std::floor(x * kTwoOverPi + 0.5);
    const double r = (x - k * kPio2Hi) - k * kPio2Lo;
    const double r2 = r * r;
    // sin r = r (1 - r2/3! (1 - r2/(4*5) (1 - r2/(6*7) ...))) evaluated in Horner form.
    double ps = 1.0 - r2 / (18.0 * 19.0);
    ps = 1.0 - r2 / (16.0 * 17.0) * ps;
    ps = 1.0 - r2 / (14.0 * 15.0) * ps;
    ps = 1.0 - r2 / (12.0 * 13.0) * ps;
    ps = 1.0 - r2 / (10.0 * 11.0) * ps;
    ps = 1.0 - r2 / (8.0 * 9.0) * ps;
    ps = 1.0 - r2 / (6.0 * 7.0) * ps;
    ps = 1.0 - r2 / (4.0 * 5.0) * ps;
    ps = 1.0 - r2 / (2.0 * 3.0) * ps;
    const double sr = r * ps;
    double pc = 1.0 - r2 / (19.0 * 20.0);
    pc = 1.0 - r2 / (17.0 * 18.0) * pc;
    pc = 1.0 - r2 / (15.0 * 16.0) * pc;
    pc = 1.0 - r2 / (13.0 * 14.0) * pc;
    pc = 1.0 - r2 / (11.0 * 12.0) * pc;
    pc = 1.0 - r2 / (9.0 * 10.0) * pc;
    pc = 1.0 - r2 / (7.0 * 8.0) * pc;
    pc = 1.0 - r2 / (5.0 * 6.0) * pc;
    pc = 1.0 - r2 / (3.0 * 4.0) * pc;
    const double cr = 1.0 - r2 / 2.0 * pc;
    const auto ki = static_cast<std::int64_t>(k);
    switch (((ki % 4) + 4) % 4) {
    case 0:
        s = sr;
        c = cr;
        break;
    case 1:
        s = cr;
        c = -sr;
        break;
    case 2:
        s = -sr;
        c = -cr;
        break;
    default:
        s = -cr;
        c = sr;
        break;
    }
}
inline double dsin(double x) {
    double s, c;
    sinCos(x, s, c);
    return s;
}
inline double dcos(double x) {
    double s, c;
    sinCos(x, s, c);
    return c;
}
inline double degToRad(double d) {
    return d * (kPi / 180.0);
}

// ---------------------------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------------------------
inline double clamp01(double v) {
    return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
}
inline double clampd(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
inline double lerp(double a, double b, double t) {
    return a + (b - a) * t;
}
inline double smooth01(double t) {
    t = clamp01(t);
    return t * t * (3.0 - 2.0 * t);
}
// Compact-support bump: 1 at 0, 0 for |x| >= 1, C1-smooth.
inline double bump(double x) {
    const double t = 1.0 - x * x;
    return t <= 0.0 ? 0.0 : t * t;
}
// Triangle wave with period 1, range [0,1].
inline double triWave(double t) {
    const double f = t - std::floor(t);
    return f < 0.5 ? 2.0 * f : 2.0 - 2.0 * f;
}

// Value noise on the integer lattice, smoothstep interpolation, range [0,1].
inline double valueNoise(std::uint64_t key, double x, double y) {
    const double fx = std::floor(x), fy = std::floor(y);
    const auto ix = static_cast<std::int64_t>(fx);
    const auto iy = static_cast<std::int64_t>(fy);
    double tx = x - fx, ty = y - fy;
    tx = tx * tx * (3.0 - 2.0 * tx);
    ty = ty * ty * (3.0 - 2.0 * ty);
    const double a = hashToUnit(hash2(key, ix, iy));
    const double b = hashToUnit(hash2(key, ix + 1, iy));
    const double c = hashToUnit(hash2(key, ix, iy + 1));
    const double d = hashToUnit(hash2(key, ix + 1, iy + 1));
    return lerp(lerp(a, b, tx), lerp(c, d, tx), ty);
}

// Fractal sum of value noise, normalised to [0,1].
inline double fbm(std::uint64_t key, double x, double y, int octaves) {
    double sum = 0.0, amp = 1.0, norm = 0.0;
    for (int o = 0; o < octaves; ++o) {
        sum += amp * valueNoise(key + static_cast<std::uint64_t>(o) * 0x632BE59BD9B4E019ull, x, y);
        norm += amp;
        amp *= 0.5;
        x = x * 2.03 + 17.1;
        y = y * 2.03 - 9.7;
    }
    return sum / norm;
}

struct Rgb {
    double r = 0, g = 0, b = 0;
};
inline Rgb mix(const Rgb& a, const Rgb& b, double t) {
    return {lerp(a.r, b.r, t), lerp(a.g, b.g, t), lerp(a.b, b.b, t)};
}
inline Rgb scale(const Rgb& a, double k) {
    return {a.r * k, a.g * k, a.b * k};
}
inline Rgb add(const Rgb& a, double k) {
    return {a.r + k, a.g + k, a.b + k};
}

} // namespace fkfx
