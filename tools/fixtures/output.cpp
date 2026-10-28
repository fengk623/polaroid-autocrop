// fkfixture — streaming TIFF writer, ICC profile and preview PNG.
#include "output.hpp"

#include <lcms2.h>
#include <png.h>
#include <tiffio.h>

#include <algorithm>
#include <cmath>
#include <csetjmp>
#include <cstring>

namespace fkfx {

std::FILE* openFile(const std::filesystem::path& path, const char* mode) {
#ifdef _WIN32
    std::wstring wmode(mode, mode + std::strlen(mode));
    return _wfopen(path.c_str(), wmode.c_str());
#else
    return std::fopen(path.c_str(), mode);
#endif
}

std::string pathString(const std::filesystem::path& path) {
    const std::u8string s = path.u8string();
    return std::string(s.begin(), s.end());
}

bool writeTextFile(const std::filesystem::path& path, const std::string& text, std::string& err) {
    std::FILE* f = openFile(path, "wb");
    if (!f) {
        err = "cannot open for writing: " + pathString(path);
        return false;
    }
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    const bool closed = std::fclose(f) == 0;
    if (!ok || !closed) err = "write failed: " + pathString(path);
    return ok && closed;
}

std::vector<std::uint8_t> makeIccProfile(int channels) {
    cmsHPROFILE h = nullptr;
    if (channels == 3) {
        h = cmsCreate_sRGBProfile();
    } else {
        const cmsFloat64Number params[5] = {2.4, 1.0 / 1.055, 0.055 / 1.055, 1.0 / 12.92, 0.04045};
        cmsToneCurve* curve = cmsBuildParametricToneCurve(nullptr, 4, params);
        h = cmsCreateGrayProfile(cmsD50_xyY(), curve);
        cmsFreeToneCurve(curve);
    }
    std::vector<std::uint8_t> buf;
    if (!h) return buf;
    cmsUInt32Number n = 0;
    if (cmsSaveProfileToMem(h, nullptr, &n) && n > 128) {
        buf.resize(n);
        if (!cmsSaveProfileToMem(h, buf.data(), &n)) buf.clear();
    }
    cmsCloseProfile(h);
    if (buf.size() > 128) {
        // ICC header bytes 24..35: creation dateTimeNumber (lcms2 stamps "now"). Fix it.
        const std::uint16_t date[6] = {2026, 1, 1, 0, 0, 0};
        for (int i = 0; i < 6; ++i) {
            buf[static_cast<std::size_t>(24 + 2 * i)] = static_cast<std::uint8_t>(date[i] >> 8);
            buf[static_cast<std::size_t>(25 + 2 * i)] = static_cast<std::uint8_t>(date[i] & 0xFF);
        }
    }
    return buf;
}

// ---------------------------------------------------------------------------------------------
// TIFF
// ---------------------------------------------------------------------------------------------
TiffWriter::~TiffWriter() {
    if (tif_) TIFFClose(tif_);
}

bool TiffWriter::open(const std::filesystem::path& path, const Scene& sc, std::string& err) {
    sc_ = &sc;
    const Options& o = sc.opt;
    const char* mode = o.bigtiff ? "w8" : "w";
#ifdef _WIN32
    tif_ = TIFFOpenW(path.c_str(), mode);
#else
    tif_ = TIFFOpen(path.c_str(), mode);
#endif
    if (!tif_) {
        err = "cannot create TIFF: " + pathString(path);
        return false;
    }
    const auto W = static_cast<std::uint32_t>(sc.width), H = static_cast<std::uint32_t>(sc.height);
    rowBytes_ = static_cast<std::size_t>(sc.width) * static_cast<std::size_t>(o.channels) *
                static_cast<std::size_t>(o.bits / 8);
    std::uint16_t photometric = PHOTOMETRIC_RGB;
    if (o.photometric() == Photometric::MinIsBlack) photometric = PHOTOMETRIC_MINISBLACK;
    if (o.photometric() == Photometric::MinIsWhite) photometric = PHOTOMETRIC_MINISWHITE;
    std::uint16_t compression = COMPRESSION_NONE;
    switch (o.compression) {
    case Compression::None:
        compression = COMPRESSION_NONE;
        break;
    case Compression::Lzw:
        compression = COMPRESSION_LZW;
        break;
    case Compression::Deflate:
        compression = COMPRESSION_ADOBE_DEFLATE;
        break;
    case Compression::PackBits:
        compression = COMPRESSION_PACKBITS;
        break;
    }
    bool ok = TIFFSetField(tif_, TIFFTAG_IMAGEWIDTH, W) && TIFFSetField(tif_, TIFFTAG_IMAGELENGTH, H) &&
              TIFFSetField(tif_, TIFFTAG_BITSPERSAMPLE, static_cast<std::uint16_t>(o.bits)) &&
              TIFFSetField(tif_, TIFFTAG_SAMPLESPERPIXEL, static_cast<std::uint16_t>(o.channels)) &&
              TIFFSetField(tif_, TIFFTAG_SAMPLEFORMAT, static_cast<std::uint16_t>(SAMPLEFORMAT_UINT)) &&
              TIFFSetField(tif_, TIFFTAG_PHOTOMETRIC, photometric) &&
              TIFFSetField(tif_, TIFFTAG_PLANARCONFIG, static_cast<std::uint16_t>(PLANARCONFIG_CONTIG)) &&
              TIFFSetField(tif_, TIFFTAG_ORIENTATION, static_cast<std::uint16_t>(ORIENTATION_TOPLEFT)) &&
              TIFFSetField(tif_, TIFFTAG_COMPRESSION, compression) &&
              TIFFSetField(tif_, TIFFTAG_XRESOLUTION, static_cast<double>(o.dpi)) &&
              TIFFSetField(tif_, TIFFTAG_YRESOLUTION, static_cast<double>(o.dpi)) &&
              TIFFSetField(tif_, TIFFTAG_RESOLUTIONUNIT, static_cast<std::uint16_t>(RESUNIT_INCH)) &&
              TIFFSetField(tif_, TIFFTAG_SOFTWARE, "fkfixture");
    if (ok && o.predictor == 2)
        ok = TIFFSetField(tif_, TIFFTAG_PREDICTOR, static_cast<std::uint16_t>(PREDICTOR_HORIZONTAL));
    if (ok && o.iccSrgb) {
        const auto icc = makeIccProfile(o.channels);
        ok = !icc.empty() &&
             TIFFSetField(tif_, TIFFTAG_ICCPROFILE, static_cast<std::uint32_t>(icc.size()), icc.data());
    }
    if (ok && o.layout == Layout::Tiles) {
        ok = TIFFSetField(tif_, TIFFTAG_TILEWIDTH, static_cast<std::uint32_t>(o.tile)) &&
             TIFFSetField(tif_, TIFFTAG_TILELENGTH, static_cast<std::uint32_t>(o.tile));
        scratch_.assign(static_cast<std::size_t>(o.tile) * static_cast<std::size_t>(o.tile) *
                            static_cast<std::size_t>(o.channels) * static_cast<std::size_t>(o.bits / 8),
                        0);
    } else if (ok) {
        const std::uint32_t rps =
            o.layout == Layout::SingleStrip ? H : std::min(static_cast<std::uint32_t>(o.rowsPerStrip), H);
        ok = TIFFSetField(tif_, TIFFTAG_ROWSPERSTRIP, rps);
        // Bound libtiff's encoded-data buffer (it defaults to a whole strip, i.e. the whole image for
        // single-strip output); full buffers are appended to the strip as they fill.
        ok = ok && TIFFWriteBufferSetup(tif_, nullptr, static_cast<tmsize_t>(1) << 20);
        scratch_.assign(rowBytes_, 0);
    }
    if (!ok) err = "libtiff rejected the requested TIFF options";
    return ok;
}

bool TiffWriter::writeBand(const std::uint8_t* band, int y0, int rows, std::string& err) {
    const Options& o = sc_->opt;
    if (o.layout != Layout::Tiles) {
        for (int r = 0; r < rows; ++r) {
            // Copy: the predictor modifies the buffer passed to TIFFWriteScanline in place.
            std::memcpy(scratch_.data(), band + static_cast<std::size_t>(r) * rowBytes_, rowBytes_);
            if (TIFFWriteScanline(tif_, scratch_.data(), static_cast<std::uint32_t>(y0 + r), 0) < 0) {
                err = "TIFFWriteScanline failed at row " + std::to_string(y0 + r);
                return false;
            }
        }
        return true;
    }
    const std::size_t px = static_cast<std::size_t>(o.channels) * static_cast<std::size_t>(o.bits / 8);
    const std::size_t T = static_cast<std::size_t>(o.tile);
    for (int tx = 0; tx < sc_->width; tx += o.tile) {
        std::fill(scratch_.begin(), scratch_.end(), std::uint8_t{0});
        const std::size_t cols = std::min(T, static_cast<std::size_t>(sc_->width - tx));
        for (int r = 0; r < rows; ++r)
            std::memcpy(scratch_.data() + static_cast<std::size_t>(r) * T * px,
                        band + static_cast<std::size_t>(r) * rowBytes_ + static_cast<std::size_t>(tx) * px,
                        cols * px);
        if (TIFFWriteTile(tif_, scratch_.data(), static_cast<std::uint32_t>(tx),
                          static_cast<std::uint32_t>(y0), 0, 0) < 0) {
            err = "TIFFWriteTile failed at " + std::to_string(tx) + "," + std::to_string(y0);
            return false;
        }
    }
    return true;
}

bool TiffWriter::close(std::string& err) {
    if (!tif_) return true;
    const bool ok = TIFFFlush(tif_) != 0;
    TIFFClose(tif_);
    tif_ = nullptr;
    if (!ok) err = "TIFFFlush failed";
    return ok;
}

// ---------------------------------------------------------------------------------------------
// Preview
// ---------------------------------------------------------------------------------------------
void PreviewBuilder::init(const Scene& sc) {
    sc_ = &sc;
    const int longSide = std::max(sc.width, sc.height);
    if (longSide <= 1200) {
        pw_ = sc.width;
        ph_ = sc.height;
    } else {
        pw_ = std::max(1, static_cast<int>(std::floor(sc.width * 1200.0 / longSide + 0.5)));
        ph_ = std::max(1, static_cast<int>(std::floor(sc.height * 1200.0 / longSide + 0.5)));
    }
    fx_ = static_cast<double>(pw_) / sc.width;
    fy_ = static_cast<double>(ph_) / sc.height;
    colMap_.resize(static_cast<std::size_t>(sc.width));
    for (int x = 0; x < sc.width; ++x)
        colMap_[static_cast<std::size_t>(x)] = std::min(pw_ - 1, static_cast<int>((x + 0.5) * fx_));
    acc_.assign(static_cast<std::size_t>(pw_) * 3, 0.0);
    cnt_.assign(static_cast<std::size_t>(pw_), 0);
    img_.assign(static_cast<std::size_t>(pw_) * static_cast<std::size_t>(ph_) * 3, 0);
    curRow_ = -1;
}

void PreviewBuilder::addRow(const std::uint8_t* row, int y) {
    const Options& o = sc_->opt;
    const int j = std::min(ph_ - 1, static_cast<int>((y + 0.5) * fy_));
    if (j != curRow_) {
        flushRow();
        curRow_ = j;
    }
    const double maxv = o.bits == 16 ? 65535.0 : 255.0;
    const bool inv = o.photometric() == Photometric::MinIsWhite;
    for (int x = 0; x < sc_->width; ++x) {
        const std::size_t i = static_cast<std::size_t>(colMap_[static_cast<std::size_t>(x)]);
        double v[3] = {0.0, 0.0, 0.0};
        for (int c = 0; c < o.channels; ++c) {
            const std::size_t k = static_cast<std::size_t>(x) * static_cast<std::size_t>(o.channels) +
                                  static_cast<std::size_t>(c);
            double s;
            if (o.bits == 16) {
                std::uint16_t t;
                std::memcpy(&t, row + 2 * k, 2);
                s = t;
            } else {
                s = row[k];
            }
            s /= maxv;
            v[c] = inv ? 1.0 - s : s;
        }
        if (o.channels == 1) v[1] = v[2] = v[0];
        acc_[3 * i] += v[0];
        acc_[3 * i + 1] += v[1];
        acc_[3 * i + 2] += v[2];
        ++cnt_[i];
    }
}

void PreviewBuilder::flushRow() {
    if (curRow_ < 0) return;
    for (int i = 0; i < pw_; ++i) {
        const std::size_t ii = static_cast<std::size_t>(i);
        if (cnt_[ii] == 0) continue;
        for (int c = 0; c < 3; ++c) {
            const double v = clamp01(acc_[3 * ii + static_cast<std::size_t>(c)] / cnt_[ii]);
            img_[(static_cast<std::size_t>(curRow_) * static_cast<std::size_t>(pw_) + ii) * 3 +
                 static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(std::floor(v * 255.0 + 0.5));
        }
    }
    std::fill(acc_.begin(), acc_.end(), 0.0);
    std::fill(cnt_.begin(), cnt_.end(), 0u);
}

void PreviewBuilder::plot(int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    if (x < 0 || y < 0 || x >= pw_ || y >= ph_) return;
    std::uint8_t* p =
        &img_[(static_cast<std::size_t>(y) * static_cast<std::size_t>(pw_) + static_cast<std::size_t>(x)) *
              3];
    p[0] = r;
    p[1] = g;
    p[2] = b;
}

void PreviewBuilder::line(Vec2 a, Vec2 b, std::uint8_t r, std::uint8_t g, std::uint8_t bl) {
    const Vec2 pa = map(a), pb = map(b);
    const double len = std::max(std::fabs(pb.x - pa.x), std::fabs(pb.y - pa.y));
    const int n = std::max(1, static_cast<int>(len * 4.0));
    for (int k = 0; k <= n; ++k) {
        const double t = static_cast<double>(k) / n;
        plot(static_cast<int>(std::floor(lerp(pa.x, pb.x, t))),
             static_cast<int>(std::floor(lerp(pa.y, pb.y, t))), r, g, bl);
    }
}

namespace {
// Only trivially destructible locals here (setjmp/longjmp error handling of libpng).
bool writePngRaw(std::FILE* f, int w, int h, const std::uint8_t* rgb) {
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) return false;
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_write_struct(&png, nullptr);
        return false;
    }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        return false;
    }
    png_init_io(png, f);
    png_set_IHDR(png, info, static_cast<png_uint_32>(w), static_cast<png_uint_32>(h), 8, PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    for (int y = 0; y < h; ++y)
        png_write_row(png, rgb + static_cast<std::size_t>(y) * static_cast<std::size_t>(w) * 3);
    png_write_end(png, nullptr);
    png_destroy_write_struct(&png, &info);
    return true;
}
} // namespace

