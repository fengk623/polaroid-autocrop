// Projects, sidecars, templates (RFC-0009, docs/project-format.md).
#include "fk/project.hpp"

#include "fk/color.hpp"
#include "fk/hash.hpp"
#include "fk/product_info.hpp"
#include "fk/util.hpp"
#include "fk/version.hpp"
#include "sink_base.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace fk {

using json = nlohmann::ordered_json;

namespace {
json sidecarJson(const Sidecar& s);
Result<Sidecar> sidecarFromJson(const json& j);
} // namespace

const char* boundarySourceName(BoundarySource s) {
    switch (s) {
    case BoundarySource::automatic:
        return "auto";
    case BoundarySource::templated:
        return "template";
    case BoundarySource::manual:
        return "manual";
    }
    return "auto";
}
static BoundarySource boundarySourceFrom(const std::string& s) {
    if (s == "template") return BoundarySource::templated;
    if (s == "manual") return BoundarySource::manual;
    return BoundarySource::automatic;
}

const char* matchSourceName(MatchSource m) {
    switch (m) {
    case MatchSource::xmp:
        return "xmp";
    case MatchSource::sidecar:
        return "sidecar";
    case MatchSource::file_name:
        return "file_name";
    case MatchSource::manual:
        return "manual";
    case MatchSource::none:
        return "none";
    }
    return "none";
}

// ---------------------------------------------------------------------------------------------------
// file helpers
Status writeFileAtomic(const std::filesystem::path& path, const std::string& content, bool keepBackup) {
    auto tmp = path;
    tmp += ".fk-tmp-" + randomToken(8);
    std::FILE* f = openFile(tmp, "wb");
    if (!f) return makeError(Errc::io_write, "Cannot write file", pathToUtf8(path));
    const size_t n = std::fwrite(content.data(), 1, content.size(), f);
    const bool ok = n == content.size() && std::fflush(f) == 0;
    std::fclose(f);
    std::error_code ec;
    if (!ok) {
        std::filesystem::remove(tmp, ec);
        return makeError(Errc::disk_full, "Writing the file failed (disk full?)", pathToUtf8(path));
    }
    detail::syncFile(tmp);
    if (keepBackup && std::filesystem::exists(path, ec)) {
        auto bak = path;
        bak += ".bak";
        std::filesystem::copy_file(path, bak, std::filesystem::copy_options::overwrite_existing, ec);
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return makeError(Errc::io_write, "Cannot replace the file", ec.message());
    }
    return {};
}

Result<std::string> readTextFile(const std::filesystem::path& path) {
    std::FILE* f = openFile(path, "rb");
    if (!f) {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec))
            return makeError(Errc::not_found, "File not found", pathToUtf8(path));
        return makeError(Errc::io_open, "Cannot open file", pathToUtf8(path));
    }
    std::string s;
    char buf[1 << 16];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
        s.append(buf, n);
    std::fclose(f);
    return s;
}

Result<Fingerprint> fingerprintFile(const std::filesystem::path& p, bool full, const CancelToken* cancel) {
    std::error_code ec;
    Fingerprint fp;
    fp.size = std::filesystem::file_size(p, ec);
    if (ec) return makeError(Errc::not_found, "File not found", pathToUtf8(p));
    const auto ft = std::filesystem::last_write_time(p, ec);
    if (!ec) {
        // portable file_clock → system_clock (std::chrono::clock_cast is missing from Apple's libc++)
        const auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ft - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
        const std::time_t t = std::chrono::system_clock::to_time_t(sys);
        std::tm tm{};
#if defined(_WIN32)
        gmtime_s(&tm, &t);
#else
        gmtime_r(&t, &tm);
#endif
        char b[32];
        std::strftime(b, sizeof(b), "%Y-%m-%dT%H:%M:%SZ", &tm);
        fp.mtime = b;
    }
    std::FILE* f = openFile(p, "rb");
    if (!f) return makeError(Errc::io_open, "Cannot open file", pathToUtf8(p));
    Sha256 h;
    const std::string sz = std::to_string(fp.size);
    h.update(sz.data(), sz.size());
    std::vector<uint8_t> buf(1 << 20);
    size_t n = std::fread(buf.data(), 1, buf.size(), f);
    h.update(buf.data(), n);
    if (fp.size > buf.size()) {
        const uint64_t tail = std::min<uint64_t>(buf.size(), fp.size - buf.size());
#if defined(_WIN32)
        _fseeki64(f, -static_cast<int64_t>(tail), SEEK_END);
#else
        fseeko(f, -static_cast<off_t>(tail), SEEK_END);
#endif
        n = std::fread(buf.data(), 1, static_cast<size_t>(tail), f);
        h.update(buf.data(), n);
    }
    std::fclose(f);
    fp.quickSha256 = h.finishHex();
    if (full) {
        auto fh = sha256File(p, cancel);
        if (!fh) return fh.error();
        fp.fullSha256 = *fh;
    }
    return fp;
}

FileStamp fileStamp(const std::filesystem::path& p) {
    FileStamp st;
    std::error_code ec;
    st.size = std::filesystem::file_size(p, ec);
    if (ec) return {};
    const auto t = std::filesystem::last_write_time(p, ec);
    if (ec) return {};
    st.exists = true;
    st.mtimeNs = std::chrono::duration_cast<std::chrono::nanoseconds>(t.time_since_epoch()).count();
    return st;
}

