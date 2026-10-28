#include "cli_common.hpp"

#include "fk/image_io.hpp"

#include <algorithm>
#include <cmath>

namespace fkcli {

bool Args::has(const std::string& k) const {
    return std::any_of(opts.begin(), opts.end(), [&](auto& p) { return p.first == k; });
}
std::string Args::get(const std::string& k, const std::string& def) const {
    for (auto& p : opts)
        if (p.first == k) return p.second;
    return def;
}
double Args::num(const std::string& k, double def) const {
    const std::string v = get(k);
    if (v.empty()) return def;
    try {
        return std::stod(v);
    } catch (...) {
        return def;
    }
}
Args Args::parse(const std::vector<std::string>& a) {
    Args r;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].rfind("--", 0) == 0) {
            const std::string k = a[i].substr(2);
            if (i + 1 < a.size() && a[i + 1].rfind("--", 0) != 0)
                r.opts.push_back({k, a[++i]});
            else
                r.opts.push_back({k, "1"});
        } else {
            r.positional.push_back(a[i]);
        }
    }
    return r;
}

json shapeToJson(const fk::Shape& s) {
    json j;
    if (s.kind == fk::ShapeKind::quad) {
        j["kind"] = "quad";
        j["points"] = json::array();
        for (auto& p : s.quad.p)
            j["points"].push_back({p.x, p.y});
    } else {
        j["kind"] = "ellipse";
        j["center"] = {s.ellipse.c.x, s.ellipse.c.y};
        j["rx"] = s.ellipse.rx;
        j["ry"] = s.ellipse.ry;
        j["angle"] = s.ellipse.angleRad;
    }
    return j;
}

json detectionToJson(const fk::DetectionResult& r) {
    json j;
    j["dpiUsed"] = r.dpiUsed;
    j["proxyScale"] = r.proxyScale;
    j["warnings"] = r.warnings;
    j["secondsDetect"] = r.secondsDetect;
    j["secondsRefine"] = r.secondsRefine;
    j["items"] = json::array();
    for (const auto& it : r.items) {
        j["items"].push_back({{"outer", shapeToJson(it.outer)},
                              {"inner", shapeToJson(it.inner)},
                              {"filmType", it.filmTypeId},
                              {"upright", it.upright},
                              {"confidence", it.confidence},
                              {"outerConfidence", it.outerConfidence},
                              {"innerConfidence", it.innerConfidence},
                              {"contrast", it.contrast},
                              {"refineOffsetPx", it.refineOffsetPx},
                              {"reasons", it.reasons}});
    }
    return j;
}

json infoToJson(const fk::ImageInfo& i) {
    return json{{"width", i.width},
                {"height", i.height},
                {"channels", i.channels},
                {"bits", i.bitsPerSample},
                {"alpha", i.hasAlpha},
                {"photometric", fk::photometricName(i.photometric)},
                {"compression", fk::compressionName(i.compression)},
                {"predictor", i.predictor},
                {"tiled", i.tiled},
                {"tileWidth", i.tileWidth},
                {"tileHeight", i.tileHeight},
                {"rowsPerStrip", i.rowsPerStrip},
                {"bigTiff", i.bigTiff},
                {"planarSeparate", i.planarSeparate},
                {"dpiX", i.dpiX},
                {"dpiY", i.dpiY},
                {"iccDescription", i.iccDescription},
                {"iccBytes", i.icc.size()},
                {"orientation", i.orientation},
                {"fileSize", i.fileSize},
                {"pages", i.pageCount}};
}

namespace {
void plot(fk::ImageBuffer& img, int64_t x, int64_t y, const uint8_t c[3]) {
    if (x < 0 || y < 0 || x >= img.width() || y >= img.height()) return;
    uint8_t* p = img.row(y) + x * 3;
    p[0] = c[0];
    p[1] = c[1];
    p[2] = c[2];
}
void line(fk::ImageBuffer& img, fk::Vec2 a, fk::Vec2 b, const uint8_t c[3]) {
    const double len = std::max(1.0, fk::norm(b - a));
    for (double t = 0; t <= len; t += 0.5) {
        const fk::Vec2 p = a + (b - a) * (t / len);
        plot(img, static_cast<int64_t>(p.x), static_cast<int64_t>(p.y), c);
    }
}
void drawShape(fk::ImageBuffer& img, const fk::Shape& s, double scale, const uint8_t c[3]) {
    if (s.kind == fk::ShapeKind::quad) {
        for (int i = 0; i < 4; ++i)
            line(img, s.quad.p[i] * scale, s.quad.p[(i + 1) % 4] * scale, c);
    } else {
        fk::Vec2 prev{};
        for (int k = 0; k <= 180; ++k) {
            const double t = k * 2 * 3.14159265358979 / 180;
            const fk::Vec2 p{s.ellipse.c.x + s.ellipse.rx * std::cos(t),
                             s.ellipse.c.y + s.ellipse.ry * std::sin(t)};
            if (k) line(img, prev * scale, p * scale, c);
            prev = p;
        }
    }
}
} // namespace

bool writeOverlay(const std::filesystem::path& out, const fk::Proxy& proxy,
                  const std::vector<fk::DetectedItem>& items,
                  const std::vector<std::vector<fk::Vec2>>* gtOuter) {
    auto r = fk::ImageBuffer::create(proxy.width, proxy.height, 3, 8);
    if (!r) return false;
    fk::ImageBuffer img = std::move(r.value());
    for (int64_t y = 0; y < proxy.height; ++y)
        for (int64_t x = 0; x < proxy.width; ++x)
            for (int c = 0; c < 3; ++c)
                img.row(y)[x * 3 + c] =
                    static_cast<uint8_t>(std::clamp(proxy.px(x, y)[c], 0.f, 1.f) * 255.f + 0.5f);
    const double s = 1.0 / proxy.scale;
    const uint8_t blue[3] = {61, 139, 255}, yellow[3] = {242, 199, 68}, red[3] = {255, 60, 60};
    if (gtOuter)
        for (const auto& poly : *gtOuter)
            for (size_t i = 0; i + 1 < poly.size(); ++i)
                line(img, poly[i] * s, poly[i + 1] * s, red);
    for (const auto& it : items) {
        drawShape(img, it.outer, s, blue);
        drawShape(img, it.inner, s, yellow);
    }
    fk::WriteOptions w;
    w.format = fk::FileFormat::png;
    w.bits = 8;
    std::error_code ec;
    std::filesystem::remove(out, ec);
    auto sink = fk::createSink(out, w);
    if (!sink) return false;
    if (!(*sink)->begin(img.size(), 3, 8).ok()) return false;
    if (!(*sink)->writeRows(img.data(), img.height(), img.rowBytes()).ok()) return false;
    return (*sink)->finish().ok();
}

} // namespace fkcli
