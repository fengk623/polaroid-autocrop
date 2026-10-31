// fkcli maskeval — how the composite mask looks at the image-area edge (UX-08, RFC-0007 §Natural blend).
//
// Usage: fkcli maskeval <scan.tif | gt.json>... [--scans DIR] [--json report.json] [--crops DIR]
// --crops writes review sheets per print and grade (warm, strong): the middle of each image-area side, 4×
// enlarged, columns scan · hard edge · feather 0.05 mm · preserve edge 0.02 mm. They show scan content: keep
// them local. With real-scan ground truth (fk.realgt/1, docs/v1/REAL-SCAN-GT.md; scans found by SHA-256 in
// --scans), the true window edge tells frame pixels from picture pixels; without it, colour does
// (frame-coloured vs not), which also counts white picture content near the edge as "frame".
//
// For every print detected in the given scans (automatic count), the image area is "exported" as a pixel crop
// (the exact pixels a pixel-crop export writes), graded synthetically and composited back into the real frame
// with each mask configuration, at full resolution, exactly as the final export does (compositeToBuffer).
// Measured inside a band of 0.15 mm along the inner boundary (colour differences ΔE00, values taken as
// sRGB-encoded):
//   frame contamination  p99 ΔE00 composite vs scan over frame pixels inside the detected image area (GT:
//   outside
//                        the true window by > 0.5 px; colour: ΔE00 < 6 from the frame's median colour within
//                        0.05 mm
//                        + 1 px of the boundary): the grade showing on the real frame (a tinted line);
//   ungraded rim         mean width in px of picture pixels (GT: inside the window by > 0.5 px; colour: ΔE00
//   > 15
//                        from the frame) whose grade is visible (ΔE00 graded vs scan > 3) but which stay
//                        closer to the scan than to the grade: a double edge;
//   seam at 50 %         p99 ΔE00 after a 2×2 box reduction (what a 50 % view shows) between the composite
//   and the
//                        ideal (frame pixels keep the scan, picture pixels take the grade, edge pixels in
//                        proportion): how visible the edge treatment is at 50 %;
//   outside              max ΔE00 outside the image area (the frame is never changed: must be 0);
//   grade strength       median ΔE00 graded vs scan of the picture pixels in the band (what the mask has to
//   blend).
// Grades: none, exposure (+12 %), warm (R ×1.05, B ×0.93), contrast (×1.15 about mid-grey), fade (lifted
// blacks) are "normal"; strong (R ×1.25, G ×0.95, B ×0.75, contrast ×1.3) is reported but not expected to be
// seamless.
#include "cli_common.hpp"

#include "fk/color.hpp"
#include "fk/composite.hpp"
#include "fk/hash.hpp"
#include "fk/image_io.hpp"
#include "fk/util.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>