IdentityCheck verifyFileIdentity(const std::filesystem::path& p, const Fingerprint& expected,
                                 const CancelToken* cancel) {
    IdentityCheck r;
    std::error_code ec;
    if (!std::filesystem::exists(p, ec)) return {Identity::missing, "missing", false};
    const uint64_t size = std::filesystem::file_size(p, ec);
    if (ec) return {Identity::missing, "missing", false};
    if (expected.size != 0 && size != expected.size) return {Identity::changed, "size", false};
    auto quick = fingerprintFile(p, false);
    if (!quick) return {Identity::missing, "missing", false};
    if (!expected.quickSha256.empty() && quick->quickSha256 != expected.quickSha256)
        return {Identity::changed, "quick", false};
    if (!expected.fullSha256.empty()) {
        auto full = sha256File(p, cancel);
        if (!full) {
            if (cancel && cancel->cancelled()) return {Identity::same, "cancelled", false};
            return {Identity::missing, "missing", false};
        }
        r.fullChecked = true;
        if (*full != expected.fullSha256) return {Identity::changed, "full", true};
    }
    return r;
}

// ---------------------------------------------------------------------------------------------------
// JSON helpers
namespace {

json vec(Vec2 v) {
    return json::array({v.x, v.y});
}
Vec2 vecFrom(const json& j) {
    return {j.at(0).get<double>(), j.at(1).get<double>()};
}

json shapeJson(const Shape& s) {
    json j;
    if (s.kind == ShapeKind::quad) {
        j["kind"] = "quad";
        j["points"] = json::array();
        for (auto& p : s.quad.p)
            j["points"].push_back(vec(p));
    } else {
        j["kind"] = "ellipse";
        j["center"] = vec(s.ellipse.c);
        j["rx"] = s.ellipse.rx;
        j["ry"] = s.ellipse.ry;
        j["angle"] = s.ellipse.angleRad;
    }
    return j;
}

Shape shapeFrom(const json& j) {
    if (j.value("kind", "quad") == "ellipse") {
        return Shape::fromEllipse({vecFrom(j.at("center")), j.at("rx").get<double>(),
                                   j.at("ry").get<double>(), j.value("angle", 0.0)});
    }
    Quad q;
    for (int i = 0; i < 4; ++i)
        q.p[i] = vecFrom(j.at("points").at(i));
    return Shape::fromQuad(q);
}

json matJson(const Mat3& m) {
    json a = json::array();
    for (double v : m.m)
        a.push_back(v);
    return a;
}
Mat3 matFrom(const json& j) {
    Mat3 m;
    for (int i = 0; i < 9; ++i)
        m.m[i] = j.at(i).get<double>();
    return m;
}

json fpJson(const Fingerprint& f) {
    return json{{"size", f.size},
                {"mtime", f.mtime},
                {"quickSha256", f.quickSha256},
                {"fullSha256", f.fullSha256.empty() ? json(nullptr) : json(f.fullSha256)}};
}
Fingerprint fpFrom(const json& j) {
    Fingerprint f;
    f.size = j.value("size", uint64_t{0});
    f.mtime = j.value("mtime", "");
    f.quickSha256 = j.value("quickSha256", "");
    if (j.contains("fullSha256") && j["fullSha256"].is_string())
        f.fullSha256 = j["fullSha256"].get<std::string>();
    return f;
}

json strokesJson(const std::vector<MaskStroke>& ss) {
    json a = json::array();
    for (const auto& s : ss) {
        json pts = json::array();
        for (auto& p : s.points)
            pts.push_back(vec(p));
        a.push_back({{"points", pts}, {"radius", s.radiusPx}, {"add", s.add}});
    }
    return a;
}
std::vector<MaskStroke> strokesFrom(const json& a) {
    std::vector<MaskStroke> out;
    for (const auto& s : a) {
        MaskStroke m;
        for (const auto& p : s.at("points"))
            m.points.push_back(vecFrom(p));
        m.radiusPx = s.value("radius", 10.0);
        m.add = s.value("add", true);
        out.push_back(m);
    }
    return out;
}

MaskMode maskModeFrom(const std::string& s) {
    if (s == "feather") return MaskMode::feather;
    if (s == "preserve_edge") return MaskMode::preserve_edge;
    return MaskMode::hard_edge;
}

json itemJson(const ItemRecord& it) {
    json edits = json::array();
    for (const auto& e : it.edits) {
        json r = json(nullptr);
        if (!e.relinkedPath.empty()) {
            r = json{{"path", e.relinkedPath},
                     {"sha256", e.relinkedSha256},
                     {"matchedBy", e.matchedBy},
                     {"at", e.relinkedAt}};
            if (const RelinkTransform& t = e.relinkTransform; t.set) {
                r["transform"] = {{"rotate", t.rotate},
                                  {"rotateSource", t.rotateSource},
                                  {"scale", t.scale},
                                  {"lowerDepth", t.lowerDepth},
                                  {"contentScore", t.contentScore},
                                  {"gradedSize", json::array({t.gradedSize.w, t.gradedSize.h})},
                                  {"iccSource", t.iccSource},
                                  {"iccSha256", t.iccSha256}};
                if (t.gradedIccRecorded) r["transform"]["gradedIccSha256"] = t.gradedIccSha256;
            }
        }
        edits.push_back({{"editVersionId", e.editVersionId},
                         {"version", e.version},
                         {"path", e.path},
                         {"sidecar", e.sidecar},
                         {"exportedAt", e.exportedAt},
                         {"fileSha256", e.fileSha256},
                         {"format", e.format},
                         {"relinked", r}});
    }
    json outer = shapeJson(it.outer);
    outer["source"] = boundarySourceName(it.outerSource);
    outer["cornerRadiusPx"] = it.cornerRadiusPx;
    json inner = shapeJson(it.inner);
    inner["source"] = boundarySourceName(it.innerSource);
    return json{
        {"itemId", it.itemId},
        {"index", it.index},
        {"label", it.label},
        {"filmType", it.filmType},
        {"outer", outer},
        {"inner", inner},
        {"upright", it.upright},
        {"confidence", it.confidence},
        {"outerConfidence", it.outerConfidence},
        {"innerConfidence", it.innerConfidence},
        {"reasons", it.reasons},
        {"reviewed", it.reviewed},
        {"excluded", it.excluded},
        {"edits", edits},
        {"composite",
         {{"maskMode", maskModeName(it.composite.maskMode)},
          {"featherPx", it.composite.featherPx},
          {"preserveEdgePx", it.composite.preserveEdgePx},
          {"strokes", strokesJson(it.composite.strokes)},
          {"lastOutput", it.composite.lastOutput.empty() ? json(nullptr) : json(it.composite.lastOutput)},
          {"lastOutputAt", it.composite.lastOutputAt}}},
        {"finalExport", it.finalExport},
        {"history", {{"undo", it.undo}, {"redo", it.redo}}}};
}

ItemRecord itemFrom(const json& j) {
    ItemRecord it;
    it.itemId = j.at("itemId").get<std::string>();
    it.index = j.value("index", 0);
    it.label = j.value("label", "");
    it.filmType = j.value("filmType", "");
    it.outer = shapeFrom(j.at("outer"));
    it.outerSource = boundarySourceFrom(j.at("outer").value("source", "auto"));
    it.cornerRadiusPx = j.at("outer").value("cornerRadiusPx", 0.0);
    it.inner = shapeFrom(j.at("inner"));
    it.innerSource = boundarySourceFrom(j.at("inner").value("source", "auto"));
    it.upright = j.value("upright", 0);
    it.confidence = j.value("confidence", 0.0);
    it.outerConfidence = j.value("outerConfidence", 0.0);
    it.innerConfidence = j.value("innerConfidence", 0.0);
    it.reasons = j.value("reasons", std::vector<std::string>{});
    it.reviewed = j.value("reviewed", false);
    it.excluded = j.value("excluded", false);
    for (const auto& e : j.value("edits", json::array())) {
        EditVersion ev;
        ev.editVersionId = e.value("editVersionId", "");
        ev.version = e.value("version", 0);
        ev.path = e.value("path", "");
        ev.sidecar = e.value("sidecar", "");
        ev.exportedAt = e.value("exportedAt", "");
        ev.fileSha256 = e.value("fileSha256", "");
        ev.format = e.value("format", "");
        if (e.contains("relinked") && e["relinked"].is_object()) {
            ev.relinkedPath = e["relinked"].value("path", "");
            ev.relinkedSha256 = e["relinked"].value("sha256", "");
            ev.matchedBy = e["relinked"].value("matchedBy", "");
            ev.relinkedAt = e["relinked"].value("at", "");
            if (e["relinked"].contains("transform") && e["relinked"]["transform"].is_object()) {
                const auto& t = e["relinked"]["transform"];
                RelinkTransform& x = ev.relinkTransform;
                x.set = true;
                x.rotate = ((t.value("rotate", 0) % 4) + 4) % 4;
                x.rotateSource = t.value("rotateSource", "");
                x.scale = t.value("scale", false);
                x.lowerDepth = t.value("lowerDepth", false);
                x.contentScore = t.value("contentScore", 0.0);
                x.iccSource = t.value("iccSource", "");
                x.iccSha256 = t.value("iccSha256", "");
                x.gradedIccRecorded = t.contains("gradedIccSha256");
                x.gradedIccSha256 = t.value("gradedIccSha256", "");
                if (t.contains("gradedSize") && t["gradedSize"].is_array() && t["gradedSize"].size() == 2)
                    x.gradedSize = {t["gradedSize"][0].get<int64_t>(), t["gradedSize"][1].get<int64_t>()};
            }
        }
        it.edits.push_back(ev);
    }
    if (j.contains("composite")) {
        const auto& c = j["composite"];
        it.composite.maskMode = maskModeFrom(c.value("maskMode", "hard_edge"));
        it.composite.featherPx = c.value("featherPx", 0.0);
        it.composite.preserveEdgePx = c.value("preserveEdgePx", 0.0);
        it.composite.strokes = strokesFrom(c.value("strokes", json::array()));
        if (c.contains("lastOutput") && c["lastOutput"].is_string())
            it.composite.lastOutput = c["lastOutput"].get<std::string>();
        it.composite.lastOutputAt = c.value("lastOutputAt", "");
    }
    it.finalExport = j.value("finalExport", "");
    if (j.contains("history")) {
        it.undo = j["history"].value("undo", std::vector<std::string>{});
        it.redo = j["history"].value("redo", std::vector<std::string>{});
    }
    return it;
}

json scanJson(const ScanRecord& s) {
    json items = json::array();
    for (const auto& it : s.items)
        items.push_back(itemJson(it));
    return json{{"scanId", s.scanId},
                {"path", s.path},
                {"relPath", s.relPath},
                {"fingerprint", fpJson(s.fingerprint)},
                {"info",
                 {{"width", s.info.width},
                  {"height", s.info.height},
                  {"channels", s.info.channels},
                  {"bits", s.info.bitsPerSample},
                  {"compression", compressionName(s.info.compression)},
                  {"dpiX", s.info.dpiX},
                  {"dpiY", s.info.dpiY},
                  {"iccDescription", s.info.iccDescription},
                  {"iccSha256", s.iccSha256}}},
                {"detection",
                 {{"at", s.detectedAt},
                  {"expectedCount", s.expectedCount},
                  {"dpiUsed", s.dpiUsed},
                  {"proxyScale", s.proxyScale},
                  {"warnings", s.warnings}}},
                {"items", items}};
}

ScanRecord scanFrom(const json& j) {
    ScanRecord s;
    s.scanId = j.at("scanId").get<std::string>();
    s.path = j.value("path", "");
    s.relPath = j.value("relPath", "");
    s.fingerprint = fpFrom(j.value("fingerprint", json::object()));
    const auto& in = j.value("info", json::object());
    s.info.width = in.value("width", int64_t{0});
    s.info.height = in.value("height", int64_t{0});
    s.info.channels = in.value("channels", 0);
    s.info.bitsPerSample = in.value("bits", 0);
    const std::string comp = in.value("compression", "none");
    for (Compression c : {Compression::none, Compression::lzw, Compression::deflate, Compression::packbits,
                          Compression::jpeg, Compression::other})
        if (comp == compressionName(c)) s.info.compression = c;
    s.info.dpiX = in.value("dpiX", 0.0);
    s.info.dpiY = in.value("dpiY", 0.0);
    s.info.iccDescription = in.value("iccDescription", "");
    s.iccSha256 = in.value("iccSha256", "");
    const auto& d = j.value("detection", json::object());
    s.detectedAt = d.value("at", "");
    s.expectedCount = d.value("expectedCount", 4);
    s.dpiUsed = d.value("dpiUsed", 0.0);
    s.proxyScale = d.value("proxyScale", 1);
    s.warnings = d.value("warnings", std::vector<std::string>{});
    for (const auto& it : j.value("items", json::array()))
        s.items.push_back(itemFrom(it));
    return s;
}

int schemaVersion(const std::string& schema, const std::string& prefix) {
    if (schema.rfind(prefix, 0) != 0) return -1;
    try {
        return std::stoi(schema.substr(prefix.size()));
    } catch (...) {
        return -1;
    }
}

const std::vector<std::string> kKnownProjectKeys = {"schema",        "projectId",  "name",     "createdAt",
                                                    "modifiedAt",    "app",        "settings", "scans",
                                                    "exportRecords", "iccProfiles"};

} // namespace

