// fkfixture — deterministic synthetic flatbed scans with ground truth (RFC-0016).
//
// Single scan:  fkfixture --out scan.tif [--gt scan.json] [--preview scan.png] [options]
// Corpus:       fkfixture --corpus N --out-dir DIR [--seed S] [--dpis 300,400]
// Run with --help for all options. Rows are rendered in bands by worker threads while the main thread
// encodes the previous band, so memory is O(width x band) and output does not depend on threads.
#include "output.hpp"
#include "scene.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <mutex>
#include <sstream>
#include <thread>

namespace fkfx {
namespace {

// ---------------------------------------------------------------------------------------------
// Worker pool: runs fn(row, thread) for rows [0, n) asynchronously.
// ---------------------------------------------------------------------------------------------
class RowPool {
  public:
    explicit RowPool(int threads) {
        for (int i = 0; i < threads; ++i)
            workers_.emplace_back([this, i] { loop(i); });
    }
    RowPool(const RowPool&) = delete;
    RowPool& operator=(const RowPool&) = delete;
    ~RowPool() {
        {
            std::lock_guard<std::mutex> lk(m_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& t : workers_)
            t.join();
    }
    void start(int rows, std::function<void(int, int)> fn) {
        {
            std::lock_guard<std::mutex> lk(m_);
            fn_ = std::move(fn);
            rows_ = rows;
            next_.store(0);
            busy_ = static_cast<int>(workers_.size());
            ++gen_;
        }
        cv_.notify_all();
    }
    void wait() {
        std::unique_lock<std::mutex> lk(m_);
        done_.wait(lk, [this] { return busy_ == 0; });
    }

  private:
    void loop(int id) {
        std::uint64_t seen = 0;
        for (;;) {
            {
                std::unique_lock<std::mutex> lk(m_);
                cv_.wait(lk, [&] { return stop_ || gen_ != seen; });
                if (stop_) return;
                seen = gen_;
            }
            for (;;) {
                const int r = next_.fetch_add(1);
                if (r >= rows_) break;
                fn_(r, id);
            }
            std::lock_guard<std::mutex> lk(m_);
            if (--busy_ == 0) done_.notify_all();
        }
    }
    std::vector<std::thread> workers_;
    std::mutex m_;
    std::condition_variable cv_, done_;
    std::function<void(int, int)> fn_;
    int rows_ = 0;
    std::atomic<int> next_{0};
    int busy_ = 0;
    std::uint64_t gen_ = 0;
    bool stop_ = false;
};

void quantizeRow(const Options& o, const float* rgb, std::uint8_t* dst, int width) {
    const bool inv = o.photometric() == Photometric::MinIsWhite;
    const double maxv = o.bits == 16 ? 65535.0 : 255.0;
    auto put = [&](std::size_t k, double v) {
        v = clamp01(v);
        const double q = std::floor(v * maxv + 0.5);
        if (o.bits == 16) {
            const auto t = static_cast<std::uint16_t>(q);
            std::memcpy(dst + 2 * k, &t, 2);
        } else {
            dst[k] = static_cast<std::uint8_t>(q);
        }
    };
    for (int x = 0; x < width; ++x) {
        const double r = rgb[3 * x], g = rgb[3 * x + 1], b = rgb[3 * x + 2];
        const auto xs = static_cast<std::size_t>(x);
        if (o.channels == 3) {
            put(3 * xs, r);
            put(3 * xs + 1, g);
            put(3 * xs + 2, b);
        } else {
            const double yv = clamp01(0.2126 * r + 0.7152 * g + 0.0722 * b);
            put(xs, inv ? 1.0 - yv : yv);
        }
    }
}

struct Paths {
    std::filesystem::path out, gt, preview;
};

bool generate(const Scene& sc, const Paths& paths, std::string& err) {
    const Options& o = sc.opt;
    const double rawBytes = static_cast<double>(sc.width) * sc.height * o.channels * (o.bits / 8);
    if (!o.bigtiff && o.compression == Compression::None && rawBytes > 4.0e9) {
        err = "uncompressed image exceeds classic TIFF limits; use --bigtiff";
        return false;
    }
    TiffWriter tw;
    if (!tw.open(paths.out, sc, err)) return false;
    PreviewBuilder pv;
    const bool wantPreview = !paths.preview.empty();
    if (wantPreview) pv.init(sc);

    int bandH = 64;
    if (o.layout == Layout::Tiles)
        bandH = o.tile;
    else if (o.layout == Layout::Strips)
        bandH = std::clamp(o.rowsPerStrip, 32, 256);
    bandH = std::min(bandH, std::max(1, sc.height));
    const std::size_t rowBytes = static_cast<std::size_t>(sc.width) * static_cast<std::size_t>(o.channels) *
                                 static_cast<std::size_t>(o.bits / 8);
    std::vector<std::uint8_t> bufs[2];
    bufs[0].assign(rowBytes * static_cast<std::size_t>(bandH), 0);
    bufs[1].assign(rowBytes * static_cast<std::size_t>(bandH), 0);

    int threads = o.threads > 0 ? o.threads : static_cast<int>(std::thread::hardware_concurrency());
    threads = std::clamp(threads, 1, 64);
    std::vector<std::vector<float>> scratch(static_cast<std::size_t>(threads),
                                            std::vector<float>(static_cast<std::size_t>(sc.width) * 3));
    RowPool pool(threads);
    auto startBand = [&](int band) {
        const int y0 = band * bandH;
        const int rows = std::min(bandH, sc.height - y0);
        std::uint8_t* dst = bufs[band & 1].data();
        pool.start(rows, [&sc, &o, &scratch, dst, y0, rowBytes](int r, int tid) {
            float* row = scratch[static_cast<std::size_t>(tid)].data();
            renderRow(sc, y0 + r, row);
            quantizeRow(o, row, dst + static_cast<std::size_t>(r) * rowBytes, sc.width);
        });
    };
    const int nBands = (sc.height + bandH - 1) / bandH;
    startBand(0);
    pool.wait();
    for (int band = 0; band < nBands; ++band) {
        if (band + 1 < nBands) startBand(band + 1);
        const int y0 = band * bandH;
        const int rows = std::min(bandH, sc.height - y0);
        const std::uint8_t* src = bufs[band & 1].data();
        bool ok = true;
        if (wantPreview)
            for (int r = 0; r < rows; ++r)
                pv.addRow(src + static_cast<std::size_t>(r) * rowBytes, y0 + r);
        ok = tw.writeBand(src, y0, rows, err);
        if (band + 1 < nBands) pool.wait();
        if (!ok) return false;
    }
    if (!tw.close(err)) return false;
    if (!paths.gt.empty() && !writeTextFile(paths.gt, groundTruth(sc).dump(1) + "\n", err)) return false;
    if (wantPreview && !pv.write(paths.preview, err)) return false;
    return true;
}

// ---------------------------------------------------------------------------------------------
// Command line
// ---------------------------------------------------------------------------------------------
const char* kUsage = R"(fkfixture - deterministic synthetic flatbed scans + ground truth (RFC-0016)

Single scan:
  fkfixture --out scan.tif [--gt scan.json] [--preview scan.png] [options]
Corpus:
  fkfixture --corpus N --out-dir DIR [--seed S] [--dpis 300,400] [scene options]
    writes DIR/scan_0001.tif + scan_0001.json ... and DIR/corpus.json

Output options:
  --dpi N                 resolution (default 300)
  --bits 8|16             sample depth (default 16)
  --channels 1|3          grey or RGB (default 3)
  --minisblack | --miniswhite   grey photometric (miniswhite stores inverted values)
  --compression none|lzw|deflate|packbits   (default lzw)
  --predictor 0|2         horizontal differencing, lzw/deflate only (default 0)
  --layout strips|single-strip|tiles        (default strips)
  --rows-per-strip N      (default 16)
  --tile N                tile size, multiple of 16 (default 256)
  --bigtiff               write BigTIFF
  --icc none|srgb         embed lcms2 sRGB profile (grey: sRGB tone curve) (default srgb)
  --preview out.png       downsampled RGB PNG (long side 1200 px) with GT overlays:
                          outer quads red (filled square = outerQuad[0]), inner shapes green,
                          yellow tick = direction of the card's top edge
  --threads N             render threads (default: hardware concurrency; output is identical)

Scene options:
  --seed N                (default 1)
  --count N               number of prints 1..12 (default 4)
  --types list            polaroid_600,polaroid_round,instax_mini,instax_square,instax_wide|all
  --frames list           white,cream,aged,gold,black,colour|all
  --content list          normal,dark,bright|all
  --features list         handwriting,stain,glare,shadow,dust,scratch|all|none (default none)
  --feature-prob P        probability that a listed print feature is applied to a print (default 1)
  --dust-density D        dust specks per cm^2 (default 1)
  --background L          background level (default 0.06)
  --bg-gradient G         background change across the scan (default 0.02)
  --noise S               per-pixel noise sigma (default 0.006)
  --scan-mm WxH           scan size in mm (default 216x297)
  --tilt-max D            max |tilt| in degrees (default 3; corpus default 5)
  --quarter-turns 0|1|2|3|any   (default 0)
  --touching              two prints share an edge (gap 0)

Conventions: pixel (x,y) covers [x,x+1)x[y,y+1); card (u,v) mm from the upright card's top-left;
p = c + R(phi)((u-W/2)s, (v-H/2)s), phi = quarterTurns*90 + tilt (clockwise on screen);
upright = (4 - quarterTurns) mod 4. Quads: clockwise from the corner with the smallest x+y.
outerBoundary: 256 points clockwise, uniform in arc length, starting at the middle of the
corner arc of outerQuad[0].
)";

struct UsageError {
    std::string msg;
};

std::filesystem::path pathFromUtf8(const std::string& s) {
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}

std::vector<std::string> splitList(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char ch : s) {
        if (ch == ',') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else if (ch != ' ') {
            cur.push_back(ch);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

double toDouble(const std::string& key, const std::string& v) {
    char* end = nullptr;
    const double d = std::strtod(v.c_str(), &end);
    if (v.empty() || *end != '\0') throw UsageError{"invalid number for " + key + ": " + v};
    return d;
}
long long toInt(const std::string& key, const std::string& v) {
    char* end = nullptr;
    const long long i = std::strtoll(v.c_str(), &end, 10);
    if (v.empty() || *end != '\0') throw UsageError{"invalid integer for " + key + ": " + v};
    return i;
}

std::vector<int> parseTypes(const std::string& v) {
    std::vector<int> out;
    const auto& types = filmTypes();
    for (const auto& t : splitList(v)) {
        if (t == "all") {
            for (int i = 0; i < static_cast<int>(types.size()); ++i)
                out.push_back(i);
            continue;
        }
        int found = -1;
        for (int i = 0; i < static_cast<int>(types.size()); ++i)
            if (t == types[static_cast<std::size_t>(i)].id) found = i;
        if (found < 0) throw UsageError{"unknown film type: " + t};
        out.push_back(found);
    }
    if (out.empty()) throw UsageError{"empty --types"};
    return out;
}

std::vector<int> parseFrames(const std::string& v) {
    std::vector<int> out;
    const auto& frames = frameDefs();
    for (auto t : splitList(v)) {
        if (t == "color") t = "colour";
        if (t == "all") {
            for (int i = 0; i < static_cast<int>(frames.size()); ++i)
                out.push_back(i);
            continue;
        }
        int found = -1;
        for (int i = 0; i < static_cast<int>(frames.size()); ++i)
            if (t == frames[static_cast<std::size_t>(i)].id) found = i;
        if (found < 0) throw UsageError{"unknown frame: " + t};
        out.push_back(found);
    }
    if (out.empty()) throw UsageError{"empty --frames"};
    return out;
}

std::vector<Content> parseContent(const std::string& v) {
    std::vector<Content> out;
    for (const auto& t : splitList(v)) {
        if (t == "all") {
            out.insert(out.end(), {Content::Normal, Content::Dark, Content::Bright});
        } else if (t == "normal") {
            out.push_back(Content::Normal);
        } else if (t == "dark") {
            out.push_back(Content::Dark);
        } else if (t == "bright") {
            out.push_back(Content::Bright);
        } else {
            throw UsageError{"unknown content: " + t};
        }
    }
    if (out.empty()) throw UsageError{"empty --content"};
    return out;
}

unsigned parseFeatures(const std::string& v) {
    unsigned m = 0;
    for (const auto& t : splitList(v)) {
        if (t == "all")
            m |= kAllFeatures;
        else if (t == "none")
            continue;
        else if (t == "handwriting")
            m |= kHandwriting;
        else if (t == "stain")
            m |= kStain;
        else if (t == "glare")
            m |= kGlare;
        else if (t == "shadow")
            m |= kShadow;
        else if (t == "dust")
            m |= kDust;
        else if (t == "scratch")
            m |= kScratch;
        else
            throw UsageError{"unknown feature: " + t};
    }
    return m;
}

void validate(const Options& o) {
    if (o.dpi < 50 || o.dpi > 4800) throw UsageError{"--dpi must be 50..4800"};
    if (o.bits != 8 && o.bits != 16) throw UsageError{"--bits must be 8 or 16"};
    if (o.channels != 1 && o.channels != 3) throw UsageError{"--channels must be 1 or 3"};
    if (o.minIsWhite && o.channels != 1) throw UsageError{"--miniswhite requires --channels 1"};
    if (o.predictor != 0 && o.predictor != 2) throw UsageError{"--predictor must be 0 or 2"};
    if (o.predictor == 2 && o.compression != Compression::Lzw && o.compression != Compression::Deflate)
        throw UsageError{"--predictor 2 requires --compression lzw or deflate"};
    if (o.rowsPerStrip < 1) throw UsageError{"--rows-per-strip must be >= 1"};
    if (o.tile < 16 || o.tile > 4096 || o.tile % 16 != 0)
        throw UsageError{"--tile must be a multiple of 16 in 16..4096"};
    if (o.count < 1 || o.count > 12) throw UsageError{"--count must be 1..12"};
    if (o.touching && o.count < 2) throw UsageError{"--touching needs --count >= 2"};
    if (o.scanWmm < 20 || o.scanHmm < 20 || o.scanWmm > 2000 || o.scanHmm > 2000)
        throw UsageError{"--scan-mm out of range"};
    if (o.tiltMax < 0 || o.tiltMax > 45) throw UsageError{"--tilt-max must be 0..45"};
    if (o.noise < 0 || o.background < 0 || o.background > 1) throw UsageError{"invalid --noise/--background"};
    if (o.featureProb < 0 || o.featureProb > 1) throw UsageError{"--feature-prob must be 0..1"};
    if (o.dustDensity < 0 || o.dustDensity > 100) throw UsageError{"--dust-density must be 0..100"};
}

std::string joinNames(const std::vector<int>& idx, bool frames) {
    std::string s;
    for (int i : idx) {
        if (!s.empty()) s += ",";
        s += frames ? frameDefs()[static_cast<std::size_t>(i)].id
                    : filmTypes()[static_cast<std::size_t>(i)].id;
    }
    return s;
}

std::string fmtNum(double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.10g", v);
    return buf;
}

// Command-line arguments that reproduce `o` (used by corpus.json).
std::vector<std::string> optionsToArgs(const Options& o) {
    std::vector<std::string> a = {"--seed", std::to_string(o.seed), "--dpi",      std::to_string(o.dpi),
                                  "--bits", std::to_string(o.bits), "--channels", std::to_string(o.channels)};
    if (o.channels == 1) a.push_back(o.minIsWhite ? "--miniswhite" : "--minisblack");
    a.insert(a.end(), {"--compression", compressionName(o.compression), "--predictor",
                       std::to_string(o.predictor), "--layout", layoutName(o.layout), "--rows-per-strip",
                       std::to_string(o.rowsPerStrip), "--tile", std::to_string(o.tile)});
    if (o.bigtiff) a.push_back("--bigtiff");
    a.insert(a.end(), {"--icc", o.iccSrgb ? "srgb" : "none", "--count", std::to_string(o.count), "--types",
                       joinNames(o.types, false), "--frames", joinNames(o.frames, true)});
    std::string c;
    for (Content k : o.contents)
        c += (c.empty() ? "" : ",") + std::string(contentName(k));
    std::string f;
    for (const auto& n : featureNames(o.features))
        f += (f.empty() ? "" : ",") + n;
    a.insert(a.end(), {"--content",       c,
                       "--features",      f.empty() ? "none" : f,
                       "--feature-prob",  fmtNum(o.featureProb),
                       "--dust-density",  fmtNum(o.dustDensity),
                       "--background",    fmtNum(o.background),
                       "--bg-gradient",   fmtNum(o.bgGradient),
                       "--noise",         fmtNum(o.noise),
                       "--scan-mm",       fmtNum(o.scanWmm) + "x" + fmtNum(o.scanHmm),
                       "--tilt-max",      fmtNum(o.tiltMax),
                       "--quarter-turns", o.quarterTurns < 0 ? "any" : std::to_string(o.quarterTurns)});
    if (o.touching) a.push_back("--touching");
    return a;
}

int runCorpus(const Options& base, bool tiltGiven, int n, const std::filesystem::path& dir,
              std::vector<int> dpis) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        std::fprintf(stderr, "fkfixture: cannot create %s\n", pathString(dir).c_str());
        return 1;
    }
    if (dpis.empty()) dpis = {300, 400};
    const auto all = [](int k) {
        std::vector<int> v;
        for (int i = 0; i < k; ++i)
            v.push_back(i);
        return v;
    };
    // Film-type mixes (indices into filmTypes()).
    const std::vector<std::vector<int>> typeMixes = {all(5), {0}, {2}, {1, 0}, {3}, {4}, {2, 3, 4}};
    const std::vector<std::vector<int>> frameMixes = {{0}, {1}, {2}, {3}, {4}, {5}, all(6)};
    const std::vector<std::vector<Content>> contentMixes = {
        {Content::Normal},
        {Content::Dark},
        {Content::Bright},
        {Content::Normal, Content::Dark, Content::Bright}};
    const std::vector<unsigned> featureSets = {kAllFeatures, kHandwriting | kShadow, kDust | kScratch,
                                               kStain | kGlare, 0u};
    const Compression comps[4] = {Compression::None, Compression::Lzw, Compression::Deflate,
                                  Compression::PackBits};
    const Layout layouts[3] = {Layout::Strips, Layout::SingleStrip, Layout::Tiles};
    const int counts[7] = {4, 2, 6, 1, 3, 8, 5};
    const int rpsCycle[4] = {16, 64, 3, 256};
    const int tileCycle[3] = {256, 128, 512};
    const double bgLevels[4] = {0.06, 0.03, 0.12, 0.08};
    const double bgGrads[3] = {0.02, 0.04, 0.0};
    const bool qtAny[5] = {false, true, false, true, true};

    nlohmann::ordered_json scans = nlohmann::ordered_json::array();
    for (int i = 0; i < n; ++i) {
        const auto ui = static_cast<std::size_t>(i);
        Options o = base;
        o.seed = splitmix64(base.seed * 1000003ull + static_cast<std::uint64_t>(i)) & 0xFFFFFFFFull;
        o.types = typeMixes[ui % typeMixes.size()];
        o.frames = frameMixes[(ui / 2) % frameMixes.size()];
        o.contents = contentMixes[ui % contentMixes.size()];
        o.features = featureSets[ui % featureSets.size()];
        o.featureProb = 0.7;
        o.bits = (i % 2 == 0) ? 16 : 8;
        o.compression = comps[(ui / 2) % 4];
        o.predictor =
            (o.compression == Compression::Lzw || o.compression == Compression::Deflate) && ((i / 8) % 2 == 1)
                ? 2
                : 0;
        o.layout = layouts[ui % 3];
        o.rowsPerStrip = rpsCycle[(ui / 3) % 4];
        o.tile = tileCycle[(ui / 3) % 3];
        o.channels = (i % 7 == 6) ? 1 : 3;
        o.minIsWhite = o.channels == 1 && ((i / 7) % 2 == 1);
        o.bigtiff = (i % 6 == 5);
        o.iccSrgb = (i % 4 != 3);
        o.quarterTurns = qtAny[ui % 5] ? -1 : 0;
        o.dpi = dpis[(ui / 3) % dpis.size()];
        o.count = counts[ui % 7];
        o.background = bgLevels[ui % 4];
        o.bgGradient = bgGrads[ui % 3];
        o.touching = (i % 9 == 4);
        if (!tiltGiven) o.tiltMax = 5.0;
        Scene sc;
        std::optional<std::string> e;
        for (;;) {
            if (o.touching && o.count < 2) o.count = 2;
            e = buildScene(o, sc);
            if (!e || o.count <= 1) break;
            --o.count; // corpus: drop prints until the mix fits on the scan
        }
        if (e) {
            std::fprintf(stderr, "fkfixture: scan %d: %s\n", i + 1, e->c_str());
            return 1;
        }
        char name[32];
        std::snprintf(name, sizeof name, "scan_%04d", i + 1);
        Paths paths{dir / (std::string(name) + ".tif"), dir / (std::string(name) + ".json"), {}};
        std::string err;
        if (!generate(sc, paths, err)) {
            std::fprintf(stderr, "fkfixture: %s: %s\n", name, err.c_str());
            return 1;
        }
        nlohmann::ordered_json entry;
        entry["file"] = std::string(name) + ".tif";
        entry["groundTruth"] = std::string(name) + ".json";
        entry["seed"] = o.seed;
        entry["dpi"] = o.dpi;
        entry["width"] = sc.width;
        entry["height"] = sc.height;
        entry["prints"] = static_cast<int>(sc.prints.size());
        entry["bits"] = o.bits;
        entry["channels"] = o.channels;
        entry["photometric"] = photometricName(o.photometric());
        entry["compression"] = compressionName(o.compression);
        entry["predictor"] = o.predictor;
        entry["layout"] = layoutName(o.layout);
        entry["bigtiff"] = o.bigtiff;
        entry["icc"] = o.iccSrgb ? "srgb" : "none";
        entry["args"] = optionsToArgs(o);
        scans.push_back(entry);
        std::printf("%s  %dx%d  %d dpi  %d prints  %d-bit %s %s %s\n", name, sc.width, sc.height, o.dpi,
                    static_cast<int>(sc.prints.size()), o.bits, photometricName(o.photometric()),
                    compressionName(o.compression), layoutName(o.layout));
    }
    nlohmann::ordered_json corpus;
    corpus["schema"] = "fk.fixture-corpus/1";
    corpus["seed"] = base.seed;
    corpus["count"] = n;
    corpus["dpis"] = dpis;
    corpus["scans"] = scans;
    std::string err;
    if (!writeTextFile(dir / "corpus.json", corpus.dump(1) + "\n", err)) {
        std::fprintf(stderr, "fkfixture: %s\n", err.c_str());
        return 1;
    }
    return 0;
}

int run(int argc, char** argv) {
    Options o;
    o.types = parseTypes("all");
    o.frames = parseFrames("all");
    o.contents = parseContent("all");
    Paths paths;
    int corpus = 0;
    std::filesystem::path outDir;
    std::vector<int> dpis;
    bool tiltGiven = false;

    for (int i = 1; i < argc; ++i) {
        const std::string k = argv[i];
        auto val = [&]() -> std::string {
            if (i + 1 >= argc) throw UsageError{"missing value for " + k};
            return argv[++i];
        };
        if (k == "--help" || k == "-h") {
            std::fputs(kUsage, stdout);
            return 0;
        } else if (k == "--out") {
            paths.out = pathFromUtf8(val());
        } else if (k == "--gt") {
            paths.gt = pathFromUtf8(val());
        } else if (k == "--preview") {
            paths.preview = pathFromUtf8(val());
        } else if (k == "--dpi") {
            o.dpi = static_cast<int>(toInt(k, val()));
        } else if (k == "--bits") {
            o.bits = static_cast<int>(toInt(k, val()));
        } else if (k == "--channels") {
            o.channels = static_cast<int>(toInt(k, val()));
        } else if (k == "--compression") {
            const std::string v = val();
            if (v == "none")
                o.compression = Compression::None;
            else if (v == "lzw")
                o.compression = Compression::Lzw;
            else if (v == "deflate" || v == "zip")
                o.compression = Compression::Deflate;
            else if (v == "packbits")
                o.compression = Compression::PackBits;
            else
                throw UsageError{"unknown compression: " + v};
        } else if (k == "--predictor") {
            o.predictor = static_cast<int>(toInt(k, val()));
        } else if (k == "--layout") {
            const std::string v = val();
            if (v == "strips")
                o.layout = Layout::Strips;
            else if (v == "single-strip")
                o.layout = Layout::SingleStrip;
            else if (v == "tiles")
                o.layout = Layout::Tiles;
            else
                throw UsageError{"unknown layout: " + v};
        } else if (k == "--rows-per-strip") {
            o.rowsPerStrip = static_cast<int>(toInt(k, val()));
        } else if (k == "--tile") {
            o.tile = static_cast<int>(toInt(k, val()));
        } else if (k == "--bigtiff") {
            o.bigtiff = true;
        } else if (k == "--icc") {
            const std::string v = val();
            if (v == "none")
                o.iccSrgb = false;
            else if (v == "srgb")
                o.iccSrgb = true;
            else
                throw UsageError{"--icc must be none or srgb"};
        } else if (k == "--minisblack") {
            o.minIsWhite = false;
        } else if (k == "--miniswhite") {
            o.minIsWhite = true;
        } else if (k == "--seed") {
            o.seed = static_cast<std::uint64_t>(toInt(k, val()));
        } else if (k == "--count") {
            o.count = static_cast<int>(toInt(k, val()));
        } else if (k == "--types") {
            o.types = parseTypes(val());
        } else if (k == "--frames") {
            o.frames = parseFrames(val());
        } else if (k == "--content") {
            o.contents = parseContent(val());
        } else if (k == "--features") {
            o.features = parseFeatures(val());
        } else if (k == "--feature-prob") {
            o.featureProb = toDouble(k, val());
        } else if (k == "--dust-density") {
            o.dustDensity = toDouble(k, val());
        } else if (k == "--background") {
            o.background = toDouble(k, val());
        } else if (k == "--bg-gradient") {
            o.bgGradient = toDouble(k, val());
        } else if (k == "--noise") {
            o.noise = toDouble(k, val());
        } else if (k == "--scan-mm") {
            const std::string v = val();
            const auto x = v.find_first_of("xX");
            if (x == std::string::npos) throw UsageError{"--scan-mm expects WxH"};
            o.scanWmm = toDouble(k, v.substr(0, x));
            o.scanHmm = toDouble(k, v.substr(x + 1));
        } else if (k == "--tilt-max") {
            o.tiltMax = toDouble(k, val());
            tiltGiven = true;
        } else if (k == "--quarter-turns") {
            const std::string v = val();
            if (v == "any")
                o.quarterTurns = -1;
            else if (v == "0" || v == "1" || v == "2" || v == "3")
                o.quarterTurns = v[0] - '0';
            else
                throw UsageError{"--quarter-turns must be 0|1|2|3|any"};
        } else if (k == "--touching") {
            o.touching = true;
        } else if (k == "--threads") {
            o.threads = static_cast<int>(toInt(k, val()));
        } else if (k == "--corpus") {
            corpus = static_cast<int>(toInt(k, val()));
        } else if (k == "--out-dir") {
            outDir = pathFromUtf8(val());
        } else if (k == "--dpis") {
            for (const auto& d : splitList(val()))
                dpis.push_back(static_cast<int>(toInt(k, d)));
        } else {
            throw UsageError{"unknown option: " + k};
        }
    }

    if (corpus > 0) {
        if (outDir.empty()) throw UsageError{"--corpus requires --out-dir"};
        for (int d : dpis)
            if (d < 50 || d > 4800) throw UsageError{"--dpis values must be 50..4800"};
        validate(o);
        return runCorpus(o, tiltGiven, corpus, outDir, dpis);
    }
    if (paths.out.empty()) throw UsageError{"--out is required (or --corpus N --out-dir DIR)"};
    validate(o);
    if (paths.gt.empty()) {
        paths.gt = paths.out;
        paths.gt.replace_extension(".json");
    }
    Scene sc;
    if (const auto e = buildScene(o, sc)) {
        std::fprintf(stderr, "fkfixture: %s\n", e->c_str());
        return 1;
    }
    std::string err;
    if (!generate(sc, paths, err)) {
        std::fprintf(stderr, "fkfixture: %s\n", err.c_str());
        return 1;
    }
    return 0;
}

} // namespace
} // namespace fkfx

namespace {
int mainUtf8(int argc, char** argv) {
    try {
        return fkfx::run(argc, argv);
    } catch (const fkfx::UsageError& e) {
        std::fprintf(stderr, "fkfixture: %s (see --help)\n", e.msg.c_str());
        return 2;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "fkfixture: %s\n", e.what());
        return 1;
    }
}
} // namespace

#ifdef _WIN32
// Arguments arrive as UTF-16 on Windows; convert to UTF-8 so non-ANSI paths survive.
int wmain(int argc, wchar_t** wargv) {
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) {
        const std::u8string u = std::filesystem::path(wargv[i]).u8string();
        args.emplace_back(u.begin(), u.end());
    }
    std::vector<char*> argv;
    for (auto& a : args)
        argv.push_back(a.data());
    argv.push_back(nullptr);
    return mainUtf8(argc, argv.data());
}
#else
int main(int argc, char** argv) {
    return mainUtf8(argc, argv);
}
#endif
