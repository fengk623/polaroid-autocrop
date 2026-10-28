// PNG/JPEG readers and writers, whole-file decoding, sink factory (RFC-0003).
#include "fk/color.hpp"
#include "fk/image_io.hpp"
#include "fk/metadata.hpp"
#include "fk/util.hpp"
#include "fk/version.hpp"
#include "sink_base.hpp"

#include <jpeglib.h>
#include <png.h>

#include <algorithm>
#include <bit>
#include <csetjmp>
#include <cstring>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace fk {

std::unique_ptr<ImageSink> makeTiffSink(const std::filesystem::path& p, const WriteOptions& o, bool dng);

std::FILE* openFile(const std::filesystem::path& p, const char* mode) {
#if defined(_WIN32)
    std::wstring wm(mode, mode + std::strlen(mode));
    return _wfopen(p.wstring().c_str(), wm.c_str());
#else
    return std::fopen(p.c_str(), mode);
#endif
}

uint64_t freeDiskBytes(const std::filesystem::path& dir) {
    std::error_code ec;
    const auto si = std::filesystem::space(dir, ec);
    if (ec) return 0;
    return si.available;
}

uint64_t estimateOutputBytes(SizeI size, int channels, int bits, const WriteOptions& opt) {
    const uint64_t raw = static_cast<uint64_t>(size.w) * size.h * channels * (bits / 8);
    uint64_t est = raw;
    if (opt.format == FileFormat::jpeg) est = raw / 2; // q100 4:4:4 upper estimate
    // LZW/Deflate can expand noisy data slightly; keep the uncompressed bound plus 2 %.
    if (opt.format != FileFormat::jpeg && opt.compression != Compression::none) est = raw + raw / 50;
    return est + opt.icc.size() + opt.xmp.size() + (64u << 10);
}

// ================================ PNG ======================================================
namespace {

struct PngErr {
    jmp_buf jb;
    char msg[256] = {0};
};
void pngError(png_structp p, png_const_charp m) {
    auto* e = static_cast<PngErr*>(png_get_error_ptr(p));
    std::snprintf(e->msg, sizeof(e->msg), "%s", m);
    longjmp(e->jb, 1);
}
void pngWarn(png_structp, png_const_charp) {}

class PngSink final : public detail::SinkBase {
  public:
    using SinkBase::SinkBase;
    ~PngSink() override {
        if (!done_) abort();
    }

