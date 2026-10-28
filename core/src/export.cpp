// Export pipeline (RFC-0005): geometry → read → (resample) → (colour once) → (depth) → sink → verify.
#include "fk/export.hpp"

#include "fk/hash.hpp"
#include "fk/util.hpp"
#include "output_stage.hpp"
#include "pixel_ops.hpp"

#include <chrono>
#include <cstdio>

namespace fk {

const char* exportTargetName(ExportTarget t) {
    return t == ExportTarget::image_area ? "image_area" : "full_print";
}
const char* colorPolicyName(ColorPolicy::Kind k) {
    switch (k) {
    case ColorPolicy::keep:
        return "keep";
    case ColorPolicy::convert:
        return "convert";
    case ColorPolicy::assign:
        return "assign";
    case ColorPolicy::untagged:
        return "untagged";
    }
    return "keep";
}

std::string fileExtension(FileFormat f) {
    switch (f) {
    case FileFormat::tiff:
        return "tif";
    case FileFormat::png:
        return "png";
    case FileFormat::jpeg:
        return "jpg";
    case FileFormat::dng:
        return "dng";
    }
    return "tif";
}

std::string itemLabel(int index) {
    char b[16];
    std::snprintf(b, sizeof(b), "P%02d", index);
    return b;
}

std::filesystem::path nextFreeVersionPath(const std::filesystem::path& dir, const std::string& base,
                                          const std::string& ext, int* versionOut) {
    for (int v = 1; v < 100000; ++v) {
        char suffix[16];
        std::snprintf(suffix, sizeof(suffix), "_v%03d", v);
        const auto p = dir / pathFromUtf8(base + suffix + "." + ext);
        std::error_code ec;
        if (!std::filesystem::exists(p, ec)) {
            if (versionOut) *versionOut = v;
            return p;
        }
    }
    return dir / pathFromUtf8(base + "_v" + randomToken(6) + "." + ext);
}

std::filesystem::path nextFreePath(const std::filesystem::path& dir, const std::string& base,
                                   const std::string& ext) {
    std::error_code ec;
    auto p = dir / pathFromUtf8(base + "." + ext);
    for (int i = 2; std::filesystem::exists(p, ec) && i < 100000; ++i)
        p = dir / pathFromUtf8(base + "-" + std::to_string(i) + "." + ext);
    return p;
}

Result<ExportPlan> planExport(const ImageSource& src, const ExportRequest& req) {
    const ImageInfo& in = src.info();
    if (!isValid(req.region)) return makeError(Errc::invalid_argument, "The boundary is not a valid shape");
    ExportPlan plan;
    auto lay = detail::computeOutputLayout(in, req.write, req.allowDepthReduction);
    if (!lay) return lay.error();
    plan.outChannels = lay->outCh;
    plan.outBits = lay->outBits;
    plan.geometry = computeExportGeometry(req.region, req.frame, req.mode, req.upright, req.standardSize,
                                          {in.width, in.height});
    if (plan.geometry.outSize.w <= 0 || plan.geometry.outSize.h <= 0 || plan.geometry.sourceRect.empty())
        return makeError(Errc::invalid_argument, "The boundary lies outside the scan");
    auto& r = plan.notExactReasons;
    if (req.mode == GeometryMode::straightened) r.push_back("resampled: straightened");
    if (req.mode == GeometryMode::standard_size) r.push_back("resampled: scaled to standard size");
    if (lay->outBits < lay->srcBits) r.push_back("bit depth reduced to 8-bit");
    if (req.color.kind == ColorPolicy::convert) r.push_back("colour converted to another profile");
    if (req.write.format == FileFormat::jpeg) r.push_back("lossy JPEG compression");
    if (req.write.format == FileFormat::dng) r.push_back("linearised for Linear DNG");
    plan.frameBitExact = r.empty();
    WriteOptions w = req.write;
    w.bits = lay->outBits;
    plan.estimatedBytes = estimateOutputBytes(plan.geometry.outSize, lay->outCh, lay->outBits, w);
    const auto dir = req.output.parent_path().empty() ? std::filesystem::path(".") : req.output.parent_path();
    plan.freeBytes = freeDiskBytes(dir);
    const uint64_t region =
        static_cast<uint64_t>(plan.geometry.sourceRect.w) * plan.geometry.sourceRect.h * in.bytesPerPixel();
    const uint64_t band = static_cast<uint64_t>(plan.geometry.sourceRect.w) * 256 * in.bytesPerPixel() * 2;
    plan.estimatedPeakMemory =
        (req.mode == GeometryMode::pixel_crop && plan.geometry.upright == 0) ? band : region + band;
    switch (req.color.kind) {
    case ColorPolicy::keep:
        plan.iccDescription = in.iccDescription;
        break;
    case ColorPolicy::untagged:
        break;
    default:
        plan.iccDescription = iccDescription(req.color.icc);
    }
    return plan;
}

Result<ExportReport> exportRegion(ImageSource& src, const ExportRequest& req, const CancelToken& cancel,
                                  const ProgressFn& progress) {
    const auto t0 = std::chrono::steady_clock::now();
    BufferAccounting::instance().resetPeak();
    const ImageInfo& in = src.info();
    if (!req.reviewed)
        return makeError(Errc::needs_review,
                         "This print has low detection confidence and must be reviewed first");
    auto planR = planExport(src, req);
    if (!planR) return planR.error();
    const ExportPlan& plan = *planR;
    if (plan.freeBytes > 0 && plan.estimatedBytes + plan.estimatedBytes / 10 > plan.freeBytes)
        return makeError(Errc::disk_full, "Not enough free disk space for this export",
                         "needed~" + std::to_string(plan.estimatedBytes) +
                             " free=" + std::to_string(plan.freeBytes));
    const ExportGeometry& g = plan.geometry;
    ExportReport rep;
    rep.resampled = g.resampled;
    rep.frameBitExact = plan.frameBitExact;
    rep.notExactReasons = plan.notExactReasons;
    rep.geometry = g;

    detail::OutputStage out;
    detail::OutputStage::Params op;
    op.srcInfo = &in;
    op.srcMeta = &src.metadata();
    op.write = req.write;
    op.color = req.color;
    op.privacy = req.privacy;
    op.ids = req.ids;
    op.output = req.output;
    op.outSize = g.outSize;
    op.dpiScale = g.scale;
    op.allowDepthReduction = req.allowDepthReduction;
    FK_TRY(out.open(op, rep));

    const size_t srcPx = in.bytesPerPixel();
    constexpr int64_t kBand = 128;
    auto workBand =
        AccountedBuffer::allocate(MemCategory::band, srcPx * static_cast<size_t>(g.outSize.w) * kBand);
    if (!workBand) return workBand.error();
    const size_t workRow = srcPx * static_cast<size_t>(g.outSize.w);
    auto fail = [&](Error e) -> Error {
        out.abort();
        return e;
    };

    const RectI& R = g.sourceRect;
    if (!g.resampled && g.upright == 0) {
        // (a) streamed pixel crop: memory = one band
        for (int64_t y = 0; y < R.h; y += kBand) {
            if (cancel.cancelled()) return fail(cancelledError());
            const int64_t n = std::min(kBand, R.h - y);
            auto st = src.readRows(R.y + y, n, R.x, R.w, workBand->data(), workRow);
            if (!st) return fail(st.error());
            auto e = out.writeRows(workBand->data(), n, workRow);
            if (!e) return fail(e.error());
            report(progress, 0.9 * static_cast<double>(y + n) / R.h, "export");
        }
    } else {
        // (b) lossless 90° permutation or (c) Lanczos-3 resampling from a buffered region
        AccountedBuffer region;
        auto st = detail::loadRegion(src, R, region, cancel, progress, 0.0, 0.4);
        if (!st) return fail(st.error());
        const size_t regionRow = srcPx * static_cast<size_t>(R.w);
        const double kscale = std::max(1.0, 1.0 / g.scale);
        const int ch = in.channels, bits = in.bitsPerSample;
        for (int64_t v0 = 0; v0 < g.outSize.h; v0 += kBand) {
            if (cancel.cancelled()) return fail(cancelledError());
            const int64_t n = std::min(kBand, g.outSize.h - v0);
            detail::parallelFor(n, [&](int64_t r) {
                const int64_t v = v0 + r;
                uint8_t* row = workBand->data() + static_cast<size_t>(r) * workRow;
                for (int64_t u = 0; u < g.outSize.w; ++u) {
                    const Vec2 p = g.outToScan.apply({u + 0.5, v + 0.5});
                    if (!g.resampled) {
                        const int64_t sx =
                            std::clamp<int64_t>(static_cast<int64_t>(std::floor(p.x)) - R.x, 0, R.w - 1);
                        const int64_t sy =
                            std::clamp<int64_t>(static_cast<int64_t>(std::floor(p.y)) - R.y, 0, R.h - 1);
                        std::memcpy(row + u * srcPx, region.data() + sy * regionRow + sx * srcPx, srcPx);
                    } else {
                        float vals[4];
                        detail::lanczosSample(region.data(), R.w, R.h, ch, bits, p.x - R.x, p.y - R.y, kscale,
                                              vals);
                        for (int c = 0; c < ch; ++c)
                            detail::storeSample(row + u * srcPx + c * (bits / 8), bits, vals[c]);
                    }
                }
            });
            auto e = out.writeRows(workBand->data(), n, workRow);
            if (!e) return fail(e.error());
            report(progress, 0.4 + 0.5 * static_cast<double>(v0 + n) / g.outSize.h, "export");
        }
    }
    FK_TRY(out.finish(req.verify, cancel, rep));
    rep.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    rep.peakAccountedBytes = BufferAccounting::instance().peak();
    report(progress, 1.0, "export");
    return rep;
}

} // namespace fk
