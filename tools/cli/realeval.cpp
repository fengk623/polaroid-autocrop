// fkcli realeval — detection accuracy against real-scan ground truth (fk.realgt/1, docs/v1/REAL-SCAN-GT.md).
//
// For every GT file the scan is found by SHA-256 in --scans, detected with an automatic count, and each GT
// print is matched to the detected print whose outer contains its centre. Along each GT side (central 80 %,
// 40 points) the signed distance d of the GT edge point to the detected shape is measured (positive = the GT
// point lies inside the detected shape):
//   outer: d > 0 is slack (detected edge outside the paper edge); a cut is d < −(uncertainty + 0.5 px);
//          sides on the scan border ("edge": "scan_border") are not measured;
//   inner: d > 0 means frame inside the detected image area (a white border in an "image only" export);
//          d < 0 means picture outside it (cut off).
// Values are reported per side (mean and worst) in px and mm, per print, and summarised overall and per
// stratum (set, DPI class, bit depth, background, inner shape). `--enforce` applies the gates frozen in
// REAL-SCAN-GT §3.
#include "cli_common.hpp"

#include "fk/hash.hpp"
#include "fk/image_io.hpp"
#include "fk/util.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>

namespace fkcli {

namespace {

fk::Shape quadFrom(const json& j) {
    if (j.value("kind", "quad") == "ellipse") {
        fk::Ellipse e;
        e.c = {j.at("cx").get<double>(), j.at("cy").get<double>()};
        e.rx = j.at("rx").get<double>();
        e.ry = j.at("ry").get<double>();
        e.angleRad = j.value("angle", 0.0);
        return fk::Shape::fromEllipse(e);
    }
    fk::Quad q;
    for (int k = 0; k < 4; ++k)
        q.p[k] = {j.at("points").at(k).at(0).get<double>(), j.at("points").at(k).at(1).get<double>()};
    return fk::Shape::fromQuad(q);
}

struct SideStat {
    double mean = 0, worst = 0, minimum = 0; ///< px; worst = largest positive, minimum = most negative
    bool measured = false;
};

/// Signed distances of GT edge points (central 80 % of each side) to `det`.
std::array<SideStat, 4> sideStats(const fk::Shape& gt, const fk::Shape& det,
                                  const std::array<bool, 4>& skip) {
    std::array<SideStat, 4> out{};
    if (gt.kind != fk::ShapeKind::quad) return out;
    for (int s = 0; s < 4; ++s) {
        if (skip[s]) continue;
        const fk::Vec2 a = gt.quad.p[s], b = gt.quad.p[(s + 1) % 4];
        std::vector<double> d;
        for (int k = 0; k < 40; ++k) {
            const double t = 0.1 + 0.8 * (k + 0.5) / 40;
            d.push_back(fk::signedDistance(det, a + (b - a) * t));
        }
        SideStat st;
        st.measured = true;
        double sum = 0;
        for (double v : d)
            sum += v;
        st.mean = sum / d.size();
        st.worst = *std::max_element(d.begin(), d.end());
        st.minimum = *std::min_element(d.begin(), d.end());
        out[s] = st;
    }
    return out;
}

double pctl(std::vector<double> v, double q) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[std::min(v.size() - 1, static_cast<size_t>(q * (v.size() - 1) + 0.5))];
}

struct Acc {
    int prints = 0, missed = 0, extra = 0, cuts = 0;
    std::vector<double> outerSlackMm, innerMm, frameInAreaMm, pictureCutMm, exportFrameMm, exportCutMm;
    void add(const Acc& o) {
        prints += o.prints;
        missed += o.missed;
        extra += o.extra;
        cuts += o.cuts;
        outerSlackMm.insert(outerSlackMm.end(), o.outerSlackMm.begin(), o.outerSlackMm.end());
        innerMm.insert(innerMm.end(), o.innerMm.begin(), o.innerMm.end());
        frameInAreaMm.insert(frameInAreaMm.end(), o.frameInAreaMm.begin(), o.frameInAreaMm.end());
        pictureCutMm.insert(pictureCutMm.end(), o.pictureCutMm.begin(), o.pictureCutMm.end());
        exportFrameMm.insert(exportFrameMm.end(), o.exportFrameMm.begin(), o.exportFrameMm.end());
        exportCutMm.insert(exportCutMm.end(), o.exportCutMm.begin(), o.exportCutMm.end());
    }
    /// Values with no measured side (e.g. outer slack of a scan whose sides all lie on its border) are null.
    json summary() const {
        auto mx = [](const std::vector<double>& v) -> json {
            if (v.empty()) return nullptr;
            return *std::max_element(v.begin(), v.end());
        };
        auto med = [](const std::vector<double>& v) -> json {
            if (v.empty()) return nullptr;
            return pctl(v, 0.5);
        };
        std::vector<double> absInner;
        for (double v : innerMm)
            absInner.push_back(std::abs(v));
        return {{"prints", prints},
                {"missed", missed},
                {"extra", extra},
                {"outer_cuts", cuts},
                {"outer_sides_measured", outerSlackMm.size()},
                {"outer_slack_mm_median", med(outerSlackMm)},
                {"outer_slack_mm_max", mx(outerSlackMm)},
                {"inner_side_error_mm_abs_median", med(absInner)},
                {"inner_side_error_mm_abs_max", mx(absInner)},
                {"frame_in_image_area_mm_max", mx(frameInAreaMm)},
                {"picture_cut_mm_max", mx(pictureCutMm)},
                {"straightened_export_frame_mm_max", mx(exportFrameMm)},
                {"straightened_export_picture_cut_mm_max", mx(exportCutMm)}};
    }
};

} // namespace

