// Compositing graded images back into the real scanned frame (RFC-0007, ADR-0010) and synthetic
// frames (RFC-0008).
#include "fk/composite.hpp"
#include "fk/color.hpp"

#include "output_stage.hpp"
#include "pixel_ops.hpp"

#include <chrono>
#include <cmath>

namespace fk {

CompositeCheck checkEditedGeometry(SizeI editSize, const ImageBuffer& edited, int scanBits) {
    CompositeCheck c;
    const SizeI e{edited.width(), edited.height()};
    c.sizeMatches = e == editSize;
    auto aspect = [](SizeI s) { return static_cast<double>(s.w) / std::max<int64_t>(1, s.h); };
    c.aspectMatches = std::abs(aspect(e) / aspect(editSize) - 1.0) < 0.005;
    if (!c.sizeMatches) {
        const SizeI rot{editSize.h, editSize.w};
        if (e == rot || std::abs(aspect(e) / aspect(rot) - 1.0) < 0.005) {
            c.suggestedRotate = 1;
            c.actionsNeeded.push_back("rotate");
        } else if (c.aspectMatches) {
            c.actionsNeeded.push_back("scale");
        }
    }
    c.depthLower = edited.bits() < scanBits;
    if (c.depthLower) c.actionsNeeded.push_back("accept_lower_depth");
    return c;
}

namespace {

Mat3 rotateBox(int k, double w, double h) {
    switch (k & 3) {
    case 1:
        return Mat3{{0, -1, h, 1, 0, 0, 0, 0, 1}};
    case 2:
        return Mat3{{-1, 0, w, 0, -1, h, 0, 0, 1}};
    case 3:
        return Mat3{{0, 1, 0, -1, 0, w, 0, 0, 1}};
    default:
        return Mat3::identity();
    }
}

double polylineDistance(const std::vector<Vec2>& pts, Vec2 q) {
    if (pts.empty()) return 1e300;
    if (pts.size() == 1) return norm(q - pts[0]);
    double best = 1e300;
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        const Vec2 a = pts[i], ab = pts[i + 1] - pts[i];
        const double l2 = dot(ab, ab);
        const double t = l2 > 0 ? std::clamp(dot(q - a, ab) / l2, 0.0, 1.0) : 0.0;
        best = std::min(best, norm(q - (a + ab * t)));
    }
    return best;
}

/// Edited image converted into the scan's colour space, colour channels and depth.
Result<ImageBuffer> prepareEdited(const CompositeRequest& req, const ImageInfo& scan) {
    const ImageBuffer& ed = *req.edited;
    const int scanColor = scan.hasAlpha ? scan.channels - 1 : scan.channels;
    const int edColor = (ed.channels() == 2 || ed.channels() == 4) ? ed.channels() - 1 : ed.channels();
    if (ed.bits() < scan.bitsPerSample && !req.adjust.allowDepthReduction)
        return makeError(Errc::needs_confirmation, "The graded file has a lower bit depth than the scan");
    auto outR =
        ImageBuffer::create(ed.width(), ed.height(), scanColor, scan.bitsPerSample, MemCategory::image);
    if (!outR) return outR.error();
    ImageBuffer out = std::move(outR.value());
    const bool convert = !req.editedIcc.empty() && !scan.icc.empty() && req.editedIcc != scan.icc;
    if (convert) {
        auto ep = Profile::fromIcc(req.editedIcc);
        if (!ep) return ep.error();
        auto sp = Profile::fromIcc(scan.icc);
        if (!sp) return sp.error();
        auto t =
            Transform::create(*ep, {edColor, ed.bits(), false}, *sp, {scanColor, scan.bitsPerSample, false});
        if (!t) return t.error();
        std::vector<uint8_t> tmp(static_cast<size_t>(ed.width()) * edColor * (ed.bits() / 8));
        for (int64_t y = 0; y < ed.height(); ++y) {
            const uint8_t* in = ed.row(y);
            if (edColor != ed.channels()) {
                const int bs = ed.bits() / 8;
                for (int64_t x = 0; x < ed.width(); ++x)
                    std::memcpy(&tmp[x * edColor * bs], in + x * ed.channels() * bs, edColor * bs);
                in = tmp.data();
            }
            t->apply(in, out.row(y), static_cast<size_t>(ed.width()));
        }
        return out;
    }
    if (edColor == 1 && scanColor == 3) {
        for (int64_t y = 0; y < ed.height(); ++y)
            for (int64_t x = 0; x < ed.width(); ++x)
                for (int c = 0; c < 3; ++c)
                    convertDepth(ed.row(y) + x * ed.channels() * (ed.bits() / 8), ed.bits(),
                                 out.row(y) + (x * 3 + c) * (scan.bitsPerSample / 8), scan.bitsPerSample, 1);
        return out;
    }
    if (edColor == 3 && scanColor == 1)
        return makeError(Errc::unsupported_format,
                         "A colour graded file cannot be placed into a greyscale scan");
    detail::RowConverter conv;
    FK_TRY(conv.init(ed.channels(), ed.bits(), scanColor, scan.bitsPerSample, std::nullopt,
                     ed.channels() == 2 || ed.channels() == 4));
    for (int64_t y = 0; y < ed.height(); ++y)
        conv.convert(ed.row(y), out.row(y), ed.width());
    return out;
}

struct Renderer {
    const CompositeRequest& req;
    const ImageInfo& info;
    ExportGeometry g;
    Mat3 outToScan;
    SizeI outSize;
    bool exactScan = false;
    AccountedBuffer region;
    ImageBuffer ed;
    Mat3 scanToBuf;
    bool exactEdit = false;
    Shape strokeLimit;

