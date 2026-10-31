// fkcli — command-line front end over the same core API as the app (RFC-0001). Used for integration
// tests, detection evaluation and measurements.
#include "cli_common.hpp"

#include "fk/composite.hpp"
#include "fk/detection.hpp"
#include "fk/export.hpp"
#include "fk/image_io.hpp"
#include "fk/memory.hpp"
#include "fk/project.hpp"
#include "fk/util.hpp"
#include "fk/version.hpp"

#include <chrono>
#include <cstdio>
#include <iostream>

using namespace fkcli;

namespace {

int usage() {
    std::cerr
        << fk::productNameAndVersion() << " — fkcli\n"
        << "  fkcli info <scan.tif>\n"
        << "  fkcli integrity <scan.tif>\n"
        << "  fkcli detect <scan.tif> [--expected N] [--no-refine] [--json out.json] [--overlay out.png]\n"
        << "  fkcli eval <corpus-dir> [--json report.json] [--enforce] [--overlays dir] [--limit N]\n"
        << "  fkcli realeval <gt.json>... --scans DIR [--json report.json] [--enforce]\n"
        << "  fkcli maskeval <scan.tif>... [--json report.json]   (composite mask at the image-area edge)\n"
        << "  fkcli export <scan.tif> --out-dir DIR [--what image|print] [--mode crop|straighten]\n"
        << "               [--format tif|png|jpg|dng] [--bits 8|16] [--upright] [--expected N]\n"
        << "  fkcli roundtrip <scan.tif> --out-dir DIR [--expected N] [--mode crop|straighten]\n"
        << "  fkcli perf <scan.tif> --out-dir DIR [--expected N]   (detect + export all + composite, "
           "memory)\n";
    return 2;
}

double secondsSince(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t).count();
}

json memJson() {
    const auto m = fk::currentMemoryStats();
    return json{{"residentMB", m.residentBytes / 1048576.0},
                {"peakResidentMB", m.peakResidentBytes / 1048576.0},
                {"privateMB", m.privateBytes / 1048576.0},
                {"accountedMB", m.accountedBytes / 1048576.0},
                {"accountedPeakMB", m.accountedPeakBytes / 1048576.0}};
}

struct Detected {
    std::unique_ptr<fk::TiffSource> src;
    fk::Proxy proxy;
    fk::DetectionResult det;
};

fk::Result<Detected> openAndDetect(const std::string& path, int expected, bool refine) {
    Detected d;
    auto s = fk::openTiff(fk::pathFromUtf8(path));
    if (!s) return s.error();
    d.src = std::move(*s);
    fk::CancelToken c;
    auto p = fk::buildProxy(*d.src, 3000, c, {});
    if (!p) return p.error();
    d.proxy = std::move(*p);
    fk::DetectParams params;
    params.expectedCount = expected;
    params.refine = refine;
    auto r = fk::detectPrints(*d.src, d.proxy, params, c, {});
    if (!r) return r.error();
    d.det = std::move(*r);
    return d;
}

fk::FileFormat formatFrom(const std::string& f) {
    if (f == "png") return fk::FileFormat::png;
    if (f == "jpg" || f == "jpeg") return fk::FileFormat::jpeg;
    if (f == "dng") return fk::FileFormat::dng;
    return fk::FileFormat::tiff;
}

int fail(const fk::Error& e) {
    std::cerr << "error [" << fk::errcName(e.code) << "]: " << e.message
              << (e.detail.empty() ? "" : " — " + e.detail) << "\n";
    return 1;
}

int cmdInfo(const Args& a) {
    auto s = fk::openTiff(fk::pathFromUtf8(a.positional.at(0)));
    if (!s) return fail(s.error());
    json j = infoToJson((*s)->info());
    j["accessPath"] = fk::tiffAccessPathName((*s)->accessPath());
    json fields = json::array();
    for (auto& f : (*s)->metadata().fields)
        fields.push_back({{"name", f.name}, {"value", f.value}, {"class", fk::privacyClassName(f.cls)}});
    j["metadata"] = fields;
    std::cout << j.dump(2) << "\n";
    return 0;
}