int cmdRealEval(const std::vector<std::string>& args) {
    const Args a = Args::parse(args);
    if (a.positional.empty() || !a.has("scans")) {
        std::cerr << "usage: fkcli realeval <gt.json>... --scans DIR [--json report.json] [--enforce]\n";
        return 2;
    }
    // index the scans by SHA-256 (only files whose hash a GT file asks for are needed, but the names are
    // free)
    std::map<std::string, std::filesystem::path> bySha;
    for (const auto& e : std::filesystem::directory_iterator(fk::pathFromUtf8(a.get("scans")))) {
        const auto ext = e.path().extension().string();
        if (ext != ".tif" && ext != ".tiff" && ext != ".TIF" && ext != ".TIFF") continue;
        if (auto h = fk::sha256File(e.path())) bySha[*h] = e.path();
    }
    Acc all;
    std::map<std::string, Acc> strata;
    json scans = json::array();
    for (const auto& gtPath : a.positional) {
        std::ifstream f(fk::pathFromUtf8(gtPath));
        json gt;
        f >> gt;
        if (gt.value("schema", "") != "fk.realgt/1") {
            std::cerr << gtPath << ": not an fk.realgt/1 file\n";
            return 1;
        }
        const std::string sha = gt.at("scan").at("sha256").get<std::string>();
        auto it = bySha.find(sha);
        if (it == bySha.end()) {
            std::cerr << gtPath << ": scan " << sha.substr(0, 12) << " not found in " << a.get("scans")
                      << "\n";
            return 1;
        }
        auto src = fk::openTiff(it->second);
        if (!src) return 1;
        fk::CancelToken cancel;
        auto proxy = fk::buildProxy(**src, 3000, cancel, {});
        if (!proxy) return 1;
        fk::DetectParams params;
        params.expectedCount = 0; // automatic
        auto det = fk::detectPrints(**src, *proxy, params, cancel, {});
        if (!det) return 1;
        const auto& info = (*src)->info();
        const double dpi = gt["scan"].value("dpiMeasured", json()).is_number()
                               ? gt["scan"]["dpiMeasured"].get<double>()
                               : (info.dpiX > 0 ? info.dpiX : det->dpiUsed);
        const double mmPerPx = 25.4 / dpi;
        const std::string set = gt.value("set", "tuning");
        const std::string dpiClass = dpi < 1000 ? "dpi<1000" : (dpi < 1400 ? "dpi1000-1399" : "dpi>=1400");
        const std::string bits = std::to_string(info.bitsPerSample) + "-bit";
        const std::string bg = "background:" + gt.value("background", "unknown");
        Acc acc;
        std::vector<bool> used(det->items.size(), false);
        json pj = json::array();
        for (const auto& p : gt.at("prints")) {
            ++acc.prints;
            const fk::Shape go = quadFrom(p.at("outer")), gi = quadFrom(p.at("inner"));
            const std::string shape = gi.kind == fk::ShapeKind::ellipse ? "shape:round" : "shape:rect";
            const fk::Vec2 c = fk::centroid(go);
            int best = -1;
            for (size_t i = 0; i < det->items.size(); ++i)
                if (!used[i] && fk::contains(det->items[i].outer, c)) best = static_cast<int>(i);
            if (best < 0) {
                ++acc.missed;
                pj.push_back({{"id", p.value("id", "")}, {"missed", true}});
                continue;
            }
            used[best] = true;
            const auto& d = det->items[best];
            std::array<bool, 4> border{};
            std::array<double, 4> uo{}, ui{};
            const json m = p.value("measurement", json::object());
            for (int s = 0; s < 4; ++s) {
                if (m.contains("outer") && m["outer"].size() == 4)
                    border[s] = m["outer"][s].value("edge", "") == "scan_border";
                if (p.contains("uncertaintyPx")) {
                    uo[s] = p["uncertaintyPx"]["outer"][s].get<double>();
                    ui[s] = p["uncertaintyPx"]["inner"][s].get<double>();
                }
            }
            const auto os = sideStats(go, d.outer, border);
            const auto is = sideStats(gi, d.inner, {false, false, false, false});
            // G6: what an "image only" export (straightened, the default) covers, mapped back onto the scan
            const fk::ExportGeometry eg =
                fk::computeExportGeometry(d.inner, d.outer.quad, fk::GeometryMode::straightened, 0,
                                          std::nullopt, {info.width, info.height});
            fk::Quad fp;
            const double ow = static_cast<double>(eg.outSize.w), oh = static_cast<double>(eg.outSize.h);
            fp.p[0] = eg.outToScan.apply({0, 0});
            fp.p[1] = eg.outToScan.apply({ow, 0});
            fp.p[2] = eg.outToScan.apply({ow, oh});
            fp.p[3] = eg.outToScan.apply({0, oh});
            const auto es =
                sideStats(gi, fk::Shape::fromQuad(fk::normalizeQuad(fp.p)), {false, false, false, false});
            json sides = json::array();
            int cuts = 0;
            for (int s = 0; s < 4; ++s) {
                json sj;
                if (os[s].measured) {
                    const double cutDepth = -os[s].minimum - uo[s] - 0.5;
                    if (cutDepth > 0) ++cuts;
                    acc.outerSlackMm.push_back(os[s].mean * mmPerPx);
                    sj["outer"] = {{"slackMeanPx", os[s].mean},
                                   {"slackMeanMm", os[s].mean * mmPerPx},
                                   {"minPx", os[s].minimum},
                                   {"cut", cutDepth > 0}};
                } else {
                    sj["outer"] = "scan_border";
                }
                if (is[s].measured) {
                    acc.innerMm.push_back(is[s].mean * mmPerPx);
                    acc.frameInAreaMm.push_back(std::max(0.0, is[s].mean) * mmPerPx);
                    acc.pictureCutMm.push_back(std::max(0.0, -is[s].mean) * mmPerPx);
                    sj["inner"] = {{"meanPx", is[s].mean},
                                   {"meanMm", is[s].mean * mmPerPx},
                                   {"worstFramePx", is[s].worst},
                                   {"worstCutPx", is[s].minimum},
                                   {"uncertaintyPx", ui[s]}};
                }
                if (es[s].measured) {
                    acc.exportFrameMm.push_back(std::max(0.0, es[s].mean) * mmPerPx);
                    acc.exportCutMm.push_back(std::max(0.0, -es[s].mean) * mmPerPx);
                    sj["straightenedExport"] = {{"meanPx", es[s].mean}, {"worstFramePx", es[s].worst}};
                }
                sides.push_back(sj);
            }
            acc.cuts += cuts;
            pj.push_back(
                {{"id", p.value("id", "")}, {"shape", shape}, {"sides", sides}, {"outerCuts", cuts}});
            Acc one;
            one.prints = 1;
            one.cuts = cuts;
            for (int s = 0; s < 4; ++s) {
                if (os[s].measured) one.outerSlackMm.push_back(os[s].mean * mmPerPx);
                if (es[s].measured) {
                    one.exportFrameMm.push_back(std::max(0.0, es[s].mean) * mmPerPx);
                    one.exportCutMm.push_back(std::max(0.0, -es[s].mean) * mmPerPx);
                }
                if (is[s].measured) {
                    one.innerMm.push_back(is[s].mean * mmPerPx);
                    one.frameInAreaMm.push_back(std::max(0.0, is[s].mean) * mmPerPx);
                    one.pictureCutMm.push_back(std::max(0.0, -is[s].mean) * mmPerPx);
                }
            }
            strata[shape].add(one);
        }
        for (size_t i = 0; i < used.size(); ++i)
            if (!used[i]) ++acc.extra;
        for (const auto& key : {"set:" + set, dpiClass, bits, bg}) {
            Acc copy = acc;
            strata[key].add(copy);
        }
        all.add(acc);
        scans.push_back({{"gt", fk::pathToUtf8(fk::pathFromUtf8(gtPath).filename())},
                         {"sha12", sha.substr(0, 12)},
                         {"set", set},
                         {"dpi", dpi},
                         {"found", det->items.size()},
                         {"warnings", det->warnings},
                         {"summary", acc.summary()},
                         {"prints", pj}});
        std::fprintf(stderr, "%s: %zu found, %d missed, %d cuts\n", sha.substr(0, 12).c_str(),
                     det->items.size(), acc.missed, acc.cuts);
    }
    json strataJ = json::object();
    for (const auto& [k, v] : strata)
        strataJ[k] = v.summary();
    const json summary = all.summary();
    std::cout << summary.dump(2) << std::endl;
    if (a.has("json")) {
        std::ofstream o(fk::pathFromUtf8(a.get("json")));
        o << json{{"summary", summary}, {"strata", strataJ}, {"scans", scans}}.dump(2);
    }
    if (a.has("enforce")) {
        // REAL-SCAN-GT §3 (frozen 2026-10-30 before the U012 detection changes)
        std::vector<std::string> failed;
        auto need = [&](bool ok, const char* what) {
            if (!ok) failed.push_back(what);
        };
        need(all.cuts == 0, "outer cuts == 0");
        need(all.missed == 0 && all.extra == 0, "no missed or extra prints");
        // a gate with nothing measured is reported, not passed silently
        auto le = [&](const char* key, double limit) {
            if (summary[key].is_null()) {
                std::cerr << "realeval: nothing measured for " << key << "\n";
                return true;
            }
            return summary[key].get<double>() <= limit;
        };
        need(le("outer_slack_mm_median", 0.30), "outer slack median <= 0.30 mm");
        need(le("outer_slack_mm_max", 0.60), "outer slack (side mean) <= 0.60 mm");
        need(le("frame_in_image_area_mm_max", 0.10), "frame inside the image area (side mean) <= 0.10 mm");
        need(le("picture_cut_mm_max", 0.10), "picture cut (side mean) <= 0.10 mm");
        need(le("straightened_export_frame_mm_max", 0.10),
             "straightened export: frame (side mean) <= 0.10 mm");
        need(le("straightened_export_picture_cut_mm_max", 0.10),
             "straightened export: picture cut (side mean) <= 0.10 mm");
        for (const auto& f : failed)
            std::cerr << "realeval: FAILED " << f << "\n";
        return failed.empty() ? 0 : 3;
    }
    return 0;
}

} // namespace fkcli
