// Relink v2 (RFC-0006 §Relink v2, UX-07): matching sources are evidence, weighed together; a conflict is
// shown, never resolved silently. The target print comes first, then the export version, then compatibility.
#include "fk/hash.hpp"
#include "fk/image_io.hpp"
#include "fk/project.hpp"
#include "fk/util.hpp"
#include "fk/version.hpp"

#include <algorithm>
#include <cmath>

namespace fk {

const char* evidenceKindName(EvidenceKind k) {
    switch (k) {
    case EvidenceKind::file_hash:
        return "file_hash";
    case EvidenceKind::embedded_ids:
        return "embedded_ids";
    case EvidenceKind::file_name:
        return "file_name";
    case EvidenceKind::content:
        return "content";
    case EvidenceKind::user_target:
        return "user_target";
    }
    return "?";
}

const char* relinkDecisionName(RelinkDecision d) {
    switch (d) {
    case RelinkDecision::unique:
        return "unique";
    case RelinkDecision::ambiguous:
        return "ambiguous";
    case RelinkDecision::none:
        return "none";
    }
    return "?";
}

RelinkInput describeGradedFile(const std::filesystem::path& file, const ImageBuffer& pixels) {
    RelinkInput in;
    in.file = file;
    if (auto sha = sha256File(file)) in.sha256 = *sha;
    if (auto xmp = readXmpPacket(file); xmp && !xmp->empty()) {
        in.ids = parseXmpIds(*xmp);
        in.hasIds = !in.ids.itemId.empty();
    }
    in.signature = computeSignature(pixels);
    in.size = {pixels.width(), pixels.height()};
    return in;
}

namespace {

// weights (RFC-0006 table): decisive > strong > by correlation > weak
constexpr double kHash = 10.0, kIds = 3.0, kIdsVersion = 1.0, kName = 1.0, kNameVersion = 0.5;
constexpr double kContentFail = 0.50; ///< below this the content contradicts the option

struct ItemRef {
    const ScanRecord* scan = nullptr;
    const ItemRecord* item = nullptr;
};

ItemRef findItemRef(const Project& p, const std::string& itemId) {
    for (const auto& s : p.scans)
        for (const auto& it : s.items)
            if (it.itemId == itemId) return {&s, &it};
    return {};
}

void scoreContent(RelinkOption& o, const Signature& graded, const Signature& ref) {
    if (!graded.valid || !ref.valid) return;
    double best = -2;
    int bestK = 0;
    for (int k = 0; k < 4; ++k) {
        const double c = compareSignatures(rotateSignature(ref, k), graded);
        if (c > best) {
            best = c;
            bestK = k;
        }
    }
    o.contentChecked = true;
    o.contentScore = best;
    o.contentRotate = bestK;
    o.contentPass = best >= kSignatureMatchThreshold;
    char d[64];
    std::snprintf(d, sizeof d, "correlation %.2f at %d°", best, bestK * 90);
    if (o.contentPass) {
        o.evidence.push_back({EvidenceKind::content, true, d});
        o.score += 2.0 + 2.0 * (best - kSignatureMatchThreshold) / (1.0 - kSignatureMatchThreshold);
    } else if (best < kContentFail) {
        o.evidence.push_back({EvidenceKind::content, false, d});
        o.score -= 2.0;
    } else {
        o.evidence.push_back({EvidenceKind::content, true, std::string(d) + " (below the match threshold)"});
    }
}

void finish(RelinkOption& o) {
    bool sup = false, con = false;
    for (const auto& e : o.evidence) {
        if (e.kind == EvidenceKind::user_target) continue;
        (e.supports ? sup : con) = true;
    }
    o.conflict = sup && con;
    o.contradicted = con;
}

bool supportsByIdsOrName(const RelinkOption& o) {
    return std::any_of(o.evidence.begin(), o.evidence.end(), [](const Evidence& e) {
        return e.supports && (e.kind == EvidenceKind::embedded_ids || e.kind == EvidenceKind::file_name);
    });
}
bool hasSupport(const RelinkOption& o) {
    return o.hashMatch || o.contentPass || supportsByIdsOrName(o);
}

} // namespace

RelinkAssessment assessRelink(const RelinkInput& in, const Project& p, const std::string& targetItemId,
                              const Signature* targetSignature) {
    RelinkAssessment a;
    // evidence from the file name, independent of any option
    std::optional<EditFileName> name;
    {
        const std::string stem = pathToUtf8(in.file.stem());
        name = parseEditFileName(stem);
    }
    auto nameItem = [&](const ScanRecord& s, const ItemRecord& it) {
        return name && pathToUtf8(pathFromUtf8(s.path).stem()) == name->scanStem &&
               it.index == name->itemIndex;
    };
    bool nameNamesSomePrint = false;
    if (name)
        for (const auto& s : p.scans)
            for (const auto& it : s.items)
                if (nameItem(s, it)) nameNamesSomePrint = true;
    const bool idsNameSomePrint = in.hasIds && findItemRef(p, in.ids.itemId).item != nullptr;

    // one option per export record (of the target only, when given)
    for (const auto& rec : p.exportRecords) {
        if (!targetItemId.empty() && rec.itemId != targetItemId) continue;
        const ItemRef ref = findItemRef(p, rec.itemId);
        if (!ref.item) continue; // record of a removed print
        RelinkOption o;
        o.scanId = ref.scan->scanId;
        o.itemId = rec.itemId;
        o.editVersionId = rec.editVersionId;
        if (!targetItemId.empty())
            o.evidence.push_back({EvidenceKind::user_target, true, "chosen by the user"});
        if (!in.sha256.empty() && in.sha256 == rec.fileSha256) {
            o.hashMatch = true;
            o.evidence.push_back({EvidenceKind::file_hash, true, "identical to the exported file"});
            o.score += kHash;
        }
        if (in.hasIds) {
            if (in.ids.itemId == rec.itemId) {
                o.evidence.push_back({EvidenceKind::embedded_ids, true, "embedded IDs name this print"});
                o.score += kIds;
                if (!in.ids.editVersionId.empty() && in.ids.editVersionId == rec.editVersionId) {
                    o.score += kIdsVersion;
                    o.versionEvidence = true;
                }
            } else if (idsNameSomePrint) {
                o.evidence.push_back({EvidenceKind::embedded_ids, false, "embedded IDs name another print"});
                o.score -= kIds;
            }
        }
        if (name) {
            if (nameItem(*ref.scan, *ref.item)) {
                o.evidence.push_back({EvidenceKind::file_name, true, "file name names this print"});
                o.score += kName;
                if (name->version == rec.version) {
                    o.score += kNameVersion;
                    o.versionEvidence = true;
                }
            } else if (nameNamesSomePrint) {
                o.evidence.push_back({EvidenceKind::file_name, false, "file name names another print"});
                o.score -= kName;
            }
        }
        scoreContent(o, in.signature, rec.signature);
        finish(o);
        a.options.push_back(std::move(o));
    }

    // target without any usable export: offer a new alignment (never a faked export)
    if (!targetItemId.empty()) {
        const ItemRef ref = findItemRef(p, targetItemId);
        const bool anyPass = std::any_of(a.options.begin(), a.options.end(),
                                         [](const RelinkOption& o) { return o.hashMatch || o.contentPass; });
        if (ref.item && !anyPass) {
            RelinkOption o;
            o.scanId = ref.scan->scanId;
            o.itemId = targetItemId;
            o.newAlignment = true;
            o.evidence.push_back({EvidenceKind::user_target, true, "chosen by the user"});
            if (targetSignature) scoreContent(o, in.signature, *targetSignature);
            finish(o);
            a.options.push_back(std::move(o));
        }
    }

    std::stable_sort(a.options.begin(), a.options.end(),
                     [](const RelinkOption& x, const RelinkOption& y) { return x.score > y.score; });

    // decision (RFC-0006 §Decisions)
    const bool targeted = !targetItemId.empty();
    if (a.options.empty() || std::none_of(a.options.begin(), a.options.end(), hasSupport)) {
        // nothing speaks for any option: batch rows offer "choose the print…"; with a target the user still
        // picks
        a.decision = targeted && !a.options.empty() ? RelinkDecision::ambiguous : RelinkDecision::none;
        return a;
    }
    const RelinkOption& best = a.options.front();
    // unique needs decisive or corroborated evidence: the file hash, or IDs/name + content, or target +
    // content
    const bool corroborated = best.hashMatch || (best.contentPass && (supportsByIdsOrName(best) || targeted));
    if (!corroborated || best.conflict) {
        a.decision = RelinkDecision::ambiguous;
        return a;
    }
    if (!best.hashMatch) {
        // a competitor: another option whose picture also matches and that no evidence contradicts — unless
        // the embedded IDs or the file name pick this export version of the same print
        for (size_t i = 1; i < a.options.size(); ++i) {
            const RelinkOption& o = a.options[i];
            if (!(o.hashMatch || o.contentPass) || o.contradicted) continue;
            if (o.itemId == best.itemId && best.versionEvidence && !o.versionEvidence) continue;
            a.decision = RelinkDecision::ambiguous;
            return a;
        }
    }
    a.decision = RelinkDecision::unique;
    return a;
}

namespace {
double aspectMismatch(SizeI graded, const ExportGeometry& g) {
    if (graded.w <= 0 || graded.h <= 0 || g.outSize.w <= 0 || g.outSize.h <= 0) return 1e9;
    const double ar = static_cast<double>(graded.w) / graded.h;
    const double br = static_cast<double>(g.outSize.w) / g.outSize.h;
    return std::min(std::abs(ar / br - 1.0), std::abs(1.0 / ar / br - 1.0));
}
bool sameSize(SizeI graded, SizeI out) {
    return graded == out || graded == SizeI{out.h, out.w};
}
constexpr double kAspectTolerance =
    0.005; // the same tolerance as for scaling any graded file (composite.cpp)
} // namespace

ExportGeometry alignmentGeometry(const ScanRecord& s, const ItemRecord& it, SizeI graded) {
    const SizeI scan{s.info.width, s.info.height};
    const ExportGeometry straight = computeExportGeometry(it.inner, it.outer.quad, GeometryMode::straightened,
                                                          it.upright, std::nullopt, scan);
    if (graded.w <= 0 || graded.h <= 0) return straight;
    const ExportGeometry crop = computeExportGeometry(it.inner, it.outer.quad, GeometryMode::pixel_crop,
                                                      it.upright, std::nullopt, scan);
    // an unresized export of either kind has exactly that size
    if (sameSize(graded, crop.outSize) && !sameSize(graded, straight.outSize)) return crop;
    if (sameSize(graded, straight.outSize)) return straight;
    return aspectMismatch(graded, crop) + 1e-4 < aspectMismatch(graded, straight) ? crop : straight;
}

ExportGeometry alignmentGeometry(const ScanRecord& s, const ItemRecord& it, SizeI graded,
                                 const ImageBuffer& preview, double downscale,
                                 const Signature& gradedSignature) {
    const ExportGeometry first = alignmentGeometry(s, it, graded);
    const SizeI scan{s.info.width, s.info.height};
    if (sameSize(graded, first.outSize) || !gradedSignature.valid) return first;
    const ExportGeometry other = computeExportGeometry(
        it.inner, it.outer.quad,
        first.mode == GeometryMode::pixel_crop ? GeometryMode::straightened : GeometryMode::pixel_crop,
        it.upright, std::nullopt, scan);
    if (aspectMismatch(graded, other) > kAspectTolerance) return first;
    // both shapes fit the graded file (a resized export): the picture decides
    auto best = [&](const ExportGeometry& g) {
        const Signature a = signatureOfExport(preview, g, downscale);
        double m = -2;
        for (int k = 0; k < 4; ++k)
            m = std::max(m, compareSignatures(rotateSignature(a, k), gradedSignature));
        return m;
    };
    return best(other) > best(first) ? other : first;
}

Sidecar alignmentRecord(const Project& p, const ScanRecord& s, const ItemRecord& it, const ExportGeometry& g,
                        const Signature& areaSignature, const std::string& gradedFileName,
                        const std::string& gradedSha256) {
    Sidecar sc;
    sc.origin = "manual_alignment";
    sc.projectId = p.projectId;
    sc.scanId = s.scanId;
    sc.itemId = it.itemId;
    sc.editVersionId = newUuid();
    sc.label = it.label;
    int v = 0;
    for (const auto& e : it.edits)
        v = std::max(v, e.version);
    for (const auto& r : p.exportRecords)
        if (r.itemId == it.itemId) v = std::max(v, r.version);
    sc.version = v + 1;
    sc.scanFile = pathToUtf8(pathFromUtf8(s.path).filename());
    sc.scanFingerprint = s.fingerprint;
    sc.inner = it.inner;
    sc.outer = it.outer;
    sc.geometry = g;
    sc.sourceSize = {s.info.width, s.info.height};
    sc.exportSize = sc.geometry.outSize;
    sc.bits = s.info.bitsPerSample;
    sc.channels = s.info.channels;
    sc.iccDescription = s.info.iccDescription;
    sc.iccSha256 = s.iccSha256;
    sc.dpiX = s.info.dpiX;
    sc.dpiY = s.info.dpiY;
    sc.fileName = gradedFileName;
    sc.fileSha256 = gradedSha256;
    sc.signature = areaSignature;
    sc.software = productNameAndVersion();
    return sc; // exportedAt stays empty: not an export
}

Signature signatureOfExport(const ImageBuffer& img, const ExportGeometry& g, double downscale) {
    if (g.outSize.w <= 0 || g.outSize.h <= 0 || img.width() <= 0 || img.height() <= 0 || downscale <= 0)
        return {};
    // sample the output box on a grid of up to 256 px (signatures are 64×64 cells of the whole box)
    const double f = std::min(1.0, 256.0 / static_cast<double>(std::max(g.outSize.w, g.outSize.h)));
    const int64_t W = std::max<int64_t>(Signature::kSide, std::llround(g.outSize.w * f));
    const int64_t H = std::max<int64_t>(Signature::kSide, std::llround(g.outSize.h * f));
    auto buf = ImageBuffer::create(W, H, 1, 16);
    if (!buf) return {};
    const int ch = img.channels();
    const double sx = static_cast<double>(g.outSize.w) / W, sy = static_cast<double>(g.outSize.h) / H;
    // box-filter each grid cell (s×s samples over its footprint in `img`), like the export's own downsampling
    const double footprint = std::max(sx, sy) *
                             std::max(std::abs(g.outToScan.m[0]) + std::abs(g.outToScan.m[1]),
                                      std::abs(g.outToScan.m[3]) + std::abs(g.outToScan.m[4])) /
                             downscale;
    const int n = std::clamp(static_cast<int>(std::ceil(footprint)), 1, 8);
    auto luma = [&](double fx, double fy) {
        const int64_t x = std::clamp<int64_t>(static_cast<int64_t>(std::floor(fx)), 0, img.width() - 1);
        const int64_t y = std::clamp<int64_t>(static_cast<int64_t>(std::floor(fy)), 0, img.height() - 1);
        if (ch < 3) return static_cast<double>(img.sample16(x, y, 0));
        return 0.2126 * img.sample16(x, y, 0) + 0.7152 * img.sample16(x, y, 1) +
               0.0722 * img.sample16(x, y, 2);
    };
    for (int64_t j = 0; j < H; ++j) {
        auto* row = reinterpret_cast<uint16_t*>(buf->row(j));
        for (int64_t i = 0; i < W; ++i) {
            double sum = 0;
            for (int b = 0; b < n; ++b)
                for (int a = 0; a < n; ++a) {
                    const Vec2 sp = g.outToScan.apply({(i + (a + 0.5) / n) * sx, (j + (b + 0.5) / n) * sy});
                    sum += luma(sp.x / downscale, sp.y / downscale);
                }
            row[i] = static_cast<uint16_t>(std::clamp(sum / (n * n), 0.0, 65535.0));
        }
    }
    return computeSignature(*buf);
}

} // namespace fk