    Status begin(SizeI size, int channels, int bits) override {
        if (bits != 8 && bits != 16)
            return makeError(Errc::invalid_argument, "PNG output supports 8 or 16 bits");
        size_ = size;
        channels_ = channels;
        bits_ = bits;
        f_ = openFile(temp_, "wb");
        if (!f_) return makeError(Errc::io_write, "Cannot create output file", pathToUtf8(temp_));
        png_ = png_create_write_struct(PNG_LIBPNG_VER_STRING, &err_, pngError, pngWarn);
        info_ = png_ ? png_create_info_struct(png_) : nullptr;
        if (!png_ || !info_) return makeError(Errc::internal, "libpng initialisation failed");
        if (setjmp(err_.jb)) return makeError(Errc::io_write, "Writing PNG failed", err_.msg);
        png_set_benign_errors(png_, 1);
        png_init_io(png_, f_);
        png_set_compression_level(png_, opt_.pngCompressionLevel);
        const int ct = channels == 1   ? PNG_COLOR_TYPE_GRAY
                       : channels == 2 ? PNG_COLOR_TYPE_GRAY_ALPHA
                       : channels == 3 ? PNG_COLOR_TYPE_RGB
                                       : PNG_COLOR_TYPE_RGB_ALPHA;
        png_set_IHDR(png_, info_, static_cast<png_uint_32>(size.w), static_cast<png_uint_32>(size.h), bits,
                     ct, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
        if (!opt_.icc.empty())
            png_set_iCCP(png_, info_, "ICC Profile", 0, opt_.icc.data(),
                         static_cast<png_uint_32>(opt_.icc.size()));
        if (opt_.dpiX > 0 && opt_.dpiY > 0)
            png_set_pHYs(png_, info_, static_cast<png_uint_32>(opt_.dpiX / 0.0254 + 0.5),
                         static_cast<png_uint_32>(opt_.dpiY / 0.0254 + 0.5), PNG_RESOLUTION_METER);
        std::vector<png_text> texts;
        sw_ = productNameAndVersion();
        png_text sw{};
        sw.compression = PNG_TEXT_COMPRESSION_NONE;
        sw.key = const_cast<char*>("Software");
        sw.text = const_cast<char*>(sw_.c_str());
        texts.push_back(sw);
        if (!opt_.xmp.empty()) {
            png_text x{};
            x.compression = PNG_ITXT_COMPRESSION_NONE;
            x.key = const_cast<char*>("XML:com.adobe.xmp");
            x.text = const_cast<char*>(opt_.xmp.c_str());
            x.itxt_length = opt_.xmp.size();
            x.lang = const_cast<char*>("");
            x.lang_key = const_cast<char*>("");
            texts.push_back(x);
        }
        png_set_text(png_, info_, texts.data(), static_cast<int>(texts.size()));
        png_write_info(png_, info_);
        if (bits == 16 && std::endian::native == std::endian::little) png_set_swap(png_);
        begun_ = true;
        return {};
    }

    Status writeRows(const uint8_t* rows, int64_t n, size_t stride) override {
        FK_TRY(checkRows(n));
        if (setjmp(err_.jb)) return makeError(Errc::io_write, "Writing PNG failed", err_.msg);
        for (int64_t i = 0; i < n; ++i)
            png_write_row(png_, const_cast<png_bytep>(rows + static_cast<size_t>(i) * stride));
        rowsWritten_ += n;
        return {};
    }

    Status finish() override {
        if (rowsWritten_ != size_.h) return makeError(Errc::internal, "finish before all rows were written");
        if (setjmp(err_.jb)) return makeError(Errc::io_write, "Writing PNG failed", err_.msg);
        png_write_end(png_, info_);
        png_destroy_write_struct(&png_, &info_);
        const bool ok = std::fflush(f_) == 0;
        std::fclose(f_);
        f_ = nullptr;
        if (!ok) {
            removeTemp();
            return makeError(Errc::io_write, "Writing PNG failed (disk full?)");
        }
        return commit();
    }

    void abort() override {
        if (png_) png_destroy_write_struct(&png_, &info_);
        if (f_) std::fclose(f_);
        f_ = nullptr;
        removeTemp();
        done_ = true;
    }

  private:
    std::FILE* f_ = nullptr;
    png_structp png_ = nullptr;
    png_infop info_ = nullptr;
    PngErr err_;
    std::string sw_;
};

// ================================ JPEG =====================================================
struct JpegErr {
    jpeg_error_mgr mgr;
    jmp_buf jb;
    char msg[JMSG_LENGTH_MAX] = {0};
};
void jpegErrorExit(j_common_ptr c) {
    auto* e = reinterpret_cast<JpegErr*>(c->err);
    (*c->err->format_message)(c, e->msg);
    longjmp(e->jb, 1);
}
void jpegOutputMessage(j_common_ptr) {}

class JpegSink final : public detail::SinkBase {
  public:
    using SinkBase::SinkBase;
    ~JpegSink() override {
        if (!done_) abort();
    }

