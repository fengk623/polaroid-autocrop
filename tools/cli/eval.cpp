// fkcli eval — detection metrics against fkfixture ground truth (RFC-0016, TEST-PLAN §4).
#include "cli_common.hpp"

#include "fk/color.hpp"
#include "fk/image_io.hpp"
#include "fk/util.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>

namespace fkcli {

namespace {

struct GtPrint {
    std::string filmType;
    std::array<fk::Vec2, 4> outerQuad{};
    std::vector<fk::Vec2> boundary;
    fk::Shape inner{};
    int upright = 0;
    std::string frame, content;
    double frameBgDeltaE = 100; ///< frame colour vs background level (visibility of the card outline)
};

std::vector<GtPrint> loadGt(const json& j) {
    std::vector<GtPrint> out;
    const double bgl = j.contains("background") ? j["background"].value("level", 0.06) : 0.06;
    const float bgRgb[3] = {static_cast<float>(bgl), static_cast<float>(bgl), static_cast<float>(bgl)};
    float bgLab[3];
    fk::srgbToLab(bgRgb, bgLab);
    for (const auto& p : j.at("prints")) {
        GtPrint g;
        g.filmType = p.value("filmType", "");
        g.frame = p.value("frame", "");
        g.content = p.value("content", "");
        g.upright = p.value("upright", 0);
        if (p.contains("frameColor")) {
            const float fc[3] = {p["frameColor"][0].get<float>(), p["frameColor"][1].get<float>(),
                                 p["frameColor"][2].get<float>()};
            float fl[3];
            fk::srgbToLab(fc, fl);
            g.frameBgDeltaE =
                std::sqrt((fl[0] - bgLab[0]) * (fl[0] - bgLab[0]) + (fl[1] - bgLab[1]) * (fl[1] - bgLab[1]) +
                          (fl[2] - bgLab[2]) * (fl[2] - bgLab[2]));
        }
        for (int i = 0; i < 4; ++i)
            g.outerQuad[i] = {p["outerQuad"][i][0].get<double>(), p["outerQuad"][i][1].get<double>()};
        for (const auto& b : p.at("outerBoundary"))
            g.boundary.push_back({b[0].get<double>(), b[1].get<double>()});
        const auto& in = p.at("inner");
        if (in.value("kind", "quad") == "ellipse") {
            g.inner = fk::Shape::fromEllipse({{in["center"][0].get<double>(), in["center"][1].get<double>()},
                                              in.at("rx").get<double>(),
                                              in.at("ry").get<double>(),
                                              in.value("angle", 0.0)});
        } else {
            fk::Quad q;
            for (int i = 0; i < 4; ++i)
                q.p[i] = {in["points"][i][0].get<double>(), in["points"][i][1].get<double>()};
            g.inner = fk::Shape::fromQuad(q);
        }
        out.push_back(std::move(g));
    }
    return out;
}

double pct(std::vector<double> v, double q) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    const double idx = q * (v.size() - 1);
    const size_t lo = static_cast<size_t>(std::floor(idx)), hi = static_cast<size_t>(std::ceil(idx));
    return v[lo] + (v[hi] - v[lo]) * (idx - lo);
}

/// Mean signed distance from points on the central 80 % of GT segment a→b to the matching detected side
/// (positive = detected edge lies outside the GT edge).
double sideSlack(fk::Vec2 a, fk::Vec2 b, const fk::Shape& det) {
    // detected line nearest in direction to a→b
    const fk::Vec2 mid = (a + b) * 0.5, d = fk::normalized(b - a);
    double best = 1e300;
    int bi = 0;
    for (int i = 0; i < 4; ++i) {
        const fk::Vec2 p = det.quad.p[i], q = det.quad.p[(i + 1) % 4];
        const double ang = std::abs(fk::dot(fk::normalized(q - p), d));
        const fk::Line l{p, fk::normalized(q - p)};
        const double dist = std::abs(fk::signedDistance(l, mid));
        if (ang > 0.9 && dist < best) {
            best = dist;
            bi = i;
        }
    }
    const fk::Vec2 p = det.quad.p[bi], q = det.quad.p[(bi + 1) % 4];
    const fk::Line l{p, fk::normalized(q - p)};
    double sum = 0;
    int n = 0;
    for (int k = 1; k < 10; ++k) {
        const fk::Vec2 g = a + (b - a) * (0.1 + 0.8 * k / 10.0);
        sum += fk::signedDistance(l, g); // > 0 when the GT edge lies inside the detected edge (outward slack)
        ++n;
    }
    return sum / n;
}

} // namespace

