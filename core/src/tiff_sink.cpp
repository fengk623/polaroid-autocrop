// TIFF and Linear DNG streaming writers (RFC-0003, ADR-0004).
#include "fk/image_io.hpp"
#include "fk/metadata.hpp"
#include "fk/product_info.hpp"
#include "fk/version.hpp"
#include "sink_base.hpp"
#include "tiff_util.hpp"

#include <tiffio.h>

#include <algorithm>
#include <cstring>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace fk {
namespace detail {

void syncFile(const std::filesystem::path& p) {
#if defined(_WIN32)
    std::FILE* f = openFile(p, "rb+");
    if (f) {
        std::fflush(f);
        _commit(_fileno(f));
        std::fclose(f);
    }
#else
    const int fd = ::open(p.c_str(), O_RDONLY);
    if (fd >= 0) {
        ::fsync(fd);
        ::close(fd);
    }
#endif
}

Status SinkBase::commit() {
    syncFile(temp_);
    std::error_code ec;
    if (std::filesystem::exists(final_, ec)) {
        removeTemp();
        return makeError(Errc::exists, "Output file already exists; it was not overwritten",
                         pathToUtf8(final_));
    }
    std::filesystem::rename(temp_, final_, ec);
    if (ec) {
        removeTemp();
        return makeError(Errc::io_write, "Cannot move the finished file into place", ec.message());
    }
    done_ = true;
    return {};
}

void SinkBase::removeTemp() {
    std::error_code ec;
    std::filesystem::remove(temp_, ec);
}

Status SinkBase::checkRows(int64_t n) const {
    if (!begun_) return makeError(Errc::internal, "writeRows before begin");
    if (rowsWritten_ + n > size_.h) return makeError(Errc::internal, "Too many rows written");
    return {};
}

} // namespace detail

namespace {

class TiffSink final : public detail::SinkBase {
  public:
    TiffSink(std::filesystem::path p, WriteOptions o, bool dng)
        : SinkBase(std::move(p), std::move(o)), dng_(dng) {}
    ~TiffSink() override {
        if (!done_) abort();
    }