// ---------------------------------------------------------------------------------------------------
Project newProject(const std::string& name) {
    Project p;
    p.projectId = newUuid();
    p.name = name;
    p.createdAt = p.modifiedAt = isoDateTimeNowUtc();
    return p;
}

std::string projectToJson(const Project& p) {
    json j;
    j["schema"] = "fk.project/" + std::to_string(kProjectSchemaVersion);
    j["projectId"] = p.projectId;
    j["name"] = p.name;
    j["createdAt"] = p.createdAt;
    j["modifiedAt"] = p.modifiedAt;
    j["app"] = {{"name", product::kName}, {"version", product::kVersion}};
    j["settings"] = {
        {"expectedCount", p.settings.expectedCount}, {"safetyMarginPx", p.settings.safetyMarginPx},
        {"exportFolder", p.settings.exportFolder},   {"exportFormat", p.settings.exportFormat},
        {"exportBits", p.settings.exportBits},       {"exportCompression", p.settings.exportCompression},
        {"colorPolicy", p.settings.colorPolicy},     {"privacy", p.settings.privacy}};
    j["scans"] = json::array();
    for (const auto& s : p.scans)
        j["scans"].push_back(scanJson(s));
    // export records (UX-07, U003): the geometry, colour and file identity of every export, so relinking
    // needs no sidecar next to the graded file. A top-level key: older versions keep it verbatim (unknown
    // keys).
    if (!p.exportRecords.empty() || !p.unparsedExportRecords.empty()) {
        json recs = json::array();
        for (const auto& r : p.exportRecords)
            recs.push_back(sidecarJson(r));
        for (const auto& raw : p.unparsedExportRecords) {
            try {
                recs.push_back(json::parse(raw));
            } catch (...) {
            }
        }
        j["exportRecords"] = std::move(recs);
    }
    // FK-003: profiles chosen to interpret graded files (a new top-level key; earlier versions keep it
    // verbatim)
    if (!p.iccProfiles.empty()) {
        json profiles = json::array();
        for (const auto& [sha, bytes] : p.iccProfiles)
            profiles.push_back(
                {{"sha256", sha}, {"description", iccDescription(bytes)}, {"data", base64Encode(bytes)}});
        j["iccProfiles"] = std::move(profiles);
    }
    if (!p.unknownJson.empty()) {
        try {
            const json extra = json::parse(p.unknownJson);
            for (auto it = extra.begin(); it != extra.end(); ++it)
                j[it.key()] = it.value();
        } catch (...) {
        }
    }
    return j.dump(2);
}

