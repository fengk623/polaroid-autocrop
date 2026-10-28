// fkfixture — scene description, layout and ground truth.
#pragma once

#include "fixture_math.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace fkfx {

// ---------------------------------------------------------------------------------------------
// Static catalogues
// ---------------------------------------------------------------------------------------------
enum class InnerKind { Quad, Ellipse };

struct FilmType {
    const char* id;
    double cardW, cardH; // mm, upright
    InnerKind inner;
    double imgX, imgY, imgW, imgH; // image rect (mm); for ellipses the circle's bounding square
    double radius;                 // card corner radius (mm)
};

struct FrameDef {
    const char* id;
    Rgb color;
};

enum class Content { Normal, Dark, Bright };

const std::vector<FilmType>& filmTypes();
const std::vector<FrameDef>& frameDefs();
const char* contentName(Content c);

// Print-level features (stored per print) and scene-level features.
enum Feature : unsigned {
    kHandwriting = 1u << 0,
    kStain = 1u << 1,
    kGlare = 1u << 2,
    kShadow = 1u << 3,
    kDust = 1u << 4,
    kScratch = 1u << 5,
};
inline constexpr unsigned kPrintFeatures = kHandwriting | kStain | kGlare | kShadow;
inline constexpr unsigned kAllFeatures = kPrintFeatures | kDust | kScratch;
std::vector<std::string> featureNames(unsigned mask);

// ---------------------------------------------------------------------------------------------
// Options
// ---------------------------------------------------------------------------------------------
enum class Compression { None, Lzw, Deflate, PackBits };
enum class Layout { Strips, SingleStrip, Tiles };
enum class Photometric { Rgb, MinIsBlack, MinIsWhite };

const char* compressionName(Compression c);
const char* layoutName(Layout l);
const char* photometricName(Photometric p);

struct Options {
    std::uint64_t seed = 1;
    int dpi = 300;
    int bits = 16;
    int channels = 3;
    Compression compression = Compression::Lzw;
    int predictor = 0;
    Layout layout = Layout::Strips;
    int rowsPerStrip = 16;
    int tile = 256;
    bool bigtiff = false;
    bool iccSrgb = true;
    bool minIsWhite = false;

    int count = 4;
    std::vector<int> types;  // indices into filmTypes()
    std::vector<int> frames; // indices into frameDefs()
    std::vector<Content> contents;
    unsigned features = 0;
    double featureProb = 1.0; // probability that a requested print-level feature is applied to a print
    double dustDensity = 1.0; // specks per cm^2
    double background = 0.06;
    double bgGradient = 0.02;
    double noise = 0.006;
    double scanWmm = 216.0, scanHmm = 297.0;
    double tiltMax = 3.0;
    int quarterTurns = 0; // 0..3 fixed, or -1 = any
    bool touching = false;
    int threads = 0; // 0 = hardware concurrency

    Photometric photometric() const {
        if (channels == 3) return Photometric::Rgb;
        return minIsWhite ? Photometric::MinIsWhite : Photometric::MinIsBlack;
    }
};

// ---------------------------------------------------------------------------------------------
// Scene
// ---------------------------------------------------------------------------------------------
struct Vec2 {
    double x = 0, y = 0;
};

struct Segment {
    double ax, ay, bx, by;
};

// Handwriting: polylines in card mm with a uniform grid for fast distance queries.
struct Handwriting {
    std::vector<Segment> segs;
    double halfWidth = 0.2;
    Rgb ink;
    double opacity = 0.9;
    double u0 = 0, v0 = 0, cell = 1.0; // grid origin and cell size (mm)
    int gw = 0, gh = 0;
    std::vector<std::uint32_t> cellStart; // gw*gh+1 offsets into cellItems
    std::vector<std::uint32_t> cellItems;
};

struct Stain {
    double cu, cv, radius, alpha, ring;
    Rgb tint;
    std::uint64_t key;
};

struct Glare {
    double cu, cv, du, dv, halfWidth, halfLength, amount;
};

struct Shadow {
    double du, dv, strength, falloff; // offset of the shadow caster in card mm
};

struct Disc {
    double cx, cy, r, soft; // image mm
    Rgb col;
    double shade;
};

struct ContentParams {
    Content kind = Content::Normal;
    // normal
    Rgb sky0, sky1, ground0, ground1;
    double horizon = 0.5, horizonSlope = 0.0, horizonWave = 0.0, horizonFreq = 1.0;
    std::vector<Disc> discs;
    bool stripes = false;
    double stripeX0 = 0, stripeY0 = 0, stripeX1 = 0, stripeY1 = 0; // image mm
    double stripeCos = 1, stripeSin = 0, stripePeriod = 3;
    Rgb stripeA, stripeB;
    double noiseScale = 8.0, noiseAmp = 0.12;
    // dark / bright: value range and tint
    double lo = 0, hi = 1;
    Rgb tint;
    std::uint64_t key = 0;
};