    Renderer(const CompositeRequest& r, const ImageInfo& i) : req(r), info(i) {}

    Status prepare(ImageSource& scan, double scale, const CancelToken& cancel) {
        if (!req.edited) return makeError(Errc::invalid_argument, "No graded image given");
        if (!isValid(req.outerRegion) || !isValid(req.inner))
            return makeError(Errc::invalid_argument, "Invalid boundary");
        g = computeExportGeometry(req.outerRegion, req.frame, req.outputMode, req.upright, std::nullopt,
                                  {info.width, info.height});
        outToScan = g.outToScan;
        outSize = g.outSize;
        exactScan = !g.resampled;
        if (scale != 1.0) {
            outSize = {std::max<int64_t>(1, std::llround(g.outSize.w * scale)),
                       std::max<int64_t>(1, std::llround(g.outSize.h * scale))};
            outToScan = g.outToScan * Mat3::scale(1.0 / scale, 1.0 / scale);
            exactScan = false;
            // resampling needs support around the region
            RectI s = boundingRect(req.outerRegion);
            const int sup = static_cast<int>(std::ceil(kResampleSupport / std::min(1.0, scale)));
            g.sourceRect = {std::max<int64_t>(0, s.x - sup), std::max<int64_t>(0, s.y - sup), 0, 0};
            g.sourceRect.w = std::min(info.width, s.right() + sup) - g.sourceRect.x;
            g.sourceRect.h = std::min(info.height, s.bottom() + sup) - g.sourceRect.y;
        }
        // graded image geometry
        const CompositeCheck chk = checkEditedGeometry(req.editSize, *req.edited, info.bitsPerSample);
        const int k = ((req.adjust.rotate % 4) + 4) % 4;
        const SizeI rotated = (k & 1) ? SizeI{req.editSize.h, req.editSize.w} : req.editSize;
        const SizeI e{req.edited->width(), req.edited->height()};
        if (!(e == rotated)) {
            const double ar = static_cast<double>(e.w) / e.h, br = static_cast<double>(rotated.w) / rotated.h;
            if (std::abs(ar / br - 1.0) >= 0.005)
                return makeError(
                    Errc::invalid_argument,
                    "The graded file has a different aspect ratio; manual alignment is required");
            if (!req.adjust.allowScale)
                return makeError(Errc::needs_confirmation,
                                 "The graded file has a different size; confirm scaling");
        }
        (void)chk;
        const double sx = static_cast<double>(e.w) / rotated.w, sy = static_cast<double>(e.h) / rotated.h;
        scanToBuf = Mat3::scale(sx, sy) *
                    rotateBox(k, static_cast<double>(req.editSize.w), static_cast<double>(req.editSize.h)) *
                    req.scanToEdit;
        exactEdit = req.editExact && sx == 1.0 && sy == 1.0 && exactScan;
        auto prep = prepareEdited(req, info);
        if (!prep) return prep.error();
        ed = std::move(prep.value());
        strokeLimit = expandShape(req.inner, 4.0);
        return detail::loadRegion(scan, g.sourceRect, region, cancel, {}, 0, 0);
    }