Result<Project> projectFromJson(const std::string& text, OpenMode* mode) {
    json j;
    try {
        j = json::parse(text);
    } catch (const std::exception& e) {
        return makeError(Errc::parse_error, "The project file is damaged", e.what());
    }
    try {
        const int v = schemaVersion(j.value("schema", ""), "fk.project/");
        if (v < 1) return makeError(Errc::parse_error, "Not a FrameKeeper project file");
        if (mode) *mode = v > kProjectSchemaVersion ? OpenMode::read_only : OpenMode::read_write;
        // (v1 is the first schema; future migrations vN→vN+1 run here.)
        Project p;
        p.schema = j["schema"].get<std::string>();
        p.projectId = j.at("projectId").get<std::string>();
        p.name = j.value("name", "");
        p.createdAt = j.value("createdAt", "");
        p.modifiedAt = j.value("modifiedAt", "");
        if (j.contains("settings")) {
            const auto& s = j["settings"];
            p.settings.expectedCount = s.value("expectedCount", 4);
            p.settings.safetyMarginPx = s.value("safetyMarginPx", 1.0);
            p.settings.exportFolder = s.value("exportFolder", "");
            p.settings.exportFormat = s.value("exportFormat", "tif");
            p.settings.exportBits = s.value("exportBits", 0);
            p.settings.exportCompression = s.value("exportCompression", "lzw");
            p.settings.colorPolicy = s.value("colorPolicy", "keep");
            p.settings.privacy = s.value("privacy", "keep_all_safe");
        }
        for (const auto& s : j.value("scans", json::array()))
            p.scans.push_back(scanFrom(s));
        for (const auto& r : j.value("exportRecords", json::array())) {
            auto rec = sidecarFromJson(r);
            if (rec)
                p.exportRecords.push_back(std::move(*rec));
            else
                p.unparsedExportRecords.push_back(r.dump()); // kept verbatim (e.g. a newer record schema)
        }
        for (const auto& e : j.value("iccProfiles", json::array())) {
            if (!e.is_object()) continue;
            auto bytes = base64Decode(e.value("data", ""));
            // kept only when the bytes are what the key says (a damaged entry is dropped, never guessed)
            if (bytes && !bytes->empty() && sha256Hex(bytes->data(), bytes->size()) == e.value("sha256", ""))
                p.iccProfiles[e.value("sha256", "")] = std::move(*bytes);
        }
        json extra = json::object();
        for (auto it = j.begin(); it != j.end(); ++it)
            if (std::find(kKnownProjectKeys.begin(), kKnownProjectKeys.end(), it.key()) ==
                kKnownProjectKeys.end())
                extra[it.key()] = it.value();
        if (!extra.empty()) p.unknownJson = extra.dump();
        return p;
    } catch (const std::exception& e) {
        return makeError(Errc::parse_error, "The project file is damaged", e.what());
    }
}

