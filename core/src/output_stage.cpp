#include "output_stage.hpp"

#include "fk/util.hpp"
#include "tiff_util.hpp"

#include <tiffio.h>

namespace fk::detail {

Result<OutputLayout> computeOutputLayout(const ImageInfo& in, const WriteOptions& write,
                                         bool allowDepthReduction) {
    OutputLayout l;
    l.srcCh = in.channels;
    l.srcBits = in.bitsPerSample;
    l.srcAlpha = in.hasAlpha;
    l.outCh = in.channels;
    l.outBits = write.bits > 0 ? write.bits : in.bitsPerSample;
    if (write.format == FileFormat::jpeg) {
        l.outBits = 8;
        if (in.hasAlpha) {
            l.outCh = in.channels - 1;
            l.notes.push_back("alpha channel dropped (JPEG has no alpha)");
        }
    }
    if (write.format == FileFormat::dng) {
        if (in.photometric != Photometric::rgb)
            return makeError(Errc::unsupported_format, "Linear DNG export needs an RGB scan");
        l.outCh = 3;
        l.outBits = 16;
        if (in.hasAlpha) l.notes.push_back("alpha channel dropped (Linear DNG)");
    }
    if (l.outBits != 8 && l.outBits != 16)
        return makeError(Errc::invalid_argument, "Output bit depth must be 8 or 16");
    if (l.outBits < l.srcBits && !allowDepthReduction)
        return makeError(Errc::needs_confirmation, "This export would reduce 16-bit data to 8-bit",
                         "set allowDepthReduction after the user confirms");
    return l;
}

namespace {

Status verifyOutput(const std::filesystem::path& p, const ExportReport& rep, const WriteOptions& w,
                    const std::string& expectedPixelHash) {
    if (w.format == FileFormat::dng) {
        TIFF* t = tiffOpenPath(p, "rm");
        if (!t) return makeError(Errc::internal, "Verification failed: DNG cannot be reopened");
        uint32_t wd = 0, ht = 0;
        uint16_t spp = 0, bps = 0, photo = 0;
        TIFFGetField(t, TIFFTAG_IMAGEWIDTH, &wd);
        TIFFGetField(t, TIFFTAG_IMAGELENGTH, &ht);
        TIFFGetFieldDefaulted(t, TIFFTAG_SAMPLESPERPIXEL, &spp);
        TIFFGetFieldDefaulted(t, TIFFTAG_BITSPERSAMPLE, &bps);
        TIFFGetField(t, TIFFTAG_PHOTOMETRIC, &photo);
        uint8_t* ver = nullptr;
        const bool hasVer = TIFFGetField(t, TIFFTAG_DNGVERSION, &ver) && ver;
        TIFFClose(t);
        if (wd != rep.size.w || ht != rep.size.h || spp != 3 || bps != 16 || photo != 34892 || !hasVer)
            return makeError(Errc::internal, "Verification failed: DNG structure is not as written");
        return {};
    }
    ImageInfo ii;
    if (w.format == FileFormat::tiff) {
        auto src = openTiff(p);
        if (!src)
            return makeError(Errc::internal, "Verification failed: output cannot be reopened",
                             src.error().message);
        ii = (*src)->info();
        if (ii.width != rep.size.w || ii.height != rep.size.h || ii.bitsPerSample != rep.bits ||
            ii.channels != rep.channels)
            return makeError(Errc::internal, "Verification failed: size or depth differs");
        if (ii.icc != w.icc) return makeError(Errc::internal, "Verification failed: ICC profile differs");
        if (w.dpiX > 0 && std::abs(ii.dpiX - w.dpiX) > 0.01 * w.dpiX)
            return makeError(Errc::internal, "Verification failed: resolution differs");
        Sha256 h;
        std::vector<uint8_t> row(ii.rowBytes());
        for (int64_t y = 0; y < ii.height; ++y) {
            FK_TRY((*src)->readRows(y, 1, 0, ii.width, row.data(), row.size()));
            h.update(row.data(), row.size());
        }
        if (h.finishHex() != expectedPixelHash)
            return makeError(Errc::internal, "Verification failed: pixel data differs");
        return {};
    }
    MetadataSet meta;
    auto img = readImageFile(p, &ii, &meta);
    if (!img)
        return makeError(Errc::internal, "Verification failed: output cannot be decoded",
                         img.error().message);
    if (ii.width != rep.size.w || ii.height != rep.size.h)
        return makeError(Errc::internal, "Verification failed: size differs");
    if (ii.icc != w.icc) return makeError(Errc::internal, "Verification failed: ICC profile differs");
    if (w.format == FileFormat::png) {
        if (ii.bitsPerSample != rep.bits || ii.channels != rep.channels)
            return makeError(Errc::internal, "Verification failed: depth differs");
        Sha256 h;
        for (int64_t y = 0; y < img->height(); ++y)
            h.update(img->row(y), img->rowBytes());
        if (h.finishHex() != expectedPixelHash)
            return makeError(Errc::internal, "Verification failed: pixel data differs");
    }
    return {};
}

} // namespace

OutputStage::~OutputStage() {
    if (open_) abort();
}

Status OutputStage::open(const Params& p, ExportReport& rep) {
    const ImageInfo& in = *p.srcInfo;
    auto lay = computeOutputLayout(in, p.write, p.allowDepthReduction);
    if (!lay) return lay.error();
    lay_ = *lay;
    size_ = p.outSize;
    w_ = p.write;
    w_.bits = lay_.outBits;
    w_.meta = applyPrivacy(*p.srcMeta, p.privacy);
    w_.xmp = buildXmp(p.ids, &w_.meta);
    if (in.dpiX > 0) {
        w_.dpiX = in.dpiX * p.dpiScale;
        w_.dpiY = in.dpiY * p.dpiScale;
    }
    std::optional<Transform> xf;
    if (p.write.format == FileFormat::dng) {
        if (in.icc.empty())
            return makeError(Errc::color_profile,
                             "Linear DNG export needs a scan with an ICC profile (assign one first)");
        auto sp = Profile::fromIcc(in.icc);
        if (!sp) return sp.error();
        auto lin = Profile::linearized(*sp);
        if (!lin) return lin.error();
        auto t = Transform::create(*sp, {3, lay_.srcBits, false}, *lin, {3, 16, false},
                                   Intent::relative_colorimetric, false);
        if (!t) return t.error();
        xf = *t;
        std::array<double, 9> m{};
        sp->colorants(m);
        const Mat3 M{{m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8]}};
        auto inv = M.inverse();
        if (!inv) return makeError(Errc::color_profile, "Profile colourants are singular");
        w_.dngColorMatrix.assign(inv->m.begin(), inv->m.end());
        w_.icc.clear();
    } else {
        switch (p.color.kind) {
        case ColorPolicy::keep:
            w_.icc = in.icc;
            break;
        case ColorPolicy::untagged:
            w_.icc.clear();
            break;
        case ColorPolicy::assign:
            w_.icc = p.color.icc;
            break;
        case ColorPolicy::convert: {
            if (in.icc.empty())
                return makeError(Errc::color_profile,
                                 "The scan has no ICC profile; assign one before converting");
            auto sp = Profile::fromIcc(in.icc);
            if (!sp) return sp.error();
            auto dp = Profile::fromIcc(p.color.icc);
            if (!dp) return dp.error();
            const int sColor = lay_.srcAlpha ? lay_.srcCh - 1 : lay_.srcCh;
            const bool outAlpha = lay_.outCh == 2 || lay_.outCh == 4;
            auto t = Transform::create(
                *sp, {sColor, lay_.srcBits, lay_.srcAlpha && outAlpha}, *dp,
                {outAlpha ? lay_.outCh - 1 : lay_.outCh, lay_.outBits, lay_.srcAlpha && outAlpha},
                p.color.intent, p.color.bpc);
            if (!t) return t.error();
            xf = *t;
            w_.icc = p.color.icc;
            break;
        }
        }
    }
    rep.output = p.output;
    rep.size = p.outSize;
    rep.bits = lay_.outBits;
    rep.channels = lay_.outCh;
    rep.format = p.write.format;
    rep.notes.insert(rep.notes.end(), lay_.notes.begin(), lay_.notes.end());
    rep.iccDescription = iccDescription(w_.icc);
    rep.iccSha256 = w_.icc.empty() ? std::string() : sha256Hex(w_.icc.data(), w_.icc.size());
    rep.dpiX = w_.dpiX;
    rep.dpiY = w_.dpiY;
    FK_TRY(conv_.init(lay_.srcCh, lay_.srcBits, lay_.outCh, lay_.outBits, xf, lay_.srcAlpha));
    auto sink = createSink(p.output, w_);
    if (!sink) return sink.error();
    sink_ = std::move(*sink);
    FK_TRY(sink_->begin(p.outSize, lay_.outCh, lay_.outBits));
    auto row = AccountedBuffer::allocate(MemCategory::band,
                                         static_cast<size_t>(p.outSize.w) * lay_.outCh * (lay_.outBits / 8));
    if (!row) return row.error();
    outRow_ = std::move(row.value());
    sig_ = std::make_unique<SignatureAccumulator>(p.outSize.w, p.outSize.h);
    open_ = true;
    return {};
}

