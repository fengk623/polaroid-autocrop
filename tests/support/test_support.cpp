#include "test_support.hpp"

#include "fk/hash.hpp"
#include "fk/util.hpp"

#include <tiffio.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace fk::test {

TempDir::TempDir() {
    p_ = std::filesystem::temp_directory_path() / ("fk-test-" + randomToken(10));
    std::filesystem::create_directories(p_);
}
TempDir::~TempDir() {
    if (std::getenv("FK_KEEP_TEST_FILES")) {
        std::fprintf(stderr, "kept test files: %s\n", p_.string().c_str());
        return;
    }
    std::error_code ec;
    std::filesystem::remove_all(p_, ec);
}
std::filesystem::path TempDir::operator/(const std::string& name) const {
    return p_ / pathFromUtf8(name);
}

ImageBuffer makePatternImage(int64_t w, int64_t h, int channels, int bits, uint32_t seed) {
    auto r = ImageBuffer::create(w, h, channels, bits);
    if (!r) throw std::runtime_error("alloc");
    ImageBuffer b = std::move(r.value());
    uint32_t s = seed * 2654435761u + 12345u;
    for (int64_t y = 0; y < h; ++y) {
        uint8_t* row = b.row(y);
        for (int64_t x = 0; x < w; ++x)
            for (int c = 0; c < channels; ++c) {
                s ^= s << 13;
                s ^= s >> 17;
                s ^= s << 5;
                const uint32_t v = static_cast<uint32_t>((x * 37 + y * 101 + c * 7919) ^ (s & 0xFF));
                if (bits == 8) {
                    row[x * channels + c] = static_cast<uint8_t>(v);
                } else {
                    const uint16_t v16 = static_cast<uint16_t>((v * 257u) ^ (s >> 16));
                    std::memcpy(row + (x * channels + c) * 2, &v16, 2);
                }
            }
    }
    return b;
}