Result<Project> loadProject(const std::filesystem::path& path, OpenMode* mode) {
    auto text = readTextFile(path);
    if (!text) return text.error();
    auto p = projectFromJson(*text, mode);
    if (!p) return p.error();
    resolveScanPaths(*p, path.parent_path());
    importSidecarRecords(*p);
    return p;
}

int importSidecarRecords(Project& p) {
    // Projects from before U003 kept export geometry only in `.fk.json` files beside the exports. Copy each
    // one that is still readable into the project so relinking no longer depends on it (saved with the
    // project).
    int n = 0;
    for (const auto& s : p.scans)
        for (const auto& it : s.items)
            for (const auto& ev : it.edits) {
                if (ev.sidecar.empty() || findExportRecord(p, ev.editVersionId)) continue;
                auto sc = loadSidecar(pathFromUtf8(ev.sidecar));
                if (!sc || sc->editVersionId != ev.editVersionId) continue;
                p.exportRecords.push_back(std::move(*sc));
                ++n;
            }
    return n;
}

Status saveProject(const Project& p, const std::filesystem::path& path) {
    Project copy = p;
    copy.modifiedAt = isoDateTimeNowUtc();
    // keep relative paths current for portability
    for (auto& s : copy.scans) {
        std::error_code ec;
        auto rel = std::filesystem::relative(pathFromUtf8(s.path), path.parent_path(), ec);
        if (!ec && !rel.empty()) s.relPath = pathToUtf8(rel.generic_u8string().empty() ? rel : rel);
    }
    return writeFileAtomic(path, projectToJson(copy), true);
}

void resolveScanPaths(Project& p, const std::filesystem::path& projectDir) {
    for (auto& s : p.scans) {
        std::error_code ec;
        if (std::filesystem::exists(pathFromUtf8(s.path), ec)) {
            s.missing = false;
            // cheap here (no reading): a different size is a different file; content is verified by
            // verifyScans
            const uint64_t size = std::filesystem::file_size(pathFromUtf8(s.path), ec);
            if (!ec && s.fingerprint.size != 0 && size != s.fingerprint.size) {
                s.changed = true;
                s.changeReason = "size";
            }
            continue;
        }
        if (!s.relPath.empty()) {
            const auto cand = projectDir / pathFromUtf8(s.relPath);
            if (std::filesystem::exists(cand, ec)) {
                s.path = pathToUtf8(std::filesystem::weakly_canonical(cand, ec));
                s.missing = false;
                continue;
            }
        }
        s.missing = true;
    }
}

int verifyScans(Project& p, const CancelToken* cancel) {
    int n = 0;
    for (auto& s : p.scans) {
        if (s.missing || (cancel && cancel->cancelled())) continue;
        const IdentityCheck c = verifyFileIdentity(pathFromUtf8(s.path), s.fingerprint, cancel);
        s.changed = c.state == Identity::changed;
        s.changeReason = s.changed ? c.reason : std::string();
        if (c.state == Identity::missing) s.missing = true;
        if (s.changed) ++n;
    }
    return n;
}

int relocateScans(Project& p, const std::filesystem::path& projectDir, const std::filesystem::path& folder) {
    resolveScanPaths(p, projectDir);
    int found = 0;
    std::error_code ec;
    for (auto& s : p.scans) {
        if (!s.missing && !s.changed) continue; // FK-007: a replaced file is looked for like a missing one
        const auto name = pathFromUtf8(s.path).filename();
        std::filesystem::path hit;
        const auto direct = folder / name;
        if (std::filesystem::exists(direct, ec)) {
            auto fp = fingerprintFile(direct);
            if (fp && *fp == s.fingerprint) hit = direct;
        }
        if (hit.empty()) {
            for (auto it = std::filesystem::recursive_directory_iterator(folder, ec);
                 it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
                if (ec) break;
                if (!it->is_regular_file(ec) || it->file_size(ec) != s.fingerprint.size) continue;
                auto fp = fingerprintFile(it->path());
                if (fp && *fp == s.fingerprint) {
                    hit = it->path();
                    break;
                }
            }
        }
        if (!hit.empty()) {
            s.path = pathToUtf8(hit);
            s.missing = false;
            s.changed = false;
            s.changeReason.clear();
            ++found;
        }
    }
    return found;
}