    Status begin(SizeI size, int channels, int bits) override {
        if (bits != 8) return makeError(Errc::invalid_argument, "JPEG output is 8-bit only");
        if (channels != 1 && channels != 3)
            return makeError(Errc::invalid_argument, "JPEG output needs grey or RGB");
        size_ = size;
        channels_ = channels;
        bits_ = bits;
        f_ = openFile(temp_, "wb");
        if (!f_) return makeError(Errc::io_write, "Cannot create output file", pathToUtf8(temp_));
        c_.err = jpeg_std_error(&err_.mgr);
        err_.mgr.error_exit = jpegErrorExit;
        err_.mgr.output_message = jpegOutputMessage;
        if (setjmp(err_.jb)) return makeError(Errc::io_write, "Writing JPEG failed", err_.msg);
        jpeg_create_compress(&c_);
        created_ = true;
        jpeg_stdio_dest(&c_, f_);
        c_.image_width = static_cast<JDIMENSION>(size.w);
        c_.image_height = static_cast<JDIMENSION>(size.h);
        c_.input_components = channels;
        c_.in_color_space = channels == 1 ? JCS_GRAYSCALE : JCS_RGB;
        jpeg_set_defaults(&c_);
        jpeg_set_quality(&c_, std::clamp(opt_.jpegQuality, 1, 100), TRUE);
        for (int i = 0; i < c_.num_components; ++i) {
            c_.comp_info[i].h_samp_factor = 1;
            c_.comp_info[i].v_samp_factor = 1;
        }
        c_.optimize_coding = TRUE;
        if (opt_.dpiX > 0 && opt_.dpiY > 0) {
            c_.write_JFIF_header = TRUE;
            c_.density_unit = 1;
            c_.X_density = static_cast<UINT16>(std::min(65535.0, opt_.dpiX + 0.5));
            c_.Y_density = static_cast<UINT16>(std::min(65535.0, opt_.dpiY + 0.5));
        }
        jpeg_start_compress(&c_, TRUE);
        if (!opt_.xmp.empty()) {
            std::string app1 = std::string("http://ns.adobe.com/xap/1.0/") + '\0' + opt_.xmp;
            if (app1.size() <= 65533)
                jpeg_write_marker(&c_, JPEG_APP0 + 1, reinterpret_cast<const JOCTET*>(app1.data()),
                                  static_cast<unsigned>(app1.size()));
        }
        if (!opt_.icc.empty())
            jpeg_write_icc_profile(&c_, opt_.icc.data(), static_cast<unsigned>(opt_.icc.size()));
        begun_ = true;
        return {};
    }

    Status writeRows(const uint8_t* rows, int64_t n, size_t stride) override {
        FK_TRY(checkRows(n));
        if (setjmp(err_.jb)) return makeError(Errc::io_write, "Writing JPEG failed", err_.msg);
        for (int64_t i = 0; i < n; ++i) {
            JSAMPROW r = const_cast<JSAMPROW>(rows + static_cast<size_t>(i) * stride);
            jpeg_write_scanlines(&c_, &r, 1);
        }
        rowsWritten_ += n;
        return {};
    }

    Status finish() override {
        if (rowsWritten_ != size_.h) return makeError(Errc::internal, "finish before all rows were written");
        if (setjmp(err_.jb)) return makeError(Errc::io_write, "Writing JPEG failed", err_.msg);
        jpeg_finish_compress(&c_);
        jpeg_destroy_compress(&c_);
        created_ = false;
        const bool ok = std::fflush(f_) == 0;
        std::fclose(f_);
        f_ = nullptr;
        if (!ok) {
            removeTemp();
            return makeError(Errc::io_write, "Writing JPEG failed (disk full?)");
        }
        return commit();
    }

    void abort() override {
        if (created_) jpeg_destroy_compress(&c_);
        created_ = false;
        if (f_) std::fclose(f_);
        f_ = nullptr;
        removeTemp();
        done_ = true;
    }