    Status begin(SizeI size, int channels, int bits) override {
        if (begun_) return makeError(Errc::internal, "begin called twice");
        if (bits != 8 && bits != 16)
            return makeError(Errc::invalid_argument, "TIFF output supports 8 or 16 bits");
        if (channels < 1 || channels > 4)
            return makeError(Errc::invalid_argument, "Unsupported channel count");
        if (dng_ && (channels != 3 || bits != 16))
            return makeError(Errc::invalid_argument, "Linear DNG needs 16-bit RGB");
        size_ = size;
        channels_ = channels;
        bits_ = bits;
        const uint64_t est = estimateOutputBytes(size, channels, bits, opt_);
        const bool big = est > 3'900'000'000ull;
        if (big && !opt_.allowBigTiff)
            return makeError(Errc::capability, "Output would exceed 4 GB and BigTIFF is disabled");
        detail::clearTiffError();
        t_ = detail::tiffOpenPath(temp_, big ? "w8" : "w");
        if (!t_) return makeError(Errc::io_write, "Cannot create output file", detail::lastTiffError());
        const bool gray = channels <= 2;
        const bool alpha = channels == 2 || channels == 4;
        TIFFSetField(t_, TIFFTAG_IMAGEWIDTH, static_cast<uint32_t>(size.w));
        TIFFSetField(t_, TIFFTAG_IMAGELENGTH, static_cast<uint32_t>(size.h));
        TIFFSetField(t_, TIFFTAG_SAMPLESPERPIXEL, static_cast<uint16_t>(channels));
        TIFFSetField(t_, TIFFTAG_BITSPERSAMPLE, static_cast<uint16_t>(bits));
        TIFFSetField(t_, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT);
        TIFFSetField(t_, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
        TIFFSetField(t_, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
        if (dng_) {
            TIFFSetField(t_, TIFFTAG_SUBFILETYPE, 0);
            TIFFSetField(t_, TIFFTAG_PHOTOMETRIC, 34892); // LinearRaw
        } else {
            TIFFSetField(t_, TIFFTAG_PHOTOMETRIC, gray ? PHOTOMETRIC_MINISBLACK : PHOTOMETRIC_RGB);
        }
        if (alpha) {
            const uint16_t es[1] = {EXTRASAMPLE_UNASSALPHA};
            TIFFSetField(t_, TIFFTAG_EXTRASAMPLES, 1, es);
        }
        uint16_t comp = COMPRESSION_NONE;
        switch (opt_.compression) {
        case Compression::none:
            comp = COMPRESSION_NONE;
            break;
        case Compression::lzw:
            comp = COMPRESSION_LZW;
            break;
        case Compression::deflate:
            comp = COMPRESSION_ADOBE_DEFLATE;
            break;
        case Compression::packbits:
            comp = COMPRESSION_PACKBITS;
            break;
        default:
            return makeError(Errc::invalid_argument, "Unsupported TIFF output compression");
        }
        if (dng_ && comp != COMPRESSION_NONE && comp != COMPRESSION_ADOBE_DEFLATE) comp = COMPRESSION_NONE;
        TIFFSetField(t_, TIFFTAG_COMPRESSION, comp);
        if ((comp == COMPRESSION_LZW || comp == COMPRESSION_ADOBE_DEFLATE) && opt_.predictor)
            TIFFSetField(t_, TIFFTAG_PREDICTOR, PREDICTOR_HORIZONTAL);
        const size_t rowBytes = static_cast<size_t>(size.w) * channels * (bits / 8);
        const uint32_t rps = static_cast<uint32_t>(
            std::clamp<size_t>((512u << 10) / std::max<size_t>(1, rowBytes), 1, static_cast<size_t>(size.h)));
        TIFFSetField(t_, TIFFTAG_ROWSPERSTRIP, rps);
        if (opt_.dpiX > 0 && opt_.dpiY > 0) {
            TIFFSetField(t_, TIFFTAG_RESOLUTIONUNIT, RESUNIT_INCH);
            TIFFSetField(t_, TIFFTAG_XRESOLUTION, static_cast<float>(opt_.dpiX));
            TIFFSetField(t_, TIFFTAG_YRESOLUTION, static_cast<float>(opt_.dpiY));
        }
        if (!opt_.icc.empty() && !dng_)
            TIFFSetField(t_, TIFFTAG_ICCPROFILE, static_cast<uint32_t>(opt_.icc.size()), opt_.icc.data());
        if (!opt_.xmp.empty())
            TIFFSetField(t_, TIFFTAG_XMLPACKET, static_cast<uint32_t>(opt_.xmp.size()), opt_.xmp.data());
        const std::string sw = productNameAndVersion();
        TIFFSetField(t_, TIFFTAG_SOFTWARE, sw.c_str());
        const std::string now = tiffDateTimeNow();
        TIFFSetField(t_, TIFFTAG_DATETIME, now.c_str());
        auto setStr = [&](uint32_t tag, const std::string& v) {
            if (!v.empty()) TIFFSetField(t_, tag, v.c_str());
        };
        setStr(TIFFTAG_MAKE, opt_.meta.make);
        setStr(TIFFTAG_MODEL, opt_.meta.model);
        setStr(TIFFTAG_ARTIST, opt_.meta.artist);
        setStr(TIFFTAG_COPYRIGHT, opt_.meta.copyright);
        setStr(TIFFTAG_IMAGEDESCRIPTION, opt_.meta.imageDescription);
        setStr(TIFFTAG_DOCUMENTNAME, opt_.meta.documentName);
        setStr(TIFFTAG_HOSTCOMPUTER, opt_.meta.hostComputer);
        if (dng_) setDngTags();
        scratch_.resize(rowBytes);
        begun_ = true;
        return {};
    }

    Status writeRows(const uint8_t* rows, int64_t n, size_t stride) override {
        FK_TRY(checkRows(n));
        for (int64_t i = 0; i < n; ++i) {
            // libtiff's predictor modifies the buffer in place: always write from a scratch copy.
            std::memcpy(scratch_.data(), rows + static_cast<size_t>(i) * stride, scratch_.size());
            detail::clearTiffError();
            if (TIFFWriteScanline(t_, scratch_.data(), static_cast<uint32_t>(rowsWritten_), 0) < 0) {
                const std::string e = detail::lastTiffError();
                return makeError(e.find("space") != std::string::npos ? Errc::disk_full : Errc::io_write,
                                 "Writing the output file failed", e);
            }
            ++rowsWritten_;
        }
        return {};
    }

    Status finish() override {
        if (!begun_ || rowsWritten_ != size_.h)
            return makeError(Errc::internal, "finish before all rows were written");
        detail::clearTiffError();
        const int ok = TIFFFlush(t_);
        TIFFClose(t_);
        t_ = nullptr;
        if (!ok) {
            removeTemp();
            return makeError(Errc::io_write, "Writing the output file failed", detail::lastTiffError());
        }
        return commit();
    }

    void abort() override {
        if (t_) {
            TIFFClose(t_);
            t_ = nullptr;
        }
        removeTemp();
        done_ = true;
    }

  private:
    void setDngTags() {
        static const uint8_t ver[4] = {1, 4, 0, 0};
        static const uint8_t back[4] = {1, 1, 0, 0};
        TIFFSetField(t_, TIFFTAG_DNGVERSION, ver);
        TIFFSetField(t_, TIFFTAG_DNGBACKWARDVERSION, back);
        TIFFSetField(t_, TIFFTAG_UNIQUECAMERAMODEL, "FrameKeeper Linear Scan");
        TIFFSetField(t_, TIFFTAG_MAKE, "FrameKeeper");
        TIFFSetField(t_, TIFFTAG_MODEL, "Linear Scan");
        if (opt_.dngColorMatrix.size() == 9) {
            float cm[9];
            for (int i = 0; i < 9; ++i)
                cm[i] = static_cast<float>(opt_.dngColorMatrix[i]);
            TIFFSetField(t_, TIFFTAG_COLORMATRIX1, 9, cm);
        }
        TIFFSetField(t_, TIFFTAG_CALIBRATIONILLUMINANT1,
                     static_cast<uint16_t>(23)); // D50 (matrix is D50-adapted)
        float neutral[3] = {1.0f, 1.0f, 1.0f};
        TIFFSetField(t_, TIFFTAG_ASSHOTNEUTRAL, 3, neutral);
        float baseline = 0.0f;
        TIFFSetField(t_, TIFFTAG_BASELINEEXPOSURE, baseline);
        uint32_t white[3] = {65535, 65535, 65535};
        TIFFSetField(t_, TIFFTAG_WHITELEVEL, 3, white);
        const std::string prof = "FrameKeeper Linear Scan";
        TIFFSetField(t_, TIFFTAG_PROFILENAME, prof.c_str());
    }

    TIFF* t_ = nullptr;
    bool dng_ = false;
    std::vector<uint8_t> scratch_;
};

} // namespace

std::unique_ptr<ImageSink> makeTiffSink(const std::filesystem::path& p, const WriteOptions& o, bool dng) {
    return std::make_unique<TiffSink>(p, o, dng);
}

} // namespace fk