ItemRecord itemFromDetection(const DetectedItem& d, int index) {
    ItemRecord it;
    it.itemId = newUuid();
    it.index = index;
    it.label = itemLabel(index);
    it.filmType = d.filmTypeId;
    it.outer = d.outer;
    it.cornerRadiusPx = d.cornerRadiusPx;
    it.inner = d.inner;
    it.upright = d.upright;
    it.confidence = d.confidence;
    it.outerConfidence = d.outerConfidence;
    it.innerConfidence = d.innerConfidence;
    it.reasons = d.reasons;
    it.reviewed = d.confidence >= kLowConfidence;
    const bool templ = std::find(d.reasons.begin(), d.reasons.end(), "template") != d.reasons.end();
    it.outerSource = it.innerSource = templ ? BoundarySource::templated : BoundarySource::automatic;
    return it;
}

ScanRecord* findScan(Project& p, const std::string& scanId) {
    for (auto& s : p.scans)
        if (s.scanId == scanId) return &s;
    return nullptr;
}

ItemRecord* findItem(Project& p, const std::string& itemId, ScanRecord** scanOut) {
    for (auto& s : p.scans)
        for (auto& it : s.items)
            if (it.itemId == itemId) {
                if (scanOut) *scanOut = &s;
                return &it;
            }
    return nullptr;
}

// ---------------------------------------------------------------------------------------------------
// sidecar
Sidecar makeSidecar(const Project& p, const ScanRecord& s, const ItemRecord& it, const EditVersion& ev,
                    const ExportReport& rep) {
    Sidecar sc;
    sc.projectId = p.projectId;
    sc.scanId = s.scanId;
    sc.itemId = it.itemId;
    sc.editVersionId = ev.editVersionId;
    sc.label = it.label;
    sc.version = ev.version;
    sc.scanFile = pathToUtf8(pathFromUtf8(s.path).filename());
    sc.scanFingerprint = s.fingerprint;
    sc.inner = it.inner;
    sc.outer = it.outer;
    sc.geometry = rep.geometry;
    sc.sourceSize = {s.info.width, s.info.height};
    sc.exportSize = rep.size;
    sc.bits = rep.bits;
    sc.channels = rep.channels;
    sc.iccDescription = rep.iccDescription;
    sc.iccSha256 = rep.iccSha256;
    sc.dpiX = rep.dpiX;
    sc.dpiY = rep.dpiY;
    sc.fileName = pathToUtf8(rep.output.filename());
    sc.fileSha256 = rep.sha256;
    sc.signature = rep.signature;
    sc.software = productNameAndVersion();
    sc.exportedAt = ev.exportedAt.empty() ? isoDateTimeNowUtc() : ev.exportedAt;
    return sc;
}

namespace {
json sidecarJson(const Sidecar& s) {
    json j;
    j["schema"] = "fk.sidecar/" + std::to_string(kSidecarSchemaVersion);
    j["projectId"] = s.projectId;
    j["scanId"] = s.scanId;
    j["scanFingerprint"] = fpJson(s.scanFingerprint);
    j["scanFile"] = s.scanFile;
    j["itemId"] = s.itemId;
    j["editVersionId"] = s.editVersionId;
    j["version"] = s.version;
    j["label"] = s.label;
    j["inner"] = shapeJson(s.inner);
    j["outer"] = shapeJson(s.outer);
    j["geometry"] = {{"mode", geometryModeName(s.geometry.mode)},
                     {"upright", s.geometry.upright},
                     {"angle", s.geometry.angleRad},
                     {"scale", s.geometry.scale},
                     {"resampled", s.geometry.resampled},
                     {"outToScan", matJson(s.geometry.outToScan)},
                     {"scanToOut", matJson(s.geometry.scanToOut)},
                     {"sourceRect", json::array({s.geometry.sourceRect.x, s.geometry.sourceRect.y,
                                                 s.geometry.sourceRect.w, s.geometry.sourceRect.h})}};
    j["sourceSize"] = json::array({s.sourceSize.w, s.sourceSize.h});
    j["exportSize"] = json::array({s.exportSize.w, s.exportSize.h});
    j["bits"] = s.bits;
    j["channels"] = s.channels;
    j["icc"] = {{"description", s.iccDescription}, {"sha256", s.iccSha256}};
    j["dpi"] = json::array({s.dpiX, s.dpiY});
    j["file"] = {{"name", s.fileName}, {"sha256", s.fileSha256}};
    j["signature"] = encodeSignature(s.signature);
    j["software"] = s.software;
    j["exportedAt"] = s.exportedAt;
    if (s.origin != "export") j["origin"] = s.origin; // absent = export (files of earlier versions)
    return j;
}

Result<Sidecar> sidecarFromJson(const json& j) {
    try {
        const int v = schemaVersion(j.value("schema", ""), "fk.sidecar/");
        if (v < 1) return makeError(Errc::parse_error, "Not a FrameKeeper sidecar");
        if (v > kSidecarSchemaVersion)
            return makeError(Errc::version_too_new, "Sidecar was written by a newer version");
        Sidecar s;
        s.projectId = j.at("projectId").get<std::string>();
        s.scanId = j.at("scanId").get<std::string>();
        s.scanFingerprint = fpFrom(j.value("scanFingerprint", json::object()));
        s.scanFile = j.value("scanFile", "");
        s.itemId = j.at("itemId").get<std::string>();
        s.editVersionId = j.value("editVersionId", "");
        s.version = j.value("version", 0);
        s.label = j.value("label", "");
        s.inner = shapeFrom(j.at("inner"));
        s.outer = shapeFrom(j.at("outer"));
        const auto& g = j.at("geometry");
        const std::string mode = g.value("mode", "pixel_crop");
        s.geometry.mode = mode == "straightened"    ? GeometryMode::straightened
                          : mode == "standard_size" ? GeometryMode::standard_size
                                                    : GeometryMode::pixel_crop;
        s.geometry.upright = g.value("upright", 0);
        s.geometry.angleRad = g.value("angle", 0.0);
        s.geometry.scale = g.value("scale", 1.0);
        s.geometry.resampled = g.value("resampled", false);
        s.geometry.outToScan = matFrom(g.at("outToScan"));
        s.geometry.scanToOut = matFrom(g.at("scanToOut"));
        const auto& sr = g.at("sourceRect");
        s.geometry.sourceRect = {sr.at(0).get<int64_t>(), sr.at(1).get<int64_t>(), sr.at(2).get<int64_t>(),
                                 sr.at(3).get<int64_t>()};
        s.sourceSize = {j.at("sourceSize").at(0).get<int64_t>(), j.at("sourceSize").at(1).get<int64_t>()};
        s.exportSize = {j.at("exportSize").at(0).get<int64_t>(), j.at("exportSize").at(1).get<int64_t>()};
        s.geometry.outSize = s.exportSize;
        s.bits = j.value("bits", 0);
        s.channels = j.value("channels", 0);
        s.iccDescription = j.value("icc", json::object()).value("description", "");
        s.iccSha256 = j.value("icc", json::object()).value("sha256", "");
        s.dpiX = j.at("dpi").at(0).get<double>();
        s.dpiY = j.at("dpi").at(1).get<double>();
        s.fileName = j.value("file", json::object()).value("name", "");
        s.fileSha256 = j.value("file", json::object()).value("sha256", "");
        if (j.contains("signature")) {
            auto sig = decodeSignature(j["signature"].get<std::string>());
            if (sig) s.signature = *sig;
        }
        s.software = j.value("software", "");
        s.exportedAt = j.value("exportedAt", "");
        s.origin = j.value("origin", "export");
        return s;
    } catch (const std::exception& e) {
        return makeError(Errc::parse_error, "The sidecar file is damaged", e.what());
    }
}
} // namespace