bool PreviewBuilder::write(const std::filesystem::path& path, std::string& err) {
    flushRow();
    curRow_ = -1;
    for (const Print& p : sc_->prints) {
        const auto q = outerQuad(p);
        for (std::size_t i = 0; i < 4; ++i)
            line(q[i], q[(i + 1) % 4], 255, 0, 0);
        const Vec2 c0 = map(q[0]); // mark outerQuad[0]
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx)
                plot(static_cast<int>(std::floor(c0.x)) + dx, static_cast<int>(std::floor(c0.y)) + dy, 255, 0,
                     0);
        if (p.ellipse) {
            Vec2 prev = p.toScan(p.ccx + p.cr, p.ccy);
            for (int k = 1; k <= 720; ++k) {
                double sn, cs;
                sinCos(kTwoPi * k / 720.0, sn, cs);
                const Vec2 cur = p.toScan(p.ccx + p.cr * cs, p.ccy + p.cr * sn);
                line(prev, cur, 0, 255, 0);
                prev = cur;
            }
        } else {
            const auto iq = innerQuad(p);
            for (std::size_t i = 0; i < 4; ++i)
                line(iq[i], iq[(i + 1) % 4], 0, 255, 0);
        }
        // Yellow tick from the card centre toward the card's top edge (shows orientation).
        line(p.toScan(p.W * 0.5, p.H * 0.5), p.toScan(p.W * 0.5, p.H * 0.5 - 12.0), 255, 220, 0);
    }
    std::FILE* f = openFile(path, "wb");
    if (!f) {
        err = "cannot open preview for writing: " + pathString(path);
        return false;
    }
    const bool ok = writePngRaw(f, pw_, ph_, img_.data());
    const bool closed = std::fclose(f) == 0;
    if (!ok || !closed) err = "PNG write failed";
    return ok && closed;
}

} // namespace fkfx
