// TIFF reading (RFC-0003, ADR-0002): tiles / small strips / forward scanline cursor.
#include "fk/color.hpp"
#include "fk/image_io.hpp"
#include "fk/util.hpp"
#include "tiff_util.hpp"

#include <tiffio.h>

#include <algorithm>
#include <cstring>
#include <limits>

namespace fk {

const char* tiffAccessPathName(TiffAccessPath p) {
    switch (p) {
    case TiffAccessPath::tiles:
        return "tiles";
    case TiffAccessPath::strips:
        return "strips";
    case TiffAccessPath::scanlines:
        return "scanlines";
    }
    return "strips";
}

namespace {

std::string tagString(TIFF* t, uint32_t tag) {
    char* v = nullptr;
    if (TIFFGetField(t, tag, &v) && v) return std::string(v);
    return {};
}

void addField(MetadataSet& m, const char* name, const std::string& v, PrivacyClass c) {
    if (v.empty()) return;
    m.fields.push_back({name, v.size() > 200 ? v.substr(0, 200) : v, c});
}

/// Reads descriptive metadata. EXIF/GPS directories are read through a separate handle so that the
/// pixel handle's directory state is never disturbed.
MetadataSet readMetadata(TIFF* t, const std::filesystem::path& path) {
    MetadataSet m;
    m.make = tagString(t, TIFFTAG_MAKE);
    m.model = tagString(t, TIFFTAG_MODEL);
    m.dateTime = tagString(t, TIFFTAG_DATETIME);
    m.artist = tagString(t, TIFFTAG_ARTIST);
    m.copyright = tagString(t, TIFFTAG_COPYRIGHT);
    m.imageDescription = tagString(t, TIFFTAG_IMAGEDESCRIPTION);
    m.documentName = tagString(t, TIFFTAG_DOCUMENTNAME);
    m.hostComputer = tagString(t, TIFFTAG_HOSTCOMPUTER);
    addField(m, "Make", m.make, PrivacyClass::safe);
    addField(m, "Model", m.model, PrivacyClass::safe);
    addField(m, "Software", tagString(t, TIFFTAG_SOFTWARE), PrivacyClass::safe);
    addField(m, "DateTime", m.dateTime, PrivacyClass::safe);
    addField(m, "Artist", m.artist, PrivacyClass::identity);
    addField(m, "Copyright", m.copyright, PrivacyClass::safe);
    addField(m, "ImageDescription", m.imageDescription, PrivacyClass::safe);
    addField(m, "DocumentName", m.documentName, PrivacyClass::safe);
    addField(m, "HostComputer", m.hostComputer, PrivacyClass::identity);

    uint32_t xmpLen = 0;
    void* xmp = nullptr;
    if (TIFFGetField(t, TIFFTAG_XMLPACKET, &xmpLen, &xmp) && xmp && xmpLen) {
        m.sourceXmp.assign(static_cast<const char*>(xmp), xmpLen);
        m.fields.push_back({"XMP", std::to_string(xmpLen) + " bytes", PrivacyClass::unknown});
    }
    toff_t exifOff = 0, gpsOff = 0;
    const bool hasExif = TIFFGetField(t, TIFFTAG_EXIFIFD, &exifOff) && exifOff;
    m.hasGps = TIFFGetField(t, TIFFTAG_GPSIFD, &gpsOff) && gpsOff;
    if (m.hasGps) m.fields.push_back({"GPSInfo", "present", PrivacyClass::location});
    if (hasExif) {
        TIFF* e = detail::tiffOpenPath(path, "rm");
        if (e) {
            if (TIFFReadEXIFDirectory(e, exifOff)) {
                m.dateTimeOriginal = tagString(e, EXIFTAG_DATETIMEORIGINAL);
                addField(m, "Exif.DateTimeOriginal", m.dateTimeOriginal, PrivacyClass::safe);
                addField(m, "Exif.BodySerialNumber", tagString(e, EXIFTAG_BODYSERIALNUMBER),
                         PrivacyClass::identity);
                addField(m, "Exif.CameraOwnerName", tagString(e, EXIFTAG_CAMERAOWNERNAME),
                         PrivacyClass::identity);
                addField(m, "Exif.LensSerialNumber", tagString(e, EXIFTAG_LENSSERIALNUMBER),
                         PrivacyClass::identity);
            }
            TIFFClose(e);
        }
    }
    return m;
}

class TiffSourceImpl final : public TiffSource {
  public:
    ~TiffSourceImpl() override {
        cache_.clear();
        for (TIFF* h : planeHandles_)
            if (h) TIFFClose(h);
        if (tif_) TIFFClose(tif_);
    }