int cmdEval(const std::vector<std::string>& argv) {
    const Args a = Args::parse(argv);
    if (a.positional.empty()) {
        std::cerr
            << "usage: fkcli eval <corpus-dir> [--json report.json] [--enforce] [--no-refine] [--limit N] "
               "[--overlays dir]\n";
        return 2;
    }
    const std::filesystem::path dir = fk::pathFromUtf8(a.positional[0]);
    std::vector<std::filesystem::path> scans;
    for (auto& e : std::filesystem::directory_iterator(dir))
        if (e.path().extension() == ".tif" &&
            std::filesystem::exists(
                fk::pathFromUtf8(fk::pathToUtf8(e.path().parent_path() / e.path().stem()) + ".json")))
            scans.push_back(e.path());
    std::sort(scans.begin(), scans.end());
    const int limit = static_cast<int>(a.num("limit", 1e9));
    if (static_cast<int>(scans.size()) > limit) scans.resize(limit);
    const bool refine = !a.has("no-refine");
    const std::string overlays = a.get("overlays");

    // Prints whose frame is nearly the background colour (ΔE < kVisibleDeltaE, e.g. a black frame on a black
    // background) cannot be outlined from the image: they may be missed (the scan then carries a
    // count_mismatch warning) or must be contained and flagged. Accuracy metrics use visible prints.
    constexpr double kVisibleDeltaE = 4.0;
    int prints = 0, visiblePrints = 0, cutsDetected = 0, missedVisible = 0, missedLowVis = 0, typeOk = 0,
        uprightOk = 0, bad = 0, badFlagged = 0, falseLow = 0, visibleScans = 0, visibleScansCountOk = 0,
        countOk = 0, lowVisDetected = 0, lowVisFlagged = 0;
    double maxCut = 0;
    std::vector<double> slacks, innerErrs, seconds;
    json perScan = json::array();
    for (const auto& sp : scans) {
        std::ifstream gf(fk::pathToUtf8(sp.parent_path() / sp.stem()) + ".json");
        json gtj;
        gf >> gtj;
        const auto gt = loadGt(gtj);
        auto src = fk::openTiff(sp);
        if (!src) {
            std::cerr << "cannot open " << sp << ": " << src.error().message << "\n";
            return 1;
        }
        const auto t0 = std::chrono::steady_clock::now();
        fk::CancelToken cancel;
        auto proxy = fk::buildProxy(**src, 3000, cancel, {});
        if (!proxy) return 1;
        fk::DetectParams params;
        params.expectedCount = static_cast<int>(gt.size());
        params.refine = refine;
        auto det = fk::detectPrints(**src, *proxy, params, cancel, {});
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        seconds.push_back(secs);
        if (!det) {
            std::cerr << "detect failed " << sp << ": " << det.error().message << "\n";
            return 1;
        }
        const bool allVisible = std::all_of(
            gt.begin(), gt.end(), [&](const GtPrint& g) { return g.frameBgDeltaE >= kVisibleDeltaE; });
        if (det->items.size() == gt.size()) ++countOk;
        if (allVisible) {
            ++visibleScans;
            if (det->items.size() == gt.size()) ++visibleScansCountOk;
        }
        json sj{{"scan", fk::pathToUtf8(sp.filename())},
                {"gt", gt.size()},
                {"found", det->items.size()},
                {"seconds", secs},
                {"warnings", det->warnings}};
        json pj = json::array();
        std::vector<bool> used(det->items.size(), false);
        for (const auto& g : gt) {
            ++prints;
            const bool visible = g.frameBgDeltaE >= kVisibleDeltaE;
            if (visible) ++visiblePrints;
            fk::Vec2 gc{0, 0};
            for (auto& p : g.outerQuad)
                gc = gc + p;
            gc = gc / 4.0;
            int best = -1;
            double bd = 1e300;
            for (size_t i = 0; i < det->items.size(); ++i) {
                if (used[i]) continue;
                const double d = fk::norm(fk::centroid(det->items[i].outer) - gc);
                if (d < bd) {
                    bd = d;
                    best = static_cast<int>(i);
                }
            }
            const double size = fk::norm(g.outerQuad[2] - g.outerQuad[0]);
            // a match must contain the GT centre region: centroid distance below a quarter of the diagonal,
            // or (conservative expansions) the GT centre inside the detected outline
            if (best >= 0 && bd > 0.25 * size && fk::signedDistance(det->items[best].outer, gc) < 0)
                best = -1;
            if (best < 0) {
                if (visible)
                    ++missedVisible;
                else
                    ++missedLowVis;
                pj.push_back({{"missed", true},
                              {"visible", visible},
                              {"filmType", g.filmType},
                              {"frame", g.frame},
                              {"frameBgDeltaE", g.frameBgDeltaE}});
                continue;
            }
            used[best] = true;
            const auto& it = det->items[best];
            // containment: every GT boundary sample inside the detected outer (0.5 px tolerance)
            double worst = 0;
            for (const auto& b : g.boundary)
                worst = std::min(worst, fk::signedDistance(it.outer, b));
            const bool cut = worst < -0.5;
            if (cut) ++cutsDetected;
            maxCut = std::max(maxCut, -worst);
            double maxSlack = 0;
            std::vector<double> sl;
            for (int s = 0; s < 4; ++s) {
                const double v = sideSlack(g.outerQuad[s], g.outerQuad[(s + 1) % 4], it.outer);
                sl.push_back(v);
                if (visible) slacks.push_back(v);
                maxSlack = std::max(maxSlack, std::abs(v));
            }
            double innerErr = 0;
            if (g.inner.kind != it.inner.kind) {
                innerErr = 99;
            } else if (g.inner.kind == fk::ShapeKind::ellipse) {
                innerErr = std::max(fk::norm(g.inner.ellipse.c - it.inner.ellipse.c),
                                    std::abs(g.inner.ellipse.rx - it.inner.ellipse.rx));
            } else {
                for (int s = 0; s < 4; ++s)
                    innerErr = std::max(innerErr, std::abs(sideSlack(g.inner.quad.p[s],
                                                                     g.inner.quad.p[(s + 1) % 4], it.inner)));
            }
            if (visible) {
                innerErrs.push_back(innerErr);
                if (it.filmTypeId == g.filmType) ++typeOk;
                if (it.upright == g.upright) ++uprightOk;
            }
            const bool isBad = cut || maxSlack > 8 || innerErr > 15 || it.upright != g.upright;
            const bool flagged = it.confidence < fk::kLowConfidence;
            if (!visible) {
                ++lowVisDetected;
                if (flagged) ++lowVisFlagged;
            }
            if (isBad) {
                ++bad;
                if (flagged) ++badFlagged;
            } else if (flagged) {
                ++falseLow;
            }
            pj.push_back({{"filmType", g.filmType},
                          {"detectedType", it.filmTypeId},
                          {"frame", g.frame},
                          {"content", g.content},
                          {"visible", visible},
                          {"frameBgDeltaE", g.frameBgDeltaE},
                          {"cutDepthPx", -worst},
                          {"slackPx", sl},
                          {"innerErrPx", innerErr},
                          {"upright", {g.upright, it.upright}},
                          {"confidence", it.confidence},
                          {"reasons", it.reasons}});
        }
        sj["prints"] = pj;
        perScan.push_back(sj);
        if (!overlays.empty()) {
            std::vector<std::vector<fk::Vec2>> gtb;
            for (auto& g : gt)
                gtb.push_back(g.boundary);
            std::filesystem::create_directories(fk::pathFromUtf8(overlays));
            writeOverlay(fk::pathFromUtf8(overlays) / (fk::pathToUtf8(sp.stem()) + ".png"), *proxy,
                         det->items, &gtb);
        }
        std::fprintf(stderr, "%s: gt=%zu found=%zu %.1fs\n", fk::pathToUtf8(sp.filename()).c_str(), gt.size(),
                     det->items.size(), secs);
    }
    std::vector<double> absSlack;
    for (double s : slacks)
        absSlack.push_back(std::abs(s));
    const double visDen = std::max(1, visiblePrints);
    json summary{
        {"scans", scans.size()},
        {"prints", prints},
        {"visible_prints", visiblePrints},
        {"cuts_on_detected", cutsDetected},
        {"max_cut_depth_px", maxCut},
        {"missed_visible", missedVisible},
        {"missed_low_visibility", missedLowVis},
        {"low_visibility_detected", lowVisDetected},
        {"low_visibility_flagged", lowVisFlagged},
        {"outer_slack_median_px", pct(slacks, 0.5)},
        {"outer_slack_p95_px", pct(absSlack, 0.95)},
        {"outer_slack_max_px", absSlack.empty() ? 0 : *std::max_element(absSlack.begin(), absSlack.end())},
        {"inner_error_median_px", pct(innerErrs, 0.5)},
        {"inner_error_p95_px", pct(innerErrs, 0.95)},
        {"count_accuracy_all", scans.empty() ? 0.0 : static_cast<double>(countOk) / scans.size()},
        {"count_accuracy_visible_scans",
         visibleScans ? static_cast<double>(visibleScansCountOk) / visibleScans : 1.0},
        {"film_type_accuracy", typeOk / visDen},
        {"orientation_accuracy", uprightOk / visDen},
        {"bad_prints", bad},
        {"low_confidence_recall", bad ? static_cast<double>(badFlagged) / bad : 1.0},
        {"false_low_confidence", falseLow},
        {"seconds_median", pct(seconds, 0.5)}};
    std::cout << summary.dump(2) << std::endl;
    if (a.has("json")) {
        std::ofstream o(a.get("json"));
        o << json{{"summary", summary}, {"scans", perScan}}.dump(2);
    }
    if (a.has("enforce")) {
        // TEST-PLAN §4
        std::vector<std::string> failed;
        auto need = [&](bool ok, const char* what) {
            if (!ok) failed.push_back(what);
        };
        need(cutsDetected == 0, "cuts_on_detected == 0");
        need(missedVisible == 0, "missed_visible == 0");
        need(lowVisFlagged == lowVisDetected || bad == badFlagged, "low-visibility detections flagged");
        need(summary["outer_slack_median_px"].get<double>() <= 3.0, "outer slack median <= 3");
        need(summary["outer_slack_p95_px"].get<double>() <= 8.0, "outer slack p95 <= 8");
        need(summary["outer_slack_max_px"].get<double>() <= 20.0, "outer slack max <= 20");
        need(summary["inner_error_median_px"].get<double>() <= 3.0, "inner error median <= 3");
        need(summary["inner_error_p95_px"].get<double>() <= 10.0, "inner error p95 <= 10");
        need(summary["count_accuracy_visible_scans"].get<double>() >= 1.0,
             "count accuracy (visible scans) == 1");
        need(summary["orientation_accuracy"].get<double>() >= 0.99, "orientation accuracy >= 0.99");
        need(summary["low_confidence_recall"].get<double>() >= 0.9, "low-confidence recall >= 0.9");
        if (!failed.empty()) {
            std::cerr << "EVAL THRESHOLDS NOT MET (TEST-PLAN §4):\n";
            for (auto& f : failed)
                std::cerr << "  - " << f << "\n";
            return 1;
        }
    }
    return 0;
}

} // namespace fkcli