    double weight(Vec2 p) const {
        double w = maskWeight(req.inner, req.mask, p.x, p.y);
        for (const auto& s : req.strokes) {
            if (polylineDistance(s.points, p) > s.radiusPx) continue;
            if (s.add) {
                if (contains(strokeLimit, p)) w = 1.0; // never beyond 4 px outside the image area
            } else {
                w = 0.0;
            }
        }
        return w;
    }

    /// Renders output rows [v0, v0+n) into `rows` (scan layout: channels and depth of the scan).
    void renderRows(int64_t v0, int64_t n, uint8_t* rows) const {
        const int ch = info.channels, bits = info.bitsPerSample, bs = bits / 8;
        const size_t px = static_cast<size_t>(ch) * bs;
        const RectI& R = g.sourceRect;
        const size_t regionRow = px * static_cast<size_t>(R.w);
        const int edCh = ed.channels();
        const size_t edPx = static_cast<size_t>(edCh) * bs;
        const double kscale = std::max(1.0, outToScan.m[0] != 0 || outToScan.m[1] != 0
                                                ? std::hypot(outToScan.m[0], outToScan.m[3])
                                                : 1.0);
        detail::parallelFor(n, [&](int64_t r) {
            const int64_t v = v0 + r;
            uint8_t* row = rows + static_cast<size_t>(r) * px * outSize.w;
            for (int64_t u = 0; u < outSize.w; ++u) {
                const Vec2 p = outToScan.apply({u + 0.5, v + 0.5});
                uint8_t* o = row + u * px;
                float sv[4] = {0, 0, 0, 0};
                if (exactScan) {
                    const int64_t sx =
                        std::clamp<int64_t>(static_cast<int64_t>(std::floor(p.x)) - R.x, 0, R.w - 1);
                    const int64_t sy =
                        std::clamp<int64_t>(static_cast<int64_t>(std::floor(p.y)) - R.y, 0, R.h - 1);
                    std::memcpy(o, region.data() + sy * regionRow + sx * px, px);
                } else {
                    detail::lanczosSample(region.data(), R.w, R.h, ch, bits, p.x - R.x, p.y - R.y, kscale,
                                          sv);
                    for (int c = 0; c < ch; ++c)
                        detail::storeSample(o + c * bs, bits, sv[c]);
                }
                const double w = weight(p);
                if (w <= 0.0) continue;
                const Vec2 q = scanToBuf.apply(p);
                if (exactEdit && w >= 1.0) {
                    const int64_t ex =
                        std::clamp<int64_t>(static_cast<int64_t>(std::floor(q.x)), 0, ed.width() - 1);
                    const int64_t ey =
                        std::clamp<int64_t>(static_cast<int64_t>(std::floor(q.y)), 0, ed.height() - 1);
                    std::memcpy(o, ed.row(ey) + ex * edPx, edPx); // colour channels; scan alpha (if any) kept
                    continue;
                }
                float ev[4];
                if (exactEdit) {
                    const int64_t ex =
                        std::clamp<int64_t>(static_cast<int64_t>(std::floor(q.x)), 0, ed.width() - 1);
                    const int64_t ey =
                        std::clamp<int64_t>(static_cast<int64_t>(std::floor(q.y)), 0, ed.height() - 1);
                    for (int c = 0; c < edCh; ++c)
                        ev[c] = ed.samplef(ex, ey, c);
                } else {
                    detail::lanczosSample(ed.data(), ed.width(), ed.height(), edCh, bits, q.x, q.y, 1.0, ev);
                }
                for (int c = 0; c < edCh; ++c) {
                    const float s = detail::sampleAt(o, px, ch, bits, 0, 0, c);
                    detail::storeSample(o + c * bs, bits, static_cast<float>(w * ev[c] + (1.0 - w) * s));
                }
            }
        });
    }
};

} // namespace