int cmdIntegrity(const Args& a) {
    auto r = fk::checkTiffIntegrity(fk::pathFromUtf8(a.positional.at(0)));
    if (!r) return fail(r.error());
    std::cout << json{{"ok", r->ok}, {"problems", r->problems}}.dump(2) << "\n";
    return r->ok ? 0 : 1;
}

int cmdDetect(const Args& a) {
    const auto t0 = std::chrono::steady_clock::now();
    auto d = openAndDetect(a.positional.at(0), static_cast<int>(a.num("expected", 4)), !a.has("no-refine"));
    if (!d) return fail(d.error());
    json j = detectionToJson(d->det);
    j["seconds"] = secondsSince(t0);
    j["memory"] = memJson();
    if (a.has("overlay")) writeOverlay(fk::pathFromUtf8(a.get("overlay")), d->proxy, d->det.items);
    const std::string out = j.dump(2);
    if (a.has("json")) {
        FILE* f = std::fopen(a.get("json").c_str(), "wb");
        if (f) {
            std::fwrite(out.data(), 1, out.size(), f);
            std::fclose(f);
        }
    } else {
        std::cout << out << "\n";
    }
    return 0;
}

fk::ExportRequest makeRequest(const fk::DetectedItem& it, const Args& a, const std::filesystem::path& out) {
    fk::ExportRequest r;
    const bool print = a.get("what", "image") == "print";
    r.target = print ? fk::ExportTarget::full_print : fk::ExportTarget::image_area;
    r.region = print ? it.outer : it.inner;
    r.frame = it.outer.quad;
    r.mode =
        a.get("mode", "crop") == "straighten" ? fk::GeometryMode::straightened : fk::GeometryMode::pixel_crop;
    r.upright = a.has("upright") ? it.upright : 0;
    r.write.format = formatFrom(a.get("format", "tif"));
    r.write.bits = static_cast<int>(a.num("bits", 0));
    r.write.compression = fk::Compression::lzw;
    r.allowDepthReduction = a.has("allow-8bit");
    r.output = out;
    return r;
}

int cmdExport(const Args& a) {
    auto d = openAndDetect(a.positional.at(0), static_cast<int>(a.num("expected", 4)), true);
    if (!d) return fail(d.error());
    const auto dir = fk::pathFromUtf8(a.get("out-dir", "."));
    std::filesystem::create_directories(dir);
    const std::string stem = fk::pathToUtf8(fk::pathFromUtf8(a.positional[0]).stem());
    json reps = json::array();
    int idx = 1;
    for (const auto& it : d->det.items) {
        const std::string kind = a.get("what", "image") == "print" ? "_FULL" : "_EDIT";
        const auto fmt = formatFrom(a.get("format", "tif"));
        const auto out =
            fk::nextFreeVersionPath(dir, stem + "_" + fk::itemLabel(idx++) + kind, fk::fileExtension(fmt));
        auto rep = fk::exportRegion(*d->src, makeRequest(it, a, out), {}, {});
        if (!rep) return fail(rep.error());
        reps.push_back({{"output", fk::pathToUtf8(rep->output)},
                        {"size", {rep->size.w, rep->size.h}},
                        {"bits", rep->bits},
                        {"frameBitExact", rep->frameBitExact},
                        {"notExactReasons", rep->notExactReasons},
                        {"seconds", rep->seconds},
                        {"peakAccountedMB", rep->peakAccountedBytes / 1048576.0}});
    }
    std::cout << json{{"exports", reps}, {"memory", memJson()}}.dump(2) << "\n";
    return 0;
}