struct Print {
    int film = 0;  // index into filmTypes()
    int frame = 0; // index into frameDefs()
    Rgb frameColor;
    unsigned features = 0; // print-level features applied
    int dustSpecks = 0;    // dust specks whose centre lies on this print (ground truth only)

    // Geometry (mm)
    double W = 0, H = 0, r = 0;
    bool ellipse = false;
    double ix0 = 0, iy0 = 0, ix1 = 0, iy1 = 0; // image rect
    double ccx = 0, ccy = 0, cr = 0;           // circle

    // Placement
    int quarterTurns = 0;
    double tiltDeg = 0;
    double cx = 0, cy = 0; // centre (scan px)
    double s = 1;          // px per mm
    double cosp = 1, sinp = 0;
    // scan px -> card mm: u = a11 x + a12 y + a13, v = a21 x + a22 y + a23
    double a11 = 0, a12 = 0, a13 = 0, a21 = 0, a22 = 0, a23 = 0;
    // Culling box in scan px (covers card + shadow + anti-aliasing margin)
    double bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;
    // Rotated (sharp-cornered) card axis-aligned bounding box size in mm
    double aabbW = 0, aabbH = 0;

    // Appearance
    std::uint64_t key = 0;
    double ageDirU = 0, ageDirV = 1, ageOff = 0; // aged: t = clamp(ageDirU*u + ageDirV*v + ageOff)
    Handwriting hw;
    std::vector<Stain> stains;
    Glare glare{};
    Shadow shadow{};
    ContentParams content;

    double rotationDeg() const { return quarterTurns * 90.0 + tiltDeg; }
    Vec2 toScan(double u, double v) const; // card mm -> scan px
};

struct Dust {
    double x, y, r, level; // scan px
};

struct Scratch {
    double ax, ay, bx, by, halfWidth, level; // scan px
    double y0, y1;                           // row range
};

struct Scene {
    Options opt;
    int width = 0, height = 0;
    double s = 1; // px per mm
    // background
    double bgLevel = 0.06, bgGradient = 0.02, gx = 1, gy = 0, gMin = 0, gInv = 1;
    double illumAmp = 0.12, illumCx = 0, illumCy = 0, illumInvR2 = 1;
    std::uint64_t noiseKey = 0;
    std::vector<Print> prints; // in reading order
    std::vector<Dust> dust;    // sorted by y
    std::vector<Scratch> scratches;
    int touchingA = -1, touchingB = -1; // indices into prints
};

// Signed distances in card mm (negative inside): card rounded rectangle and image area.
inline double sdCard(const Print& p, double u, double v) {
    const double qx = std::fabs(u - p.W * 0.5) - (p.W * 0.5 - p.r);
    const double qy = std::fabs(v - p.H * 0.5) - (p.H * 0.5 - p.r);
    const double ox = qx > 0.0 ? qx : 0.0, oy = qy > 0.0 ? qy : 0.0;
    const double in = qx > qy ? qx : qy;
    return std::sqrt(ox * ox + oy * oy) + (in < 0.0 ? in : 0.0) - p.r;
}
inline double sdInner(const Print& p, double u, double v) {
    if (p.ellipse) {
        const double du = u - p.ccx, dv = v - p.ccy;
        return std::sqrt(du * du + dv * dv) - p.cr;
    }
    const double qx = std::fabs(u - (p.ix0 + p.ix1) * 0.5) - (p.ix1 - p.ix0) * 0.5;
    const double qy = std::fabs(v - (p.iy0 + p.iy1) * 0.5) - (p.iy1 - p.iy0) * 0.5;
    const double ox = qx > 0.0 ? qx : 0.0, oy = qy > 0.0 ? qy : 0.0;
    const double in = qx > qy ? qx : qy;
    return std::sqrt(ox * ox + oy * oy) + (in < 0.0 ? in : 0.0);
}

// Builds the scene (layout, features). Returns an error message on failure.
std::optional<std::string> buildScene(const Options& opt, Scene& scene);

// Ground truth JSON (schema fk.fixture-gt/1).
nlohmann::ordered_json groundTruth(const Scene& scene);

// Ground-truth geometry helpers (also used by the preview overlay).
std::vector<Vec2> outerQuad(const Print& p);
std::vector<Vec2> innerQuad(const Print& p);
std::vector<Vec2> outerBoundary(const Print& p, int n);

// Rendering: one row of clean RGB + noise into out[width*3] (values may be outside [0,1]).
void renderRow(const Scene& scene, int y, float* out);

} // namespace fkfx