void writeTestTiff(const std::filesystem::path& p, const ImageBuffer& img, const TestTiffOptions& o) {
#if defined(_WIN32)
    TIFF* t = TIFFOpenW(p.wstring().c_str(), o.bigTiff ? "w8" : "w");
#else
    TIFF* t = TIFFOpen(p.c_str(), o.bigTiff ? "w8" : "w");
#endif
    if (!t) throw std::runtime_error("TIFFOpen failed");
    const int ch = img.channels(), bps = img.bits() / 8;
    const bool gray = ch <= 2;
    TIFFSetField(t, TIFFTAG_IMAGEWIDTH, static_cast<uint32_t>(img.width()));
    TIFFSetField(t, TIFFTAG_IMAGELENGTH, static_cast<uint32_t>(img.height()));
    TIFFSetField(t, TIFFTAG_SAMPLESPERPIXEL, static_cast<uint16_t>(ch));
    TIFFSetField(t, TIFFTAG_BITSPERSAMPLE, static_cast<uint16_t>(img.bits()));
    TIFFSetField(t, TIFFTAG_PHOTOMETRIC,
                 gray ? (o.minIsWhite ? PHOTOMETRIC_MINISWHITE : PHOTOMETRIC_MINISBLACK) : PHOTOMETRIC_RGB);
    TIFFSetField(t, TIFFTAG_PLANARCONFIG, o.planarSeparate ? PLANARCONFIG_SEPARATE : PLANARCONFIG_CONTIG);
    TIFFSetField(t, TIFFTAG_COMPRESSION, static_cast<uint16_t>(o.compression));
    if (o.predictor != 1) TIFFSetField(t, TIFFTAG_PREDICTOR, static_cast<uint16_t>(o.predictor));
    if (ch == 2 || ch == 4) {
        const uint16_t es[1] = {EXTRASAMPLE_UNASSALPHA};
        TIFFSetField(t, TIFFTAG_EXTRASAMPLES, 1, es);
    }
    if (!o.icc.empty())
        TIFFSetField(t, TIFFTAG_ICCPROFILE, static_cast<uint32_t>(o.icc.size()), o.icc.data());
    if (o.dpi > 0) {
        TIFFSetField(t, TIFFTAG_RESOLUTIONUNIT, RESUNIT_INCH);
        TIFFSetField(t, TIFFTAG_XRESOLUTION, static_cast<float>(o.dpi));
        TIFFSetField(t, TIFFTAG_YRESOLUTION, static_cast<float>(o.dpi));
    }
    // stored samples (minIsWhite inverted)
    auto stored = [&](int64_t y, std::vector<uint8_t>& out) {
        out.assign(img.row(y), img.row(y) + img.rowBytes());
        if (o.minIsWhite) {
            for (int64_t x = 0; x < img.width(); ++x) {
                if (bps == 1)
                    out[x * ch] = static_cast<uint8_t>(255 - out[x * ch]);
                else {
                    uint16_t v;
                    std::memcpy(&v, &out[x * ch * 2], 2);
                    v = static_cast<uint16_t>(65535 - v);
                    std::memcpy(&out[x * ch * 2], &v, 2);
                }
            }
        }
    };
    const int planes = o.planarSeparate ? ch : 1;
    const int sppPlane = o.planarSeparate ? 1 : ch;
    std::vector<uint8_t> row;
    if (o.layout == TiffLayout::tiles) {
        TIFFSetField(t, TIFFTAG_TILEWIDTH, o.tile);
        TIFFSetField(t, TIFFTAG_TILELENGTH, o.tile);
        const tmsize_t ts = TIFFTileSize(t);
        std::vector<uint8_t> tile(static_cast<size_t>(ts));
        for (int pl = 0; pl < planes; ++pl)
            for (uint32_t ty = 0; ty < img.height(); ty += o.tile)
                for (uint32_t tx = 0; tx < img.width(); tx += o.tile) {
                    std::fill(tile.begin(), tile.end(), 0);
                    for (uint32_t y = 0; y < o.tile && ty + y < img.height(); ++y) {
                        stored(ty + y, row);
                        for (uint32_t x = 0; x < o.tile && tx + x < img.width(); ++x)
                            for (int c = 0; c < sppPlane; ++c) {
                                const int sc = o.planarSeparate ? pl : c;
                                std::memcpy(&tile[((y * o.tile + x) * sppPlane + c) * bps],
                                            &row[((tx + x) * ch + sc) * bps], bps);
                            }
                    }
                    TIFFWriteEncodedTile(t, TIFFComputeTile(t, tx, ty, 0, static_cast<uint16_t>(pl)),
                                         tile.data(), ts);
                }
    } else {
        const uint32_t rps =
            o.layout == TiffLayout::single_strip ? static_cast<uint32_t>(img.height()) : o.rowsPerStrip;
        TIFFSetField(t, TIFFTAG_ROWSPERSTRIP, rps);
        std::vector<uint8_t> line(static_cast<size_t>(img.width()) * sppPlane * bps);
        for (int pl = 0; pl < planes; ++pl)
            for (int64_t y = 0; y < img.height(); ++y) {
                stored(y, row);
                if (!o.planarSeparate)
                    line = row;
                else
                    for (int64_t x = 0; x < img.width(); ++x)
                        std::memcpy(&line[x * bps], &row[(x * ch + pl) * bps], bps);
                if (TIFFWriteScanline(t, line.data(), static_cast<uint32_t>(y), static_cast<uint16_t>(pl)) <
                    0)
                    throw std::runtime_error("TIFFWriteScanline failed");
            }
    }
    TIFFClose(t);
}

std::string sha256OfFile(const std::filesystem::path& p) {
    auto r = sha256File(p);
    return r ? r.value() : std::string("error");
}

bool sameSamples(const ImageBuffer& a, const ImageBuffer& b) {
    if (a.width() != b.width() || a.height() != b.height() || a.channels() != b.channels() ||
        a.bits() != b.bits())
        return false;
    return std::memcmp(a.data(), b.data(), a.byteSize()) == 0;
}

} // namespace fk::test
