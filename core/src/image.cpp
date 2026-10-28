#include "fk/image.hpp"

namespace fk {

const char* compressionName(Compression c) {
    switch (c) {
    case Compression::none:
        return "none";
    case Compression::lzw:
        return "lzw";
    case Compression::deflate:
        return "deflate";
    case Compression::packbits:
        return "packbits";
    case Compression::jpeg:
        return "jpeg";
    case Compression::other:
        return "other";
    }
    return "other";
}

const char* fileFormatName(FileFormat f) {
    switch (f) {
    case FileFormat::tiff:
        return "tiff";
    case FileFormat::png:
        return "png";
    case FileFormat::jpeg:
        return "jpeg";
    case FileFormat::dng:
        return "dng";
    }
    return "tiff";
}

const char* photometricName(Photometric p) {
    switch (p) {
    case Photometric::min_is_black:
        return "minisblack";
    case Photometric::min_is_white:
        return "miniswhite";
    case Photometric::rgb:
        return "rgb";
    }
    return "rgb";
}

Result<ImageBuffer> ImageBuffer::create(int64_t w, int64_t h, int channels, int bits, MemCategory cat,
                                        bool zero) {
    if (w <= 0 || h <= 0 || channels < 1 || channels > 4 || (bits != 8 && bits != 16))
        return makeError(Errc::invalid_argument, "Invalid image buffer parameters");
    ImageBuffer b;
    b.w_ = w;
    b.h_ = h;
    b.ch_ = channels;
    b.bits_ = bits;
    const size_t bytes = b.rowBytes() * static_cast<size_t>(h);
    auto buf = AccountedBuffer::allocate(cat, bytes, zero);
    if (!buf) return buf.error();
    b.buf_ = std::move(buf.value());
    return b;
}

Result<ImageBuffer> ImageBuffer::clone() const {
    auto r = create(w_, h_, ch_, bits_, MemCategory::image, false);
    if (!r) return r.error();
    std::memcpy(r->data(), data(), byteSize());
    return r;
}

void convertDepth(const uint8_t* src, int srcBits, uint8_t* dst, int dstBits, size_t samples) {
    if (srcBits == dstBits) {
        std::memcpy(dst, src, samples * (srcBits / 8));
        return;
    }
    if (srcBits == 8 && dstBits == 16) {
        for (size_t i = 0; i < samples; ++i) {
            const uint16_t v = static_cast<uint16_t>(src[i] * 257);
            std::memcpy(dst + 2 * i, &v, 2);
        }
        return;
    }
    // 16 -> 8, round half up: (v * 255 + 32767) / 65535
    for (size_t i = 0; i < samples; ++i) {
        uint16_t v;
        std::memcpy(&v, src + 2 * i, 2);
        dst[i] = static_cast<uint8_t>((static_cast<uint32_t>(v) * 255u + 32767u) / 65535u);
    }
}

} // namespace fk