double edgeChange(const ImageBuffer& composite, const ImageBuffer& scanOnly, const Shape& inner, Vec2 origin,
                  double scale, double pxPerMm) {
    if (composite.size() != scanOnly.size() || composite.channels() != scanOnly.channels() || scale <= 0)
        return 0;
    const int ch = composite.channels();
    const double lo = 0.2 * pxPerMm, hi = 0.6 * pxPerMm;
    std::vector<double> d;
    for (int64_t v = 0; v < composite.height(); ++v)
        for (int64_t u = 0; u < composite.width(); ++u) {
            const Vec2 p = origin + Vec2{(u + 0.5) / scale, (v + 0.5) / scale};
            const double dist = signedDistance(inner, p);
            if (dist < lo || dist > hi) continue;
            float a[3], b[3], la[3], lb[3];
            for (int c = 0; c < 3; ++c) {
                a[c] = composite.samplef(u, v, std::min(c, ch - 1));
                b[c] = scanOnly.samplef(u, v, std::min(c, ch - 1));
            }
            srgbToLab(a, la);
            srgbToLab(b, lb);
            d.push_back(deltaE2000(la, lb));
        }
    if (d.empty()) return 0;
    std::nth_element(d.begin(), d.begin() + static_cast<std::ptrdiff_t>(d.size() / 2), d.end());
    return d[d.size() / 2];
}

MaskChoice naturalBlend(MaskMode mode, bool dpiReliable) {
    switch (mode) {
    case MaskMode::feather:
        return {mode, dpiReliable ? kNaturalFeatherMm : kNaturalFeatherPx, dpiReliable};
    case MaskMode::preserve_edge:
        return {mode, dpiReliable ? kNaturalInsetMm : kNaturalInsetPx, dpiReliable};
    default:
        return {MaskMode::hard_edge, 0, false};
    }
}

MaskParams resolveMask(const MaskChoice& c, double dpi) {
    MaskParams p;
    p.mode = c.mode;
    const double px = std::max(0.0, c.mm ? c.width * (dpi > 0 ? dpi : 300.0) / 25.4 : c.width);
    if (c.mode == MaskMode::feather) p.featherPx = px;
    if (c.mode == MaskMode::preserve_edge) p.preserveEdgePx = px;
    return p;
}

Result<ExportReport> compositeItem(ImageSource& scan, const CompositeRequest& req, const CancelToken& cancel,
                                   const ProgressFn& progress) {
    const auto t0 = std::chrono::steady_clock::now();
    BufferAccounting::instance().resetPeak();
    const ImageInfo& info = scan.info();
    Renderer rd(req, info);
    FK_TRY(rd.prepare(scan, 1.0, cancel));
    report(progress, 0.3, "composite");
    ExportReport rep;
    rep.geometry = rd.g;
    rep.resampled = rd.g.resampled;
    auto& r = rep.notExactReasons;
    if (rd.g.resampled) r.push_back("resampled: straightened");
    if (req.color.kind == ColorPolicy::convert) r.push_back("colour converted to another profile");
    if (req.write.format == FileFormat::jpeg) r.push_back("lossy JPEG compression");
    if (req.write.format == FileFormat::dng) r.push_back("linearised for Linear DNG");
    if (req.write.bits > 0 && req.write.bits < info.bitsPerSample) r.push_back("bit depth reduced to 8-bit");
    rep.frameBitExact = r.empty();

    detail::OutputStage out;
    detail::OutputStage::Params op;
    op.srcInfo = &info;
    op.srcMeta = &scan.metadata();
    op.write = req.write;
    op.color = req.color;
    op.privacy = req.privacy;
    op.ids = req.ids;
    op.output = req.output;
    op.outSize = rd.outSize;
    op.dpiScale = 1.0;
    op.allowDepthReduction = req.adjust.allowDepthReduction;
    FK_TRY(out.open(op, rep));
    constexpr int64_t kBand = 128;
    const size_t rowBytes = info.bytesPerPixel() * static_cast<size_t>(rd.outSize.w);
    auto band = AccountedBuffer::allocate(MemCategory::band, rowBytes * kBand);
    if (!band) return band.error();
    for (int64_t v = 0; v < rd.outSize.h; v += kBand) {
        if (cancel.cancelled()) {
            out.abort();
            return cancelledError();
        }
        const int64_t n = std::min(kBand, rd.outSize.h - v);
        rd.renderRows(v, n, band->data());
        auto st = out.writeRows(band->data(), n, rowBytes);
        if (!st) {
            out.abort();
            return st.error();
        }
        report(progress, 0.3 + 0.6 * static_cast<double>(v + n) / rd.outSize.h, "composite");
    }
    FK_TRY(out.finish(req.verify, cancel, rep));
    rep.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    rep.peakAccountedBytes = BufferAccounting::instance().peak();
    report(progress, 1.0, "composite");
    return rep;
}