    Status init(const std::filesystem::path& path, const TiffOpenOptions& opt) {
        path_ = path;
        budget_ = opt.cacheBudgetBytes;
        detail::clearTiffError();
        tif_ = detail::tiffOpenPath(path, "rm");
        if (!tif_) {
            std::error_code ec;
            if (!std::filesystem::exists(path, ec))
                return makeError(Errc::not_found, "File not found", pathToUtf8(path));
            return makeError(Errc::unsupported_format, "Not a readable TIFF file", detail::lastTiffError());
        }
        FK_TRY(detail::readTiffInfo(tif_, path, info_, raw_));
        meta_ = readMetadata(tif_, path);
        if (raw_.ycbcrJpeg) TIFFSetField(tif_, TIFFTAG_JPEGCOLORMODE, JPEGCOLORMODE_RGB);

        if (info_.tiled) {
            path_kind_ = TiffAccessPath::tiles;
        } else {
            const tmsize_t ss = TIFFStripSize(tif_);
            uint32_t rps = 0;
            TIFFGetFieldDefaulted(tif_, TIFFTAG_ROWSPERSTRIP, &rps);
            rowsPerStrip_ =
                std::max<uint32_t>(1, std::min<uint32_t>(rps, static_cast<uint32_t>(info_.height)));
            path_kind_ = (ss > 0 && static_cast<uint64_t>(ss) <= opt.maxCachedStripBytes)
                             ? TiffAccessPath::strips
                             : TiffAccessPath::scanlines;
        }
        if (path_kind_ == TiffAccessPath::scanlines) {
            const int planes = info_.planarSeparate ? raw_.spp : 1;
            planeHandles_.assign(planes, nullptr);
            planeRow_.assign(planes, -1);
            planeHandles_[0] = tif_;
            for (int p = 1; p < planes; ++p) {
                planeHandles_[p] = detail::tiffOpenPath(path, "rm");
                if (!planeHandles_[p]) return makeError(Errc::io_open, "Cannot reopen TIFF for plane access");
            }
            const tmsize_t sl = TIFFScanlineSize(tif_);
            auto lb = AccountedBuffer::allocate(MemCategory::decode_cache, static_cast<size_t>(sl) * planes);
            if (!lb) return lb.error();
            lineBuf_ = std::move(lb.value());
            tif_ = nullptr; // owned by planeHandles_[0] now
        }
        return {};
    }

    const ImageInfo& info() const override { return info_; }
    const MetadataSet& metadata() const override { return meta_; }
    TiffAccessPath accessPath() const override { return path_kind_; }
    void setCacheBudget(uint64_t bytes) override {
        budget_ = bytes;
        evict(0);
    }
    uint64_t decodeCount() const override { return decodes_; }
    uint64_t restartCount() const override { return restarts_; }

    Status readRows(int64_t y0, int64_t rows, int64_t x0, int64_t width, uint8_t* dst,
                    size_t stride) override {
        if (y0 < 0 || rows < 0 || x0 < 0 || width < 0 || y0 + rows > info_.height || x0 + width > info_.width)
            return makeError(Errc::invalid_argument, "Row request outside the image");
        ++requestId_;
        Status st;
        switch (path_kind_) {
        case TiffAccessPath::tiles:
            st = readTiles(y0, rows, x0, width, dst, stride);
            break;
        case TiffAccessPath::strips:
            st = readStrips(y0, rows, x0, width, dst, stride);
            break;
        case TiffAccessPath::scanlines:
            st = readScanlines(y0, rows, x0, width, dst, stride);
            break;
        }
        if (!st) return st;
        if (raw_.minIsWhite) invertGray(rows, width, dst, stride);
        return {};
    }