Status saveSidecar(const Sidecar& s, const std::filesystem::path& path) {
    return writeFileAtomic(path, sidecarJson(s).dump(2), false);
}

Result<Sidecar> loadSidecar(const std::filesystem::path& path) {
    auto text = readTextFile(path);
    if (!text) return text.error();
    json j;
    try {
        j = json::parse(*text);
    } catch (const std::exception& e) {
        return makeError(Errc::parse_error, "The sidecar file is damaged", e.what());
    }
    return sidecarFromJson(j);
}

const Sidecar* findExportRecord(const Project& p, const std::string& editVersionId) {
    if (editVersionId.empty()) return nullptr;
    for (const auto& r : p.exportRecords)
        if (r.editVersionId == editVersionId) return &r;
    return nullptr;
}

Result<Sidecar> exportRecordFor(const Project& p, const EditVersion& ev) {
    if (const Sidecar* r = findExportRecord(p, ev.editVersionId)) return *r;
    if (!ev.sidecar.empty()) return loadSidecar(pathFromUtf8(ev.sidecar));
    return makeError(Errc::not_found, "No export record for this file in the project");
}

std::string sidecarPathFor(const std::string& imagePathUtf8) {
    const auto p = pathFromUtf8(imagePathUtf8);
    return pathToUtf8(p.parent_path() / pathFromUtf8(pathToUtf8(p.stem()) + product::kSidecarSuffix));
}

// ---------------------------------------------------------------------------------------------------
// relink
RelinkCandidate findMatch(const std::filesystem::path& edited, const Project& p) {
    RelinkCandidate c;
    auto fill = [&](const std::string& itemId, const std::string& evId, MatchSource src) -> bool {
        for (const auto& s : p.scans)
            for (const auto& it : s.items) {
                if (it.itemId != itemId) continue;
                c.source = src;
                c.scanId = s.scanId;
                c.itemId = it.itemId;
                c.editVersionId = evId;
                const EditVersion* ev = nullptr;
                for (const auto& e : it.edits)
                    if (e.editVersionId == evId) ev = &e;
                if (!ev && !it.edits.empty()) ev = &it.edits.back();
                if (ev) {
                    if (c.editVersionId.empty()) c.editVersionId = ev->editVersionId;
                    auto sc = exportRecordFor(p, *ev);
                    if (sc) c.sidecar = *sc;
                }
                return true;
            }
        return false;
    };
    // 1. XMP
    auto xmp = readXmpPacket(edited);
    if (xmp && !xmp->empty()) {
        const XmpIds ids = parseXmpIds(*xmp);
        if (!ids.itemId.empty() && fill(ids.itemId, ids.editVersionId, MatchSource::xmp)) return c;
    }
    // 2. sidecar next to the file (editor suffixes stripped)
    const std::string stem = pathToUtf8(edited.stem());
    for (const std::string& cand : sidecarStemCandidates(stem)) {
        const auto sp = edited.parent_path() / pathFromUtf8(cand + product::kSidecarSuffix);
        auto sc = loadSidecar(sp);
        if (!sc) continue;
        if (fill(sc->itemId, sc->editVersionId, MatchSource::sidecar)) {
            c.sidecar = *sc;
            return c;
        }
    }
    // 3. file name pattern
    if (auto e = parseEditFileName(stem)) {
        for (const auto& s : p.scans) {
            if (pathToUtf8(pathFromUtf8(s.path).stem()) != e->scanStem) continue;
            for (const auto& it : s.items) {
                if (it.index != e->itemIndex) continue;
                std::string evId;
                for (const auto& ev : it.edits)
                    if (ev.version == e->version) evId = ev.editVersionId;
                fill(it.itemId, evId, MatchSource::file_name);
                return c;
            }
        }
    }
    return c;
}

