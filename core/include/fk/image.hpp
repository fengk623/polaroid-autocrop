// FrameKeeper core — image types (API-SPEC-core §3).
#pragma once

#include "fk/common.hpp"
#include "fk/memory.hpp"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace fk {

enum class Photometric : int { min_is_black = 0, min_is_white = 1, rgb = 2 };
enum class Compression : int { none = 0, lzw = 1, deflate = 2, packbits = 3, jpeg = 4, other = 99 };
enum class FileFormat : int { tiff = 0, png = 1, jpeg = 2, dng = 3 };

const char* compressionName(Compression c);
const char* fileFormatName(FileFormat f);
const char* photometricName(Photometric p);

struct ImageInfo {
    int64_t width = 0;
    int64_t height = 0;
    int channels = 0;      ///< samples per pixel delivered by readers (1–4)
    int bitsPerSample = 0; ///< 8 or 16
    bool hasAlpha = false;
    Photometric photometric = Photometric::rgb;
    Compression compression = Compression::none;
    int predictor = 1;
    bool tiled = false;
    uint32_t tileWidth = 0, tileHeight = 0, rowsPerStrip = 0;
    bool bigTiff = false;
    bool planarSeparate = false;
    double dpiX = 0, dpiY = 0; ///< 0 = unknown
    std::vector<uint8_t> icc;
    std::string iccDescription;
    int orientation = 1;
    uint64_t fileSize = 0;
    int pageCount = 1;
    FileFormat format = FileFormat::tiff;

    int bytesPerSample() const { return bitsPerSample / 8; }
    size_t bytesPerPixel() const { return static_cast<size_t>(channels) * bytesPerSample(); }
    size_t rowBytes() const { return bytesPerPixel() * static_cast<size_t>(width); }
    uint64_t uncompressedBytes() const {
        return static_cast<uint64_t>(rowBytes()) * static_cast<uint64_t>(height);
    }
};

/// Owning interleaved image, 8 or 16 bits per sample, 1–4 channels. Accounted memory.
class ImageBuffer {
  public:
    ImageBuffer() = default;
    static Result<ImageBuffer> create(int64_t w, int64_t h, int channels, int bits,
                                      MemCategory cat = MemCategory::image, bool zero = false);

    int64_t width() const { return w_; }
    int64_t height() const { return h_; }
    int channels() const { return ch_; }
    int bits() const { return bits_; }
    int bytesPerSample() const { return bits_ / 8; }
    size_t bytesPerPixel() const { return static_cast<size_t>(ch_) * bytesPerSample(); }
    size_t rowBytes() const { return bytesPerPixel() * static_cast<size_t>(w_); }
    bool empty() const { return w_ == 0 || h_ == 0; }
    SizeI size() const { return {w_, h_}; }

    uint8_t* row(int64_t y) { return buf_.data() + static_cast<size_t>(y) * rowBytes(); }
    const uint8_t* row(int64_t y) const { return buf_.data() + static_cast<size_t>(y) * rowBytes(); }
    uint8_t* data() { return buf_.data(); }
    const uint8_t* data() const { return buf_.data(); }
    size_t byteSize() const { return buf_.size(); }

    /// Sample as 16-bit-scale value (8-bit samples are scaled by 257).
    uint16_t sample16(int64_t x, int64_t y, int c) const {
        if (bits_ == 8) return static_cast<uint16_t>(row(y)[x * ch_ + c] * 257);
        uint16_t v;
        std::memcpy(&v, row(y) + (x * ch_ + c) * 2, 2);
        return v;
    }
    /// Normalised sample in [0,1].
    float samplef(int64_t x, int64_t y, int c) const {
        if (bits_ == 8) return row(y)[x * ch_ + c] * (1.0f / 255.0f);
        uint16_t v;
        std::memcpy(&v, row(y) + (x * ch_ + c) * 2, 2);
        return v * (1.0f / 65535.0f);
    }

    /// Deep copy (accounted).
    Result<ImageBuffer> clone() const;

  private:
    int64_t w_ = 0, h_ = 0;
    int ch_ = 0, bits_ = 0;
    AccountedBuffer buf_;
};

/// 8-bit → 16-bit (×257) and 16-bit → 8-bit (round half up) sample conversion over rows.
void convertDepth(const uint8_t* src, int srcBits, uint8_t* dst, int dstBits, size_t samples);

} // namespace fk