  private:
    struct Chunk {
        uint32_t index = 0;
        AccountedBuffer buf;
        tmsize_t bytes = 0;
        uint64_t lastUse = 0;
        uint64_t request = 0;
    };

    // ---- cache ----
    Result<const Chunk*> chunk(uint32_t index) {
        for (auto& c : cache_) {
            if (c.index == index) {
                c.lastUse = ++tick_;
                c.request = requestId_;
                return &c;
            }
        }
        const bool tiled = path_kind_ == TiffAccessPath::tiles;
        const tmsize_t size = tiled ? TIFFTileSize(tif_) : TIFFStripSize(tif_);
        if (size <= 0) return makeError(Errc::damaged_file, "Invalid strip/tile size");
        evict(static_cast<uint64_t>(size));
        auto b = AccountedBuffer::allocate(MemCategory::decode_cache, static_cast<size_t>(size));
        if (!b) return b.error();
        detail::clearTiffError();
        const tmsize_t got = tiled ? TIFFReadEncodedTile(tif_, index, b->data(), size)
                                   : TIFFReadEncodedStrip(tif_, index, b->data(), size);
        if (got < 0)
            return makeError(Errc::damaged_file, "The file is damaged: a data block cannot be decoded",
                             detail::lastTiffError() + " (chunk " + std::to_string(index) + ")");
        ++decodes_;
        Chunk c;
        c.index = index;
        c.bytes = got;
        c.buf = std::move(b.value());
        c.lastUse = ++tick_;
        c.request = requestId_;
        used_ += c.buf.size();
        cache_.push_back(std::move(c));
        return &cache_.back();
    }

    void evict(uint64_t incoming) {
        while (!cache_.empty() && used_ + incoming > budget_) {
            auto victim = cache_.end();
            for (auto it = cache_.begin(); it != cache_.end(); ++it) {
                if (it->request == requestId_) continue; // needed by the current request
                if (victim == cache_.end() || it->lastUse < victim->lastUse) victim = it;
            }
            if (victim == cache_.end()) break;
            used_ -= victim->buf.size();
            cache_.erase(victim);
        }
    }

    // ---- strips ----
    Status readStrips(int64_t y0, int64_t rows, int64_t x0, int64_t width, uint8_t* dst, size_t stride) {
        const int bps = info_.bytesPerSample();
        const int spp = raw_.spp;
        const uint32_t stripsPerPlane = TIFFNumberOfStrips(tif_) / (info_.planarSeparate ? spp : 1);
        (void)stripsPerPlane;
        for (int64_t y = y0; y < y0 + rows; ++y) {
            const uint32_t s = static_cast<uint32_t>(y / rowsPerStrip_);
            const int64_t rowInStrip = y - static_cast<int64_t>(s) * rowsPerStrip_;
            uint8_t* out = dst + static_cast<size_t>(y - y0) * stride;
            if (!info_.planarSeparate) {
                auto c = chunk(s);
                if (!c) return c.error();
                const size_t rb = static_cast<size_t>(info_.width) * spp * bps;
                const size_t off = static_cast<size_t>(rowInStrip) * rb + static_cast<size_t>(x0) * spp * bps;
                if (off + static_cast<size_t>(width) * spp * bps > static_cast<size_t>((*c)->bytes))
                    return makeError(Errc::damaged_file, "The file is damaged: a strip is too short");
                std::memcpy(out, (*c)->buf.data() + off, static_cast<size_t>(width) * spp * bps);
            } else {
                const uint32_t spPlane = TIFFNumberOfStrips(tif_) / spp;
                for (int p = 0; p < spp; ++p) {
                    auto c = chunk(p * spPlane + s);
                    if (!c) return c.error();
                    const size_t rb = static_cast<size_t>(info_.width) * bps;
                    const uint8_t* src = (*c)->buf.data() + static_cast<size_t>(rowInStrip) * rb + x0 * bps;
                    for (int64_t x = 0; x < width; ++x)
                        std::memcpy(out + (x * spp + p) * bps, src + x * bps, bps);
                }
            }
        }
        return {};
    }