RelinkCheck checkEdited(const Sidecar& sc, const ImageBuffer& edited, const ImageInfo& info) {
    RelinkCheck r;
    const SizeI e{edited.width(), edited.height()};
    const SizeI ex = sc.exportSize;
    const CompositeCheck g = checkEditedGeometry(ex, edited, sc.bits);
    r.sizeMatches = g.sizeMatches;
    r.aspectMatches = g.aspectMatches;
    r.depthLower = edited.bits() < sc.bits;
    r.iccMissing = info.icc.empty() && !sc.iccSha256.empty();
    r.iccInvalid = !info.icc.empty() && !Profile::fromIcc(info.icc).ok();
    r.iccDiffers = !info.icc.empty() && sha256Hex(info.icc.data(), info.icc.size()) != sc.iccSha256;
    // turns whose size fits: same orientation {0,2}, swapped {1,3}, both for a square image area
    auto fits = [&](SizeI t) {
        if (e.w <= 0 || e.h <= 0 || t.w <= 0 || t.h <= 0) return false;
        return std::abs((static_cast<double>(e.w) / e.h) / (static_cast<double>(t.w) / t.h) - 1.0) < 0.005;
    };
    if (fits(ex)) r.allowedRotations.insert(r.allowedRotations.end(), {0, 2});
    if (fits({ex.h, ex.w})) r.allowedRotations.insert(r.allowedRotations.end(), {1, 3});
    std::sort(r.allowedRotations.begin(), r.allowedRotations.end());
    const Signature es = computeSignature(edited);
    for (int k = 0; k < 4; ++k)
        r.rotationScores[static_cast<size_t>(k)] = compareSignatures(rotateSignature(sc.signature, k), es);
    std::vector<int> cand = r.allowedRotations;
    if (cand.empty()) cand = {0, 1, 2, 3};
    int bestK = cand.front();
    for (int k : cand)
        if (r.rotationScores[static_cast<size_t>(k)] > r.rotationScores[static_cast<size_t>(bestK)])
            bestK = k;
    double second = -2;
    for (int k : cand)
        if (k != bestK) second = std::max(second, r.rotationScores[static_cast<size_t>(k)]);
    r.suggestedRotate = bestK;
    r.signatureScore = r.rotationScores[static_cast<size_t>(bestK)];
    r.signaturePass = r.signatureScore >= kSignatureMatchThreshold;
    r.rotationReliable = r.signaturePass && r.signatureScore - second >= kOrientationMargin;
    if (r.allowedRotations.empty()) {
        r.actionsNeeded.push_back("manual_alignment");
    } else {
        if (!r.rotationReliable)
            r.actionsNeeded.push_back("choose_rotation");
        else if (bestK != 0)
            r.actionsNeeded.push_back("rotate");
        const SizeI target = (bestK & 1) ? SizeI{ex.h, ex.w} : ex;
        if (!(e == target)) r.actionsNeeded.push_back("scale");
    }
    if (r.depthLower) r.actionsNeeded.push_back("accept_lower_depth");
    if (r.iccMissing || r.iccInvalid) r.actionsNeeded.push_back("choose_icc");
    if (!r.signaturePass) r.actionsNeeded.push_back("confirm_content");
    return r;
}

// ---------------------------------------------------------------------------------------------------
// templates
Status saveTemplate(const Template& t, const std::filesystem::path& path) {
    json j;
    j["schema"] = "fk.template/" + std::to_string(kTemplateSchemaVersion);
    j["templateId"] = t.templateId.empty() ? newUuid() : t.templateId;
    j["name"] = t.name;
    j["createdDpi"] = t.createdDpi;
    j["scanSize"] = json::array({t.scanSize.w, t.scanSize.h});
    j["slots"] = json::array();
    for (const auto& s : t.slots) {
        json outer = json::array();
        for (auto& p : s.outer)
            outer.push_back(vec(p));
        j["slots"].push_back(
            {{"index", s.index}, {"filmType", s.filmType}, {"outer", outer}, {"inner", shapeJson(s.inner)}});
    }
    return writeFileAtomic(path, j.dump(2), false);
}

Result<Template> loadTemplate(const std::filesystem::path& path, OpenMode* mode) {
    auto text = readTextFile(path);
    if (!text) return text.error();
    try {
        const json j = json::parse(*text);
        const int v = schemaVersion(j.value("schema", ""), "fk.template/");
        if (v < 1) return makeError(Errc::parse_error, "Not a FrameKeeper template");
        if (mode) *mode = v > kTemplateSchemaVersion ? OpenMode::read_only : OpenMode::read_write;
        Template t;
        t.schema = j["schema"].get<std::string>();
        t.templateId = j.value("templateId", "");
        t.name = j.value("name", "");
        t.createdDpi = j.value("createdDpi", 0.0);
        t.scanSize = {j.at("scanSize").at(0).get<int64_t>(), j.at("scanSize").at(1).get<int64_t>()};
        for (const auto& s : j.at("slots")) {
            TemplateSlot sl;
            sl.index = s.value("index", 0);
            sl.filmType = s.value("filmType", "");
            for (int i = 0; i < 4; ++i)
                sl.outer[i] = vecFrom(s.at("outer").at(i));
            sl.inner = shapeFrom(s.at("inner"));
            t.slots.push_back(sl);
        }
        if (t.slots.empty()) return makeError(Errc::parse_error, "Template has no slots");
        return t;
    } catch (const std::exception& e) {
        return makeError(Errc::parse_error, "The template file is damaged", e.what());
    }
}

} // namespace fk