Result<ImageBuffer> compositeToBuffer(ImageSource& scan, const CompositeRequest& req, double scale,
                                      const CancelToken& cancel) {
    const ImageInfo& info = scan.info();
    Renderer rd(req, info);
    FK_TRY(rd.prepare(scan, scale, cancel));
    auto out = ImageBuffer::create(rd.outSize.w, rd.outSize.h, info.channels, info.bitsPerSample);
    if (!out) return out.error();
    constexpr int64_t kBand = 128;
    for (int64_t v = 0; v < rd.outSize.h; v += kBand) {
        if (cancel.cancelled()) return cancelledError();
        rd.renderRows(v, std::min(kBand, rd.outSize.h - v), out->row(v));
    }
    return out;
}

// ---- synthetic frame ------------------------------------------------------------------------------
FrameLayout computeFrameLayout(SizeI pic, const FrameSpec& spec) {
    FrameLayout l;
    const double longEdge = static_cast<double>(std::max(pic.w, pic.h));
    auto px = [&](double v) -> int64_t {
        const double p = spec.unit == FrameUnit::percent_long_edge ? v / 100.0 * longEdge : v;
        return std::max<int64_t>(0, static_cast<int64_t>(std::floor(p + 0.5)));
    };
    switch (spec.mode) {
    case FrameSpecMode::equal:
        l.top = l.right = l.bottom = l.left = px(spec.top);
        break;
    case FrameSpecMode::sides:
        l.top = px(spec.top);
        l.right = px(spec.right);
        l.bottom = px(spec.bottom);
        l.left = px(spec.left);
        break;
    case FrameSpecMode::canvas_ratio: {
        const int64_t m = px(spec.minMargin);
        const double rw = spec.ratioW, rh = spec.ratioH;
        const double needW = static_cast<double>(pic.w + 2 * m), needH = static_cast<double>(pic.h + 2 * m);
        double cw = needW, ch = needW * rh / rw;
        if (ch < needH) {
            ch = needH;
            cw = needH * rw / rh;
        }
        const int64_t W = static_cast<int64_t>(std::ceil(cw - 1e-9)),
                      H = static_cast<int64_t>(std::ceil(ch - 1e-9));
        l.left = (W - pic.w) / 2;
        l.right = W - pic.w - l.left;
        l.top = (H - pic.h) / 2;
        l.bottom = H - pic.h - l.top;
        break;
    }
    }
    l.canvas = {pic.w + l.left + l.right, pic.h + l.top + l.bottom};
    l.offsetX = l.left;
    l.offsetY = l.top;
    return l;
}