  private:
    std::FILE* f_ = nullptr;
    jpeg_compress_struct c_{};
    JpegErr err_{};
    bool created_ = false;
};

// ================================ readers ==================================================
Result<ImageBuffer> readPng(const std::filesystem::path& path, ImageInfo* infoOut, MetadataSet* metaOut) {
    std::FILE* f = openFile(path, "rb");
    if (!f) return makeError(Errc::io_open, "Cannot open file", pathToUtf8(path));
    PngErr err;
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, &err, pngError, pngWarn);
    png_infop info = png ? png_create_info_struct(png) : nullptr;
    ImageBuffer buf;
    ImageInfo ii;
    MetadataSet meta;
    std::vector<png_bytep> rows;
    if (!png || !info) {
        std::fclose(f);
        return makeError(Errc::internal, "libpng initialisation failed");
    }
    if (setjmp(err.jb)) {
        png_destroy_read_struct(&png, &info, nullptr);
        std::fclose(f);
        return makeError(Errc::damaged_file, "PNG cannot be decoded", err.msg);
    }
    png_set_benign_errors(png, 1);
    png_init_io(png, f);
    png_read_info(png, info);
    png_uint_32 w = 0, h = 0;
    int depth = 0, ct = 0;
    png_get_IHDR(png, info, &w, &h, &depth, &ct, nullptr, nullptr, nullptr);
    if (ct == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (ct == PNG_COLOR_TYPE_GRAY && depth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (depth == 16 && std::endian::native == std::endian::little) png_set_swap(png);
    png_set_interlace_handling(png);
    png_read_update_info(png, info);
    const int channels = png_get_channels(png, info);
    const int bits = png_get_bit_depth(png, info) == 16 ? 16 : 8;
    auto b = ImageBuffer::create(w, h, channels, bits);
    if (!b) {
        png_destroy_read_struct(&png, &info, nullptr);
        std::fclose(f);
        return b.error();
    }
    buf = std::move(b.value());
    rows.resize(h);
    for (png_uint_32 y = 0; y < h; ++y)
        rows[y] = buf.row(y);
    png_read_image(png, rows.data());
    png_read_end(png, info);
    png_charp name = nullptr;
    int comp = 0;
    png_bytep prof = nullptr;
    png_uint_32 plen = 0;
    if (png_get_iCCP(png, info, &name, &comp, &prof, &plen) && prof && plen) ii.icc.assign(prof, prof + plen);
    png_uint_32 rx = 0, ry = 0;
    int unit = 0;
    if (png_get_pHYs(png, info, &rx, &ry, &unit) && unit == PNG_RESOLUTION_METER) {
        ii.dpiX = rx * 0.0254;
        ii.dpiY = ry * 0.0254;
    }
    png_textp texts = nullptr;
    int nt = 0;
    if (png_get_text(png, info, &texts, &nt) > 0) {
        for (int i = 0; i < nt; ++i)
            if (std::strcmp(texts[i].key, "XML:com.adobe.xmp") == 0)
                meta.sourceXmp.assign(texts[i].text,
                                      texts[i].itxt_length ? texts[i].itxt_length : texts[i].text_length);
    }
    png_destroy_read_struct(&png, &info, nullptr);
    std::fclose(f);
    ii.width = w;
    ii.height = h;
    ii.channels = channels;
    ii.bitsPerSample = bits;
    ii.hasAlpha = channels == 2 || channels == 4;
    ii.photometric = channels <= 2 ? Photometric::min_is_black : Photometric::rgb;
    ii.format = FileFormat::png;
    ii.compression = Compression::deflate;
    ii.iccDescription = iccDescription(ii.icc);
    std::error_code ec;
    ii.fileSize = std::filesystem::file_size(path, ec);
    if (infoOut) *infoOut = ii;
    if (metaOut) *metaOut = meta;
    return buf;
}

Result<ImageBuffer> readJpeg(const std::filesystem::path& path, ImageInfo* infoOut, MetadataSet* metaOut) {
    std::FILE* f = openFile(path, "rb");
    if (!f) return makeError(Errc::io_open, "Cannot open file", pathToUtf8(path));
    jpeg_decompress_struct d{};
    JpegErr err{};
    d.err = jpeg_std_error(&err.mgr);
    err.mgr.error_exit = jpegErrorExit;
    err.mgr.output_message = jpegOutputMessage;
    ImageBuffer buf;
    ImageInfo ii;
    MetadataSet meta;
    if (setjmp(err.jb)) {
        jpeg_destroy_decompress(&d);
        std::fclose(f);
        return makeError(Errc::damaged_file, "JPEG cannot be decoded", err.msg);
    }
    jpeg_create_decompress(&d);
    jpeg_stdio_src(&d, f);
    jpeg_save_markers(&d, JPEG_APP0 + 1, 0xFFFF);
    jpeg_save_markers(&d, JPEG_APP0 + 2, 0xFFFF);
    jpeg_read_header(&d, TRUE);
    if (d.jpeg_color_space == JCS_CMYK || d.jpeg_color_space == JCS_YCCK) {
        jpeg_destroy_decompress(&d);
        std::fclose(f);
        return makeError(Errc::unsupported_format, "CMYK JPEG files are not supported");
    }
    d.out_color_space = d.num_components == 1 ? JCS_GRAYSCALE : JCS_RGB;
    JOCTET* icc = nullptr;
    unsigned iccLen = 0;
    if (jpeg_read_icc_profile(&d, &icc, &iccLen) && icc) {
        ii.icc.assign(icc, icc + iccLen);
        std::free(icc);
    }
    for (jpeg_saved_marker_ptr m = d.marker_list; m; m = m->next) {
        static const char kXmp[] = "http://ns.adobe.com/xap/1.0/";
        if (m->marker == JPEG_APP0 + 1 && m->data_length > sizeof(kXmp) &&
            std::memcmp(m->data, kXmp, sizeof(kXmp)) == 0)
            meta.sourceXmp.assign(reinterpret_cast<const char*>(m->data) + sizeof(kXmp),
                                  m->data_length - sizeof(kXmp));
    }
    if (d.saw_JFIF_marker && d.density_unit == 1) {
        ii.dpiX = d.X_density;
        ii.dpiY = d.Y_density;
    } else if (d.saw_JFIF_marker && d.density_unit == 2) {
        ii.dpiX = d.X_density * 2.54;
        ii.dpiY = d.Y_density * 2.54;
    }
    jpeg_start_decompress(&d);
    auto b = ImageBuffer::create(d.output_width, d.output_height, d.output_components, 8);
    if (!b) {
        jpeg_destroy_decompress(&d);
        std::fclose(f);
        return b.error();
    }
    buf = std::move(b.value());
    while (d.output_scanline < d.output_height) {
        JSAMPROW r = buf.row(d.output_scanline);
        jpeg_read_scanlines(&d, &r, 1);
    }
    jpeg_finish_decompress(&d);
    ii.width = d.output_width;
    ii.height = d.output_height;
    ii.channels = d.output_components;
    jpeg_destroy_decompress(&d);
    std::fclose(f);
    ii.bitsPerSample = 8;
    ii.photometric = ii.channels == 1 ? Photometric::min_is_black : Photometric::rgb;
    ii.format = FileFormat::jpeg;
    ii.compression = Compression::jpeg;
    ii.iccDescription = iccDescription(ii.icc);
    std::error_code ec;
    ii.fileSize = std::filesystem::file_size(path, ec);
    if (infoOut) *infoOut = ii;
    if (metaOut) *metaOut = meta;
    return buf;
}

FileFormat sniff(const std::filesystem::path& path) {
    std::FILE* f = openFile(path, "rb");
    uint8_t h[8] = {0};
    if (f) {
        const size_t n = std::fread(h, 1, 8, f);
        (void)n;
        std::fclose(f);
    }
    if (h[0] == 0x89 && h[1] == 'P' && h[2] == 'N' && h[3] == 'G') return FileFormat::png;
    if (h[0] == 0xFF && h[1] == 0xD8) return FileFormat::jpeg;
    return FileFormat::tiff;
}

class BufferSource final : public ImageSource {
  public:
    BufferSource(const ImageBuffer& b, ImageInfo i) : b_(b), info_(std::move(i)) {}
    const ImageInfo& info() const override { return info_; }
    const MetadataSet& metadata() const override { return meta_; }
    Status readRows(int64_t y0, int64_t rows, int64_t x0, int64_t width, uint8_t* dst,
                    size_t stride) override {
        if (y0 < 0 || x0 < 0 || y0 + rows > b_.height() || x0 + width > b_.width())
            return makeError(Errc::invalid_argument, "Row request outside the image");
        const size_t bpp = b_.bytesPerPixel();
        for (int64_t y = 0; y < rows; ++y)
            std::memcpy(dst + static_cast<size_t>(y) * stride, b_.row(y0 + y) + static_cast<size_t>(x0) * bpp,
                        static_cast<size_t>(width) * bpp);
        return {};
    }

  private:
    const ImageBuffer& b_;
    ImageInfo info_;
    MetadataSet meta_;
};

} // namespace

Result<std::string> readXmpPacket(const std::filesystem::path& path) {
    const FileFormat f = sniff(path);
    if (f == FileFormat::tiff || f == FileFormat::dng) {
        auto src = openTiff(path, {});
        if (!src) return src.error();
        return (*src)->metadata().sourceXmp;
    }
    std::FILE* fp = openFile(path, "rb");
    if (!fp) return makeError(Errc::io_open, "Cannot open file", pathToUtf8(path));
    std::string out;
    auto rd = [&](void* b, size_t n) { return std::fread(b, 1, n, fp) == n; };
    if (f == FileFormat::png) {
        uint8_t sig[8];
        rd(sig, 8);
        for (;;) {
            uint8_t hdr[8];
            if (!rd(hdr, 8)) break;
            const uint32_t len =
                (uint32_t(hdr[0]) << 24) | (uint32_t(hdr[1]) << 16) | (uint32_t(hdr[2]) << 8) | hdr[3];
            const std::string type(reinterpret_cast<char*>(hdr + 4), 4);
            if (type == "IDAT" || type == "IEND" || len > (64u << 20)) break;
            std::string data(len, '\0');
            if (len && !rd(data.data(), len)) break;
            uint8_t crc[4];
            rd(crc, 4);
            if (type == "iTXt" && data.rfind("XML:com.adobe.xmp", 0) == 0) {
                // keyword\0 compflag compmethod lang\0 transkey\0 text
                size_t p = data.find('\0');
                if (p != std::string::npos && p + 2 < data.size() && data[p + 1] == 0) {
                    p += 3;
                    p = data.find('\0', p);
                    if (p != std::string::npos) p = data.find('\0', p + 1);
                    if (p != std::string::npos) out = data.substr(p + 1);
                }
                break;
            }
        }
    } else if (f == FileFormat::jpeg) {
        uint8_t m[2];
        rd(m, 2);
        for (;;) {
            if (!rd(m, 2) || m[0] != 0xFF) break;
            if (m[1] == 0xDA || m[1] == 0xD9) break;
            uint8_t l[2];
            if (!rd(l, 2)) break;
            const size_t len = (size_t(l[0]) << 8 | l[1]);
            if (len < 2) break;
            std::string data(len - 2, '\0');
            if (!rd(data.data(), data.size())) break;
            static const std::string kXmp = std::string("http://ns.adobe.com/xap/1.0/") + '\0';
            if (m[1] == 0xE1 && data.rfind(kXmp, 0) == 0) {
                out = data.substr(kXmp.size());
                break;
            }
        }
    }
    std::fclose(fp);
    return out;
}

std::unique_ptr<ImageSource> makeBufferSource(const ImageBuffer& buf, ImageInfo info) {
    info.width = buf.width();
    info.height = buf.height();
    info.channels = buf.channels();
    info.bitsPerSample = buf.bits();
    return std::make_unique<BufferSource>(buf, std::move(info));
}

Result<ImageBuffer> readImageFile(const std::filesystem::path& path, ImageInfo* info, MetadataSet* meta) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
        return makeError(Errc::not_found, "File not found", pathToUtf8(path));
    switch (sniff(path)) {
    case FileFormat::png:
        return readPng(path, info, meta);
    case FileFormat::jpeg:
        return readJpeg(path, info, meta);
    default:
        break;
    }
    auto src = openTiff(path, {});
    if (!src) return src.error();
    const ImageInfo& ii = (*src)->info();
    auto b = ImageBuffer::create(ii.width, ii.height, ii.channels, ii.bitsPerSample);
    if (!b) return b.error();
    constexpr int64_t kBand = 256;
    for (int64_t y = 0; y < ii.height; y += kBand) {
        const int64_t n = std::min(kBand, ii.height - y);
        FK_TRY((*src)->readRows(y, n, 0, ii.width, b->row(y), b->rowBytes()));
    }
    if (info) *info = ii;
    if (meta) *meta = (*src)->metadata();
    return b;
}

Result<std::unique_ptr<ImageSink>> createSink(const std::filesystem::path& finalPath,
                                              const WriteOptions& opt) {
    std::error_code ec;
    if (std::filesystem::exists(finalPath, ec))
        return makeError(Errc::exists, "Output file already exists; it was not overwritten",
                         pathToUtf8(finalPath));
    const auto dir = finalPath.parent_path().empty() ? std::filesystem::path(".") : finalPath.parent_path();
    if (!std::filesystem::is_directory(dir, ec))
        return makeError(Errc::not_found, "Output folder does not exist", pathToUtf8(dir));
    switch (opt.format) {
    case FileFormat::tiff:
        return makeTiffSink(finalPath, opt, false);
    case FileFormat::dng:
        return makeTiffSink(finalPath, opt, true);
    case FileFormat::png:
        return std::unique_ptr<ImageSink>(std::make_unique<PngSink>(finalPath, opt));
    case FileFormat::jpeg:
        return std::unique_ptr<ImageSink>(std::make_unique<JpegSink>(finalPath, opt));
    }
    return makeError(Errc::invalid_argument, "Unknown output format");
}

} // namespace fk