namespace fkcli {

namespace {

struct Lab {
    float v[3] = {0, 0, 0};
};

Lab toLab(double r, double g, double b) {
    const float rgb[3] = {static_cast<float>(r), static_cast<float>(g), static_cast<float>(b)};
    Lab l;
    fk::srgbToLab(rgb, l.v);
    return l;
}

/// CIEDE2000 of the core (tested against the published data, test_color DeltaE2000.*).
double dE00(const Lab& x, const Lab& y) {
    return fk::deltaE2000(x.v, y.v);
}

struct Grade {
    std::string name;
    bool normal = true;
    std::array<double, 3> gain{1, 1, 1};
    double contrast = 1, lift = 0;
};

const std::vector<Grade> kGrades = {{"none", true, {1, 1, 1}, 1, 0},
                                    {"exposure", true, {1.12, 1.12, 1.12}, 1, 0},
                                    {"warm", true, {1.05, 1.0, 0.93}, 1, 0},
                                    {"contrast", true, {1, 1, 1}, 1.15, 0},
                                    {"fade", true, {0.92, 0.92, 0.92}, 1, 0.06},
                                    {"strong", false, {1.25, 0.95, 0.75}, 1.3, 0}};

double applyGrade(const Grade& g, int c, double v) {
    v = v * g.gain[static_cast<size_t>(std::min(c, 2))] + g.lift;
    v = 0.5 + (v - 0.5) * g.contrast;
    return std::clamp(v, 0.0, 1.0);
}

struct Config {
    std::string name;
    fk::MaskMode mode = fk::MaskMode::hard_edge;
    double mm = 0;
};

std::string mmText(double mm) {
    char b[32];
    std::snprintf(b, sizeof b, "%.3g", mm);
    return b;
}

std::vector<Config> configs() {
    std::vector<Config> v{{"hard 0", fk::MaskMode::hard_edge, 0}};
    for (double mm : {0.02, 0.03, 0.04, 0.05, 0.06, 0.08})
        v.push_back({"feather " + mmText(mm) + " mm", fk::MaskMode::feather, mm});
    for (double mm : {0.01, 0.015, 0.02, 0.025, 0.03})
        v.push_back({"preserve " + mmText(mm) + " mm", fk::MaskMode::preserve_edge, mm});
    return v;
}

double percentile(std::vector<double> v, double p) {
    if (v.empty()) return 0;
    const size_t k = std::min(v.size() - 1, static_cast<size_t>(p * static_cast<double>(v.size())));
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k), v.end());
    return v[k];
}

Lab labAt(const fk::ImageBuffer& b, int64_t x, int64_t y) {
    if (b.channels() < 3) {
        const double v = b.samplef(x, y, 0);
        return toLab(v, v, v);
    }
    return toLab(b.samplef(x, y, 0), b.samplef(x, y, 1), b.samplef(x, y, 2));
}

struct Measure {
    double contamination = 0, contaminationMax = 0, rimPx = 0, seam50 = 0, outsideMax = 0, gradeStrength = 0;
};

} // namespace