/// detect → export image area → re-import unchanged → composite → compare with a direct full-print export.
int cmdRoundtrip(const Args& a) {
    auto d = openAndDetect(a.positional.at(0), static_cast<int>(a.num("expected", 4)), true);
    if (!d) return fail(d.error());
    const auto dir = fk::pathFromUtf8(a.get("out-dir", "."));
    std::filesystem::create_directories(dir);
    const bool straight = a.get("mode", "crop") == "straighten";
    json results = json::array();
    bool allExact = true;
    int idx = 0;
    for (const auto& it : d->det.items) {
        ++idx;
        Args ea = a;
        fk::ExportRequest req = makeRequest(it, ea, dir / ("rt_" + std::to_string(idx) + "_edit.tif"));
        req.region = it.inner;
        auto edit = fk::exportRegion(*d->src, req, {}, {});
        if (!edit) return fail(edit.error());
        fk::ImageInfo ei;
        auto edited = fk::readImageFile(edit->output, &ei);
        if (!edited) return fail(edited.error());
        fk::CompositeRequest cr;
        cr.outerRegion = it.outer;
        cr.frame = it.outer.quad;
        cr.inner = it.inner;
        cr.scanToEdit = edit->geometry.scanToOut;
        cr.editSize = edit->size;
        cr.editExact = !edit->resampled;
        cr.edited = &*edited;
        cr.editedIcc = ei.icc;
        cr.outputMode = fk::GeometryMode::pixel_crop;
        cr.write.format = fk::FileFormat::tiff;
        cr.write.bits = 0;
        cr.output = dir / ("rt_" + std::to_string(idx) + "_final.tif");
        auto comp = fk::compositeItem(*d->src, cr, {}, {});
        if (!comp) return fail(comp.error());
        fk::ExportRequest pr = makeRequest(it, ea, dir / ("rt_" + std::to_string(idx) + "_direct.tif"));
        pr.region = it.outer;
        pr.mode = fk::GeometryMode::pixel_crop;
        pr.upright = 0;
        auto direct = fk::exportRegion(*d->src, pr, {}, {});
        if (!direct) return fail(direct.error());
        const bool exact = comp->pixelSha256 == direct->pixelSha256;
        allExact = allExact && (exact || straight);
        results.push_back({{"item", idx},
                           {"bitExact", exact},
                           {"compositeSeconds", comp->seconds},
                           {"peakAccountedMB", comp->peakAccountedBytes / 1048576.0}});
    }
    std::cout
        << json{{"mode", straight ? "straighten" : "crop"}, {"items", results}, {"memory", memJson()}}.dump(2)
        << "\n";
    return allExact ? 0 : 1;
}

int cmdPerf(const Args& a) {
    json j;
    const auto t0 = std::chrono::steady_clock::now();
    auto d = openAndDetect(a.positional.at(0), static_cast<int>(a.num("expected", 4)), true);
    if (!d) return fail(d.error());
    j["scan"] = infoToJson(d->src->info());
    j["accessPath"] = fk::tiffAccessPathName(d->src->accessPath());
    j["detectSeconds"] = secondsSince(t0);
    j["afterDetect"] = memJson();
    const auto dir = fk::pathFromUtf8(a.get("out-dir", "."));
    std::filesystem::create_directories(dir);
    json ex = json::array();
    int idx = 0;
    for (const auto& it : d->det.items) {
        ++idx;
        fk::ExportRequest r = makeRequest(it, a, dir / ("perf_" + std::to_string(idx) + ".tif"));
        r.region = it.outer;
        std::error_code ec;
        std::filesystem::remove(r.output, ec);
        auto rep = fk::exportRegion(*d->src, r, {}, {});
        if (!rep) return fail(rep.error());
        ex.push_back({{"seconds", rep->seconds},
                      {"peakAccountedMB", rep->peakAccountedBytes / 1048576.0},
                      {"size", {rep->size.w, rep->size.h}}});
        std::filesystem::remove(r.output, ec);
    }
    j["exports"] = ex;
    j["final"] = memJson();
    std::cout << j.dump(2) << "\n";
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) return usage();
    const std::string cmd = argv[1];
    std::vector<std::string> rest(argv + 2, argv + argc);
    const Args a = Args::parse(rest);
    try {
        if (cmd == "eval") return cmdEval(rest);
        if (cmd == "realeval") return cmdRealEval(rest);
        if (cmd == "maskeval") return cmdMaskEval(rest);
        if (a.positional.empty()) return usage();
        if (cmd == "info") return cmdInfo(a);
        if (cmd == "integrity") return cmdIntegrity(a);
        if (cmd == "detect") return cmdDetect(a);
        if (cmd == "export") return cmdExport(a);
        if (cmd == "roundtrip") return cmdRoundtrip(a);
        if (cmd == "perf") return cmdPerf(a);
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return usage();
}