    // ---- tiles ----
    Status readTiles(int64_t y0, int64_t rows, int64_t x0, int64_t width, uint8_t* dst, size_t stride) {
        const int bps = info_.bytesPerSample();
        const int spp = raw_.spp;
        const int64_t tw = info_.tileWidth, th = info_.tileHeight;
        const int planes = info_.planarSeparate ? spp : 1;
        const int sppInTile = info_.planarSeparate ? 1 : spp;
        for (int64_t ty = y0 / th; ty * th < y0 + rows; ++ty) {
            const int64_t ry0 = std::max(y0, ty * th), ry1 = std::min(y0 + rows, (ty + 1) * th);
            for (int64_t tx = x0 / tw; tx * tw < x0 + width; ++tx) {
                const int64_t cx0 = std::max(x0, tx * tw), cx1 = std::min(x0 + width, (tx + 1) * tw);
                for (int p = 0; p < planes; ++p) {
                    const uint32_t idx =
                        TIFFComputeTile(tif_, static_cast<uint32_t>(tx * tw), static_cast<uint32_t>(ty * th),
                                        0, static_cast<uint16_t>(p));
                    auto c = chunk(idx);
                    if (!c) return c.error();
                    const size_t tileRowBytes = static_cast<size_t>(tw) * sppInTile * bps;
                    for (int64_t y = ry0; y < ry1; ++y) {
                        const uint8_t* src = (*c)->buf.data() +
                                             static_cast<size_t>(y - ty * th) * tileRowBytes +
                                             static_cast<size_t>(cx0 - tx * tw) * sppInTile * bps;
                        uint8_t* out = dst + static_cast<size_t>(y - y0) * stride +
                                       static_cast<size_t>(cx0 - x0) * spp * bps;
                        if (planes == 1) {
                            std::memcpy(out, src, static_cast<size_t>(cx1 - cx0) * spp * bps);
                        } else {
                            for (int64_t x = 0; x < cx1 - cx0; ++x)
                                std::memcpy(out + (x * spp + p) * bps, src + x * bps, bps);
                        }
                    }
                }
            }
        }
        return {};
    }

    // ---- forward scanline cursor (huge strips) ----
    Status readScanlines(int64_t y0, int64_t rows, int64_t x0, int64_t width, uint8_t* dst, size_t stride) {
        const int bps = info_.bytesPerSample();
        const int spp = raw_.spp;
        const int planes = static_cast<int>(planeHandles_.size());
        const tmsize_t sl = TIFFScanlineSize(planeHandles_[0]);
        for (int64_t y = y0; y < y0 + rows; ++y) {
            uint8_t* out = dst + static_cast<size_t>(y - y0) * stride;
            for (int p = 0; p < planes; ++p) {
                TIFF* h = planeHandles_[p];
                uint8_t* lb = lineBuf_.data() + static_cast<size_t>(sl) * p;
                // Compressed strips can only be decoded sequentially from the strip start (libtiff codecs
                // have no forward seek): skip forward by decoding, restart the strip to go backward.
                const int64_t stripStart = (y / rowsPerStrip_) * rowsPerStrip_;
                int64_t next = planeRow_[p] + 1;
                if (planeRow_[p] >= y || planeRow_[p] < stripStart - 1) {
                    if (planeRow_[p] >= y) ++restarts_;
                    next = stripStart;
                }
                for (int64_t r = next; r <= y; ++r) {
                    detail::clearTiffError();
                    if (TIFFReadScanline(h, lb, static_cast<uint32_t>(r), static_cast<uint16_t>(p)) < 0)
                        return makeError(Errc::damaged_file, "The file is damaged: a row cannot be decoded",
                                         detail::lastTiffError() + " (row " + std::to_string(r) + ")");
                    planeRow_[p] = r;
                }
                planeRow_[p] = y;
                if (planes == 1) {
                    std::memcpy(out, lb + static_cast<size_t>(x0) * spp * bps,
                                static_cast<size_t>(width) * spp * bps);
                } else {
                    for (int64_t x = 0; x < width; ++x)
                        std::memcpy(out + (x * spp + p) * bps, lb + (x0 + x) * bps, bps);
                }
            }
        }
        return {};
    }