int cmdMaskEval(const std::vector<std::string>& args) {
    const Args a = Args::parse(args);
    if (a.positional.empty()) {
        std::cerr << "usage: fkcli maskeval <scan.tif | gt.json>... [--scans DIR] [--json report.json]\n";
        return 2;
    }
    std::map<std::string, std::filesystem::path> bySha;
    if (a.has("scans"))
        for (const auto& e : std::filesystem::directory_iterator(fk::pathFromUtf8(a.get("scans")))) {
            const auto ext = e.path().extension().string();
            if (ext != ".tif" && ext != ".tiff" && ext != ".TIF" && ext != ".TIFF") continue;
            if (auto h = fk::sha256File(e.path())) bySha[*h] = e.path();
        }
    json report = {{"tool", "fkcli maskeval"}, {"scans", json::array()}};
    // summary: config → grade class → worst over prints
    std::map<std::string, std::map<std::string, Measure>> worst;
    const auto cfgs = configs();
    for (const auto& arg : a.positional) {
        std::string path = arg;
        std::vector<fk::Shape> gtInners, gtOuters;
        if (std::filesystem::path(arg).extension() == ".json") {
            std::ifstream f(fk::pathFromUtf8(arg));
            json gt;
            f >> gt;
            const auto it = bySha.find(gt.at("scan").at("sha256").get<std::string>());
            if (gt.value("schema", "") != "fk.realgt/1" || it == bySha.end()) {
                std::cerr << arg << ": not an fk.realgt/1 file, or its scan is not in --scans\n";
                return 1;
            }
            path = fk::pathToUtf8(it->second);
            for (const auto& p : gt.at("prints")) {
                auto quad = [](const json& j) {
                    fk::Quad q;
                    for (int k = 0; k < 4; ++k)
                        q.p[k] = {j.at("points").at(k).at(0).get<double>(),
                                  j.at("points").at(k).at(1).get<double>()};
                    return fk::Shape::fromQuad(q);
                };
                gtOuters.push_back(quad(p.at("outer")));
                gtInners.push_back(quad(p.at("inner")));
            }
        }
        auto src = fk::openTiff(fk::pathFromUtf8(path));
        if (!src) {
            std::cerr << path << ": " << src.error().message << "\n";
            return 1;
        }
        fk::CancelToken cancel;
        auto proxy = fk::buildProxy(**src, 3000, cancel, {});
        if (!proxy) return 1;
        fk::DetectParams params;
        params.expectedCount = 0;
        auto det = fk::detectPrints(**src, *proxy, params, cancel, {});
        if (!det) return 1;
        fk::ImageInfo info;
        auto scan = fk::readImageFile(fk::pathFromUtf8(path), &info);
        if (!scan) return 1;
        const double dpi = det->dpiUsed > 0 ? det->dpiUsed : (info.dpiX > 0 ? info.dpiX : 300.0);
        const double pxPerMm = dpi / 25.4;
        const fk::SizeI S{scan->width(), scan->height()};
        json sj = {{"scan", std::filesystem::path(path).filename().string()},
                   {"truth", gtInners.empty() ? "colour" : "ground truth"},
                   {"dpi", dpi},
                   {"bits", info.bitsPerSample},
                   {"prints", json::array()}};
        for (size_t ii = 0; ii < det->items.size(); ++ii) {
            const auto& it = det->items[ii];
            const std::string label = fk::itemLabel(static_cast<int>(ii) + 1);
            const fk::RectI Ri = fk::boundingRect(it.inner, S);
            const fk::RectI Ro = fk::boundingRect(it.outer, S);
            const fk::Shape* gtInner = nullptr;
            for (size_t g = 0; g < gtOuters.size(); ++g)
                if (fk::contains(it.outer, fk::centroid(gtOuters[g]))) gtInner = &gtInners[g];
            if (!gtOuters.empty() && !gtInner) continue; // not in the ground truth: not measured
            // the frame's colour: scan pixels between the image area and the paper edge, 0.5 mm from both
            std::vector<double> fl, fa, fb;
            for (int64_t y = Ro.y; y < Ro.y + Ro.h; y += 2)
                for (int64_t x = Ro.x; x < Ro.x + Ro.w; x += 2) {
                    const fk::Vec2 p{x + 0.5, y + 0.5};
                    if (fk::signedDistance(it.inner, p) < -0.5 * pxPerMm &&
                        fk::signedDistance(it.outer, p) > 0.5 * pxPerMm) {
                        const Lab l = labAt(*scan, x, y);
                        fl.push_back(l.v[0]), fa.push_back(l.v[1]), fb.push_back(l.v[2]);
                    }
                }
            const Lab frame{{static_cast<float>(percentile(fl, 0.5)), static_cast<float>(percentile(fa, 0.5)),
                             static_cast<float>(percentile(fb, 0.5))}};
            json pj = {{"label", label},
                       {"frameLab", {frame.v[0], frame.v[1], frame.v[2]}},
                       {"results", json::array()}};
            // how much of a scan pixel is picture (0 frame … 1 picture), and whether it is clearly one of
            // them
            auto pictureShare = [&](int64_t x, int64_t y, const Lab& s0) {
                if (gtInner)
                    return std::clamp(fk::signedDistance(*gtInner, {x + 0.5, y + 0.5}) + 0.5, 0.0, 1.0);
                return std::clamp((dE00(s0, frame) - 6) / 9, 0.0, 1.0);
            };
            const double band = 0.15 * pxPerMm, edgeZone = 0.05 * pxPerMm + 1.0;
            double perimeter = 0;
            if (it.inner.kind == fk::ShapeKind::quad)
                for (int k = 0; k < 4; ++k)
                    perimeter += fk::norm(it.inner.quad.p[(k + 1) % 4] - it.inner.quad.p[k]);
            else
                perimeter = 3.14159265 * (it.inner.ellipse.rx + it.inner.ellipse.ry);
            // review sheet cells: side × column (scan, hard, feather 0.05, preserve 0.02), 48 px → 192 px
            constexpr int kCell = 48, kZoom = 4;
            std::vector<fk::Vec2> mids;
            if (it.inner.kind == fk::ShapeKind::quad)
                for (int k = 0; k < 4; ++k)
                    mids.push_back((it.inner.quad.p[k] + it.inner.quad.p[(k + 1) % 4]) * 0.5);
            else
                for (int k = 0; k < 4; ++k) {
                    const double t = k * 3.14159265358979 / 2;
                    mids.push_back(it.inner.ellipse.c + fk::Vec2{std::cos(t) * it.inner.ellipse.rx,
                                                                 std::sin(t) * it.inner.ellipse.ry});
                }
            auto cellOf = [&](const fk::ImageBuffer& b, int64_t ox, int64_t oy, fk::Vec2 m,
                              std::vector<uint8_t>& sheet, int row, int col) {
                const int W = 4 * kCell * kZoom;
                for (int y = 0; y < kCell * kZoom; ++y)
                    for (int x = 0; x < kCell * kZoom; ++x) {
                        const int64_t sx = static_cast<int64_t>(m.x) - kCell / 2 + x / kZoom - ox;
                        const int64_t sy = static_cast<int64_t>(m.y) - kCell / 2 + y / kZoom - oy;
                        uint8_t* o = &sheet[(static_cast<size_t>(row * kCell * kZoom + y) * W +
                                             col * kCell * kZoom + x) *
                                            3];
                        for (int c = 0; c < 3; ++c)
                            o[c] = (sx < 0 || sy < 0 || sx >= b.width() || sy >= b.height())
                                       ? 0
                                       : static_cast<uint8_t>(
                                             b.sample16(sx, sy, std::min(c, b.channels() - 1)) >> 8);
                    }
            };
            const bool crops = a.has("crops");
            for (const auto& g : kGrades) {
                const bool sheetGrade = crops && (g.name == "warm" || g.name == "strong");
                std::vector<uint8_t> sheet;
                if (sheetGrade) {
                    sheet.assign(static_cast<size_t>(4 * kCell * kZoom) * (4 * kCell * kZoom) * 3, 0);
                    for (int k = 0; k < 4; ++k)
                        cellOf(*scan, 0, 0, mids[static_cast<size_t>(k)], sheet, k, 0);
                }
                // the pixel-crop export, graded
                auto ed = fk::ImageBuffer::create(Ri.w, Ri.h, scan->channels(), scan->bits());
                if (!ed) return 1;
                for (int64_t y = 0; y < Ri.h; ++y)
                    for (int64_t x = 0; x < Ri.w; ++x)
                        for (int c = 0; c < scan->channels(); ++c) {
                            const double v = applyGrade(g, c, scan->samplef(Ri.x + x, Ri.y + y, c));
                            if (scan->bits() == 8) {
                                ed->row(y)[x * scan->channels() + c] =
                                    static_cast<uint8_t>(std::lround(v * 255));
                            } else {
                                const uint16_t u = static_cast<uint16_t>(std::lround(v * 65535));
                                std::memcpy(ed->row(y) + (x * scan->channels() + c) * 2, &u, 2);
                            }
                        }
                for (const auto& cfg : cfgs) {
                    fk::CompositeRequest req;
                    req.outerRegion = it.outer;
                    req.frame = it.outer.quad;
                    req.inner = it.inner;
                    req.scanToEdit =
                        fk::Mat3::translate(-static_cast<double>(Ri.x), -static_cast<double>(Ri.y));
                    req.editSize = {Ri.w, Ri.h};
                    req.editExact = true;
                    req.edited = &*ed;
                    req.mask.mode = cfg.mode;
                    if (cfg.mode == fk::MaskMode::feather) req.mask.featherPx = cfg.mm * pxPerMm;
                    if (cfg.mode == fk::MaskMode::preserve_edge) req.mask.preserveEdgePx = cfg.mm * pxPerMm;
                    req.outputMode = fk::GeometryMode::pixel_crop;
                    auto comp = fk::compositeToBuffer(**src, req, 1.0, cancel);
                    if (!comp) {
                        std::cerr << path << ": " << comp.error().message << "\n";
                        return 1;
                    }
                    const fk::ImageBuffer& C = *comp;
                    // the composite covers boundingRect(outer) clamped to the scan, pixel for pixel
                    std::vector<double> contam, seam, strength;
                    double contamMax = 0, outsideMax = 0;
                    int64_t rimCount = 0;
                    auto gradedLab = [&](int64_t x, int64_t y) {
                        const int64_t ex = x - Ri.x, ey = y - Ri.y;
                        if (ex < 0 || ey < 0 || ex >= Ri.w || ey >= Ri.h) return labAt(*scan, x, y);
                        return labAt(*ed, ex, ey);
                    };
                    for (int64_t y = Ro.y; y < Ro.y + Ro.h && y - Ro.y < C.height(); ++y)
                        for (int64_t x = Ro.x; x < Ro.x + Ro.w && x - Ro.x < C.width(); ++x) {
                            const fk::Vec2 p{x + 0.5, y + 0.5};
                            const double d = fk::signedDistance(it.inner, p);
                            if (d > band || d < -band) continue;
                            const Lab s = labAt(*scan, x, y), c = labAt(C, x - Ro.x, y - Ro.y);
                            if (d < 0) {
                                outsideMax = std::max(outsideMax, dE00(s, c));
                                continue;
                            }
                            const double share = pictureShare(x, y, s);
                            if (share <= 0 && (gtInner || d <= edgeZone)) {
                                const double e = dE00(s, c);
                                contam.push_back(e);
                                contamMax = std::max(contamMax, e);
                            } else if (share >= 1) {
                                const Lab gl = gradedLab(x, y);
                                const double full = dE00(s, gl);
                                strength.push_back(full);
                                if (full > 3 && dE00(c, gl) > dE00(c, s)) ++rimCount;
                            }
                        }
                    // 50 % view: 2×2 blocks of the composite vs the colour-matte reference
                    for (int64_t y = Ro.y; y + 1 < Ro.y + std::min(Ro.h, C.height()); y += 2)
                        for (int64_t x = Ro.x; x + 1 < Ro.x + std::min(Ro.w, C.width()); x += 2) {
                            const double d = fk::signedDistance(it.inner, {x + 1.0, y + 1.0});
                            if (d > band || d < -band) continue;
                            double cs[3] = {0, 0, 0}, rs[3] = {0, 0, 0};
                            const int nc = std::min(3, scan->channels());
                            for (int dy = 0; dy < 2; ++dy)
                                for (int dx = 0; dx < 2; ++dx) {
                                    const int64_t X = x + dx, Y = y + dy;
                                    const fk::Vec2 p{X + 0.5, Y + 0.5};
                                    const bool in = fk::signedDistance(it.inner, p) >= 0;
                                    const Lab s = labAt(*scan, X, Y);
                                    const double t = in ? pictureShare(X, Y, s) : 0.0;
                                    for (int c2 = 0; c2 < nc; ++c2) {
                                        cs[c2] += C.samplef(X - Ro.x, Y - Ro.y, c2) / 4;
                                        const double sv = scan->samplef(X, Y, c2);
                                        const int64_t ex = X - Ri.x, ey = Y - Ri.y;
                                        const double gv = (in && ex >= 0 && ey >= 0 && ex < Ri.w && ey < Ri.h)
                                                              ? ed->samplef(ex, ey, c2)
                                                              : sv;
                                        rs[c2] += (sv + t * (gv - sv)) / 4;
                                    }
                                }
                            const Lab cl = nc >= 3 ? toLab(cs[0], cs[1], cs[2]) : toLab(cs[0], cs[0], cs[0]);
                            const Lab rl = nc >= 3 ? toLab(rs[0], rs[1], rs[2]) : toLab(rs[0], rs[0], rs[0]);
                            seam.push_back(dE00(cl, rl));
                        }
                    if (sheetGrade) {
                        const int col = cfg.name == "hard 0"             ? 1
                                        : cfg.name == "feather 0.05 mm"  ? 2
                                        : cfg.name == "preserve 0.02 mm" ? 3
                                                                         : -1;
                        if (col > 0)
                            for (int k = 0; k < 4; ++k)
                                cellOf(C, Ro.x, Ro.y, mids[static_cast<size_t>(k)], sheet, k, col);
                    }
                    Measure m{percentile(contam, 0.99), contamMax,  static_cast<double>(rimCount) / perimeter,
                              percentile(seam, 0.99),   outsideMax, percentile(strength, 0.5)};
                    pj["results"].push_back({{"grade", g.name},
                                             {"mask", cfg.name},
                                             {"contaminationP99", m.contamination},
                                             {"contaminationMax", m.contaminationMax},
                                             {"rimPx", m.rimPx},
                                             {"seam50P99", m.seam50},
                                             {"outsideMax", m.outsideMax},
                                             {"gradeStrength", m.gradeStrength}});
                    Measure& w = worst[cfg.name][g.normal ? "normal" : "strong"];
                    w.contamination = std::max(w.contamination, m.contamination);
                    w.contaminationMax = std::max(w.contaminationMax, m.contaminationMax);
                    w.rimPx = std::max(w.rimPx, m.rimPx);
                    w.seam50 = std::max(w.seam50, m.seam50);
                    w.outsideMax = std::max(w.outsideMax, m.outsideMax);
                    w.gradeStrength = std::max(w.gradeStrength, m.gradeStrength);
                }
                if (sheetGrade) {
                    const int W = 4 * kCell * kZoom;
                    auto img = fk::ImageBuffer::create(W, W, 3, 8);
                    if (img) {
                        std::memcpy(img->data(), sheet.data(), sheet.size());
                        fk::WriteOptions opt;
                        opt.format = fk::FileFormat::png;
                        opt.bits = 8;
                        std::filesystem::create_directories(fk::pathFromUtf8(a.get("crops")));
                        const auto out =
                            fk::pathFromUtf8(a.get("crops")) / (std::filesystem::path(path).stem().string() +
                                                                "_" + label + "_" + g.name + ".png");
                        std::filesystem::remove(out);
                        if (auto sink = fk::createSink(out, opt); sink && (*sink)->begin({W, W}, 3, 8)) {
                            (*sink)->writeRows(img->data(), W, img->rowBytes());
                            (void)(*sink)->finish();
                        }
                    }
                }
            }
            sj["prints"].push_back(pj);
            std::cerr << path << " " << label << " done\n";
        }
        report["scans"].push_back(sj);
    }
    std::cout << "mask                 | normal grades: contam p99  rim px  seam50 p99  outside  strength | "
                 "strong: "
                 "contam  rim  seam50  strength\n";
    json summary = json::object();
    for (const auto& cfg : cfgs) {
        const Measure& n = worst[cfg.name]["normal"];
        const Measure& s = worst[cfg.name]["strong"];
        char line[256];
        std::snprintf(
            line, sizeof line,
            "%-20s |                %6.2f  %6.2f  %10.2f  %7.2f  %8.2f | %14.2f %4.2f %7.2f %9.2f\n",
            cfg.name.c_str(), n.contamination, n.rimPx, n.seam50, n.outsideMax, n.gradeStrength,
            s.contamination, s.rimPx, s.seam50, s.gradeStrength);
        std::cout << line;
        summary[cfg.name] = {{"normal",
                              {{"contaminationP99", n.contamination},
                               {"contaminationMax", n.contaminationMax},
                               {"rimPx", n.rimPx},
                               {"seam50P99", n.seam50},
                               {"outsideMax", n.outsideMax}}},
                             {"strong",
                              {{"contaminationP99", s.contamination},
                               {"rimPx", s.rimPx},
                               {"seam50P99", s.seam50},
                               {"outsideMax", s.outsideMax}}}};
    }
    report["summary"] = summary;
    if (a.has("json")) {
        std::ofstream(fk::pathFromUtf8(a.get("json"))) << report.dump(1) << "\n";
    }
    return 0;
}

} // namespace fkcli