Result<ImageBuffer> renderSyntheticFrame(const ImageBuffer& pic, const FrameSpec& spec) {
    FrameLayout l = computeFrameLayout(pic.size(), spec);
    const int ch = pic.channels(), bits = pic.bits(), bs = bits / 8;
    auto outR = ImageBuffer::create(l.canvas.w, l.canvas.h, ch, bits);
    if (!outR) return outR.error();
    ImageBuffer out = std::move(outR.value());
    std::vector<uint8_t> fill(static_cast<size_t>(ch) * bs);
    const int color = (ch == 2 || ch == 4) ? ch - 1 : ch;
    for (int c = 0; c < ch; ++c) {
        float v = 1.0f;
        if (c < color)
            v = color == 1
                    ? (0.2126f * spec.color[0] + 0.7152f * spec.color[1] + 0.0722f * spec.color[2]) / 65535.f
                    : spec.color[c] / 65535.f;
        detail::storeSample(&fill[c * bs], bits, v);
    }
    for (int64_t y = 0; y < l.canvas.h; ++y) {
        uint8_t* row = out.row(y);
        for (int64_t x = 0; x < l.canvas.w; ++x)
            std::memcpy(row + x * ch * bs, fill.data(), fill.size());
        if (y >= l.offsetY && y < l.offsetY + pic.height())
            std::memcpy(row + l.offsetX * ch * bs, pic.row(y - l.offsetY), pic.rowBytes());
    }
    if (!spec.platformTarget) return out;
    // pad to the platform ratio, then Lanczos resize to the exact target
    const SizeI T = *spec.platformTarget;
    FrameSpec pad;
    pad.mode = FrameSpecMode::canvas_ratio;
    pad.unit = FrameUnit::pixels;
    pad.ratioW = static_cast<double>(T.w);
    pad.ratioH = static_cast<double>(T.h);
    pad.minMargin = 0;
    pad.color = spec.color;
    auto padded = renderSyntheticFrame(out, pad);
    if (!padded) return padded.error();
    auto resR = ImageBuffer::create(T.w, T.h, ch, bits);
    if (!resR) return resR.error();
    ImageBuffer res = std::move(resR.value());
    const double sx = static_cast<double>(padded->width()) / T.w,
                 sy = static_cast<double>(padded->height()) / T.h;
    const double k = std::max(1.0, std::max(sx, sy));
    detail::parallelFor(T.h, [&](int64_t y) {
        for (int64_t x = 0; x < T.w; ++x) {
            float v[4];
            detail::lanczosSample(padded->data(), padded->width(), padded->height(), ch, bits, (x + 0.5) * sx,
                                  (y + 0.5) * sy, k, v);
            for (int c = 0; c < ch; ++c)
                detail::storeSample(res.row(y) + (x * ch + c) * bs, bits, v[c]);
        }
    });
    return res;
}

Result<std::array<uint16_t, 3>> sampleFrameColor(ImageSource& scan, const Shape& outer, const Shape& inner) {
    const ImageInfo& info = scan.info();
    const RectI R = boundingRect(outer, SizeI{info.width, info.height});
    if (R.empty()) return makeError(Errc::invalid_argument, "Empty print");
    std::vector<double> ch[3];
    const int64_t step = std::max<int64_t>(1, std::max(R.w, R.h) / 400);
    std::vector<uint8_t> row(info.rowBytes());
    const Shape innerGrow = expandShape(inner, 3.0);
    const Shape outerShrink = expandShape(outer, -3.0);
    for (int64_t y = R.y; y < R.bottom(); y += step) {
        FK_TRY(scan.readRows(y, 1, 0, info.width, row.data(), row.size()));
        for (int64_t x = R.x; x < R.right(); x += step) {
            const Vec2 p{x + 0.5, y + 0.5};
            if (!contains(outerShrink, p) || contains(innerGrow, p)) continue;
            for (int c = 0; c < 3; ++c) {
                const int sc = info.channels >= 3 ? c : 0;
                ch[c].push_back(
                    detail::sampleAt(row.data(), row.size(), info.channels, info.bitsPerSample, x, 0, sc));
            }
        }
    }
    if (ch[0].empty()) return makeError(Errc::invalid_argument, "No frame pixels found");
    std::array<uint16_t, 3> c{};
    for (int k = 0; k < 3; ++k) {
        auto& v = ch[k];
        std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
        c[k] = static_cast<uint16_t>(std::lround(std::clamp(v[v.size() / 2], 0.0, 1.0) * 65535.0));
    }
    return c;
}

} // namespace fk