Status OutputStage::writeRows(const uint8_t* rows, int64_t n, size_t stride) {
    for (int64_t r = 0; r < n; ++r) {
        conv_.convert(rows + static_cast<size_t>(r) * stride, outRow_.data(), size_.w);
        hash_.update(outRow_.data(), outRow_.size());
        sig_->addRows(y_, 1, outRow_.data(), outRow_.size(), lay_.outCh, lay_.outBits);
        FK_TRY(sink_->writeRows(outRow_.data(), 1, outRow_.size()));
        ++y_;
    }
    return {};
}

Status OutputStage::finish(bool verify, const CancelToken& cancel, ExportReport& rep) {
    FK_TRY(sink_->finish());
    open_ = false;
    rep.pixelSha256 = hash_.finishHex();
    rep.signature = sig_->finish();
    if (verify) {
        auto v = verifyOutput(rep.output, rep, w_, rep.pixelSha256);
        if (!v) {
            std::error_code ec;
            std::filesystem::remove(rep.output, ec);
            return v.error();
        }
    }
    auto fh = sha256File(rep.output, &cancel);
    if (!fh) return fh.error();
    rep.sha256 = *fh;
    return {};
}

void OutputStage::abort() {
    if (sink_) sink_->abort();
    open_ = false;
}

} // namespace fk::detail