    void invertGray(int64_t rows, int64_t width, uint8_t* dst, size_t stride) {
        const int spp = raw_.spp;
        for (int64_t y = 0; y < rows; ++y) {
            uint8_t* r = dst + static_cast<size_t>(y) * stride;
            if (info_.bitsPerSample == 8) {
                for (int64_t x = 0; x < width; ++x)
                    r[x * spp] = static_cast<uint8_t>(255 - r[x * spp]);
            } else {
                for (int64_t x = 0; x < width; ++x) {
                    uint16_t v;
                    std::memcpy(&v, r + x * spp * 2, 2);
                    v = static_cast<uint16_t>(65535 - v);
                    std::memcpy(r + x * spp * 2, &v, 2);
                }
            }
        }
    }

    std::filesystem::path path_;
    TIFF* tif_ = nullptr;
    std::vector<TIFF*> planeHandles_;
    std::vector<int64_t> planeRow_;
    AccountedBuffer lineBuf_;
    ImageInfo info_;
    MetadataSet meta_;
    detail::RawTiffLayout raw_;
    TiffAccessPath path_kind_ = TiffAccessPath::strips;
    uint32_t rowsPerStrip_ = 1;
    std::vector<Chunk> cache_;
    uint64_t budget_ = 0, used_ = 0, tick_ = 0, requestId_ = 0;
    uint64_t decodes_ = 0, restarts_ = 0;
};

} // namespace

Result<std::unique_ptr<TiffSource>> openTiff(const std::filesystem::path& path, const TiffOpenOptions& opt) {
    auto s = std::make_unique<TiffSourceImpl>();
    auto st = s->init(path, opt);
    if (!st) return st.error();
    return std::unique_ptr<TiffSource>(std::move(s));
}

Result<IntegrityReport> checkTiffIntegrity(const std::filesystem::path& path) {
    IntegrityReport rep;
    std::error_code ec;
    const uint64_t fileSize = std::filesystem::file_size(path, ec);
    if (ec) return makeError(Errc::not_found, "File not found", pathToUtf8(path));
    detail::clearTiffError();
    TIFF* t = detail::tiffOpenPath(path, "rm");
    if (!t) {
        rep.ok = false;
        rep.problems.push_back("cannot open as TIFF: " + detail::lastTiffError());
        return rep;
    }
    const uint32_t n = TIFFIsTiled(t) ? TIFFNumberOfTiles(t) : TIFFNumberOfStrips(t);
    for (uint32_t i = 0; i < n; ++i) {
        int err = 0;
        const uint64_t off = TIFFGetStrileOffsetWithErr(t, i, &err);
        const uint64_t cnt = TIFFGetStrileByteCountWithErr(t, i, &err);
        if (err || off == 0 || off + cnt > fileSize) {
            rep.ok = false;
            rep.problems.push_back("data block " + std::to_string(i) + " lies outside the file (truncated?)");
            break;
        }
    }
    TIFFClose(t);
    if (!rep.ok) return rep;
    auto src = openTiff(path, {});
    if (!src) {
        rep.ok = false;
        rep.problems.push_back(src.error().message + ": " + src.error().detail);
        return rep;
    }
    const auto& inf = (*src)->info();
    std::vector<uint8_t> row(inf.rowBytes());
    for (int64_t y : {int64_t{0}, inf.height - 1}) {
        auto st = (*src)->readRows(y, 1, 0, inf.width, row.data(), row.size());
        if (!st) {
            rep.ok = false;
            rep.problems.push_back("row " + std::to_string(y) + ": " + st.error().message + " " +
                                   st.error().detail);
        }
    }
    return rep;
}

} // namespace fk
