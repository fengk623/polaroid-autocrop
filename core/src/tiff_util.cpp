#include "tiff_util.hpp"

#include "fk/color.hpp"

#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace fk::detail {

namespace {
thread_local std::string g_lastError;

void errorHandler(const char* module, const char* fmt, va_list ap) {
    char buf[1024];
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    g_lastError = (module ? std::string(module) + ": " : std::string()) + buf;
}
void warningHandler(const char*, const char*, va_list) {}

void installHandlers() {
    static std::once_flag once;
    std::call_once(once, [] {
        TIFFSetErrorHandler(errorHandler);
        TIFFSetWarningHandler(warningHandler);
    });
}
} // namespace

TIFF* tiffOpenPath(const std::filesystem::path& p, const char* mode) {
    installHandlers();
#if defined(_WIN32)
    return TIFFOpenW(p.wstring().c_str(), mode);
#else
    return TIFFOpen(p.c_str(), mode);
#endif
}

void clearTiffError() {
    g_lastError.clear();
}
std::string lastTiffError() {
    return g_lastError;
}

Status readTiffInfo(TIFF* t, const std::filesystem::path& path, ImageInfo& info, RawTiffLayout& raw) {
    uint32_t w = 0, h = 0;
    uint16_t spp = 1, bps = 1, photo = 0, planar = PLANARCONFIG_CONTIG, comp = COMPRESSION_NONE,
             fmt = SAMPLEFORMAT_UINT;
    uint16_t pred = 1;
    TIFFGetField(t, TIFFTAG_IMAGEWIDTH, &w);
    TIFFGetField(t, TIFFTAG_IMAGELENGTH, &h);
    TIFFGetFieldDefaulted(t, TIFFTAG_SAMPLESPERPIXEL, &spp);
    TIFFGetFieldDefaulted(t, TIFFTAG_BITSPERSAMPLE, &bps);
    TIFFGetFieldDefaulted(t, TIFFTAG_PLANARCONFIG, &planar);
    TIFFGetFieldDefaulted(t, TIFFTAG_COMPRESSION, &comp);
    TIFFGetFieldDefaulted(t, TIFFTAG_SAMPLEFORMAT, &fmt);
    TIFFGetFieldDefaulted(t, TIFFTAG_PREDICTOR, &pred);
    if (!TIFFGetField(t, TIFFTAG_PHOTOMETRIC, &photo))
        return makeError(Errc::unsupported_format, "TIFF without PhotometricInterpretation is not supported");
    if (w == 0 || h == 0) return makeError(Errc::damaged_file, "TIFF has zero width or height");
    if (bps != 8 && bps != 16)
        return makeError(Errc::unsupported_format, "Only 8- and 16-bit TIFFs are supported (this file has " +
                                                       std::to_string(bps) + " bits)");
    if (fmt != SAMPLEFORMAT_UINT)
        return makeError(Errc::unsupported_format, "Floating-point or signed TIFF samples are not supported");

    info = ImageInfo{};
    raw = RawTiffLayout{};
    switch (photo) {
    case PHOTOMETRIC_MINISBLACK:
        info.photometric = Photometric::min_is_black;
        break;
    case PHOTOMETRIC_MINISWHITE:
        info.photometric = Photometric::min_is_white;
        raw.minIsWhite = true;
        break;
    case PHOTOMETRIC_RGB:
        info.photometric = Photometric::rgb;
        break;
    case PHOTOMETRIC_YCBCR:
        if (comp != COMPRESSION_JPEG)
            return makeError(Errc::unsupported_format, "YCbCr TIFF is only supported with JPEG compression");
        info.photometric = Photometric::rgb;
        raw.ycbcrJpeg = true;
        break;
    case PHOTOMETRIC_PALETTE:
        return makeError(Errc::unsupported_format, "Palette (indexed colour) TIFFs are not supported");
    case PHOTOMETRIC_SEPARATED:
        return makeError(Errc::unsupported_format, "CMYK TIFFs are not supported");
    case PHOTOMETRIC_CIELAB:
    case PHOTOMETRIC_ICCLAB:
    case PHOTOMETRIC_ITULAB:
        return makeError(Errc::unsupported_format, "Lab TIFFs are not supported");
    default:
        return makeError(Errc::unsupported_format,
                         "Unsupported TIFF photometric interpretation " + std::to_string(photo));
    }
    const bool gray = info.photometric != Photometric::rgb;
    if (gray && spp != 1 && spp != 2)
        return makeError(Errc::unsupported_format,
                         "Greyscale TIFF with " + std::to_string(spp) + " samples per pixel");
    if (!gray && spp != 3 && spp != 4)
        return makeError(Errc::unsupported_format,
                         "RGB TIFF with " + std::to_string(spp) + " samples per pixel");
    if (raw.ycbcrJpeg && planar != PLANARCONFIG_CONTIG)
        return makeError(Errc::unsupported_format, "Planar YCbCr JPEG TIFF is not supported");

    raw.spp = spp;
    info.width = w;
    info.height = h;
    info.channels = spp;
    info.bitsPerSample = bps;
    info.hasAlpha = (gray && spp == 2) || (!gray && spp == 4);
    info.planarSeparate = planar == PLANARCONFIG_SEPARATE;
    info.predictor = pred;
    switch (comp) {
    case COMPRESSION_NONE:
        info.compression = Compression::none;
        break;
    case COMPRESSION_LZW:
        info.compression = Compression::lzw;
        break;
    case COMPRESSION_ADOBE_DEFLATE:
    case COMPRESSION_DEFLATE:
        info.compression = Compression::deflate;
        break;
    case COMPRESSION_PACKBITS:
        info.compression = Compression::packbits;
        break;
    case COMPRESSION_JPEG:
        info.compression = Compression::jpeg;
        break;
    default:
        if (!TIFFIsCODECConfigured(comp))
            return makeError(Errc::unsupported_format,
                             "TIFF compression scheme " + std::to_string(comp) + " is not supported");
        info.compression = Compression::other;
    }
    info.tiled = TIFFIsTiled(t) != 0;
    if (info.tiled) {
        TIFFGetField(t, TIFFTAG_TILEWIDTH, &info.tileWidth);
        TIFFGetField(t, TIFFTAG_TILELENGTH, &info.tileHeight);
        if (info.tileWidth == 0 || info.tileHeight == 0)
            return makeError(Errc::damaged_file, "Invalid TIFF tile size");
    } else {
        uint32_t rps = 0;
        TIFFGetFieldDefaulted(t, TIFFTAG_ROWSPERSTRIP, &rps);
        info.rowsPerStrip = rps;
    }
    info.bigTiff = TIFFIsBigTIFF(t) != 0;

    float xr = 0, yr = 0;
    uint16_t unit = RESUNIT_INCH;
    TIFFGetFieldDefaulted(t, TIFFTAG_RESOLUTIONUNIT, &unit);
    if (TIFFGetField(t, TIFFTAG_XRESOLUTION, &xr) && TIFFGetField(t, TIFFTAG_YRESOLUTION, &yr) && xr > 0 &&
        yr > 0) {
        if (unit == RESUNIT_INCH) {
            info.dpiX = xr;
            info.dpiY = yr;
        } else if (unit == RESUNIT_CENTIMETER) {
            info.dpiX = xr * 2.54;
            info.dpiY = yr * 2.54;
        }
    }
    uint32_t iccLen = 0;
    void* icc = nullptr;
    if (TIFFGetField(t, TIFFTAG_ICCPROFILE, &iccLen, &icc) && icc && iccLen) {
        info.icc.assign(static_cast<const uint8_t*>(icc), static_cast<const uint8_t*>(icc) + iccLen);
        info.iccDescription = iccDescription(info.icc);
    }
    uint16_t orient = ORIENTATION_TOPLEFT;
    TIFFGetFieldDefaulted(t, TIFFTAG_ORIENTATION, &orient);
    info.orientation = orient;
    std::error_code ec;
    info.fileSize = std::filesystem::file_size(path, ec);
    info.pageCount = static_cast<int>(TIFFNumberOfDirectories(t));
    TIFFSetDirectory(t, 0);
    info.format = FileFormat::tiff;
    return {};
}

} // namespace fk::detail
