// FrameKeeper core — image I/O (RFC-0003, ADR-0002).
#pragma once

#include "fk/common.hpp"
#include "fk/image.hpp"
#include "fk/metadata.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace fk {

/// Streaming pixel source. Rows should be requested in increasing y for efficiency.
class ImageSource {
  public:
    virtual ~ImageSource() = default;
    virtual const ImageInfo& info() const = 0;
    /// Reads `rows` rows starting at y0, columns [x0, x0+width), into dst (interleaved, file bit depth).
    virtual Status readRows(int64_t y0, int64_t rows, int64_t x0, int64_t width, uint8_t* dst,
                            size_t dstStride) = 0;
    /// Descriptive metadata of the source (privacy-classified).
    virtual const MetadataSet& metadata() const = 0;
};

struct TiffOpenOptions {
    uint64_t cacheBudgetBytes = 128ull << 20; ///< decoded strip/tile LRU budget
    /// Strips whose decoded size exceeds this are read with the sequential scanline cursor (ADR-0002).
    uint64_t maxCachedStripBytes = 64ull << 20;
};

enum class TiffAccessPath : int { tiles = 0, strips = 1, scanlines = 2 };
const char* tiffAccessPathName(TiffAccessPath p);

class TiffSource : public ImageSource {
  public:
    virtual TiffAccessPath accessPath() const = 0;
    virtual void setCacheBudget(uint64_t bytes) = 0;
    /// Bytes held by the decoded strip/tile cache and line buffers now (FK-005: counted in the app's cache
    /// budget). Not thread-safe against a concurrent readRows.
    virtual uint64_t cachedBytes() const = 0;
    /// Number of strip/tile decodes and scanline restarts so far (diagnostics, tests).
    virtual uint64_t decodeCount() const = 0;
    virtual uint64_t restartCount() const = 0;
};

Result<std::unique_ptr<TiffSource>> openTiff(const std::filesystem::path& path,
                                             const TiffOpenOptions& opt = {});

struct IntegrityReport {
    bool ok = true;
    std::vector<std::string> problems;
};
/// Validates header/IFD, strip/tile offsets inside the file, and decodes first and last rows.
Result<IntegrityReport> checkTiffIntegrity(const std::filesystem::path& path);

/// Decodes a whole TIFF/PNG/JPEG (print-sized images such as graded files).
Result<ImageBuffer> readImageFile(const std::filesystem::path& path, ImageInfo* info = nullptr,
                                  MetadataSet* meta = nullptr);

/// Reads only the XMP packet of a TIFF/PNG/JPEG without decoding pixels ("" when absent).
Result<std::string> readXmpPacket(const std::filesystem::path& path);

/// Wraps an in-memory image as an ImageSource (proxies, tests).
std::unique_ptr<ImageSource> makeBufferSource(const ImageBuffer& buf, ImageInfo info);

struct WriteOptions {
    FileFormat format = FileFormat::tiff;
    int bits = 16;
    Compression compression = Compression::lzw;
    bool predictor = true; ///< horizontal differencing for LZW/Deflate TIFF
    int jpegQuality = 100;
    int pngCompressionLevel = 6;
    std::vector<uint8_t> icc;  ///< embedded as-is (empty = untagged)
    double dpiX = 0, dpiY = 0; ///< 0 = not written
    MetadataSet meta;          ///< descriptive tags (already privacy-filtered)
    std::string xmp;           ///< our packet (buildXmp)
    bool allowBigTiff = true;
    bool hasAlpha = false;
    /// DNG only: ColorMatrix1 (XYZ D50 → camera) as 9 values, row-major; filled by the DNG export.
    std::vector<double> dngColorMatrix;
};

/// Streaming writer; writes to a temporary file next to the target and renames on finish().
class ImageSink {
  public:
    virtual ~ImageSink() = default;
    virtual Status begin(SizeI size, int channels, int bits) = 0;
    virtual Status writeRows(const uint8_t* rows, int64_t n, size_t stride) = 0;
    virtual Status finish() = 0;
    virtual void abort() = 0;
    virtual const std::filesystem::path& finalPath() const = 0;
};

/// Creates a sink for `finalPath`. Fails with Errc::exists if the target exists.
Result<std::unique_ptr<ImageSink>> createSink(const std::filesystem::path& finalPath,
                                              const WriteOptions& opt);

/// Estimated output bytes (uncompressed upper bound + headers).
uint64_t estimateOutputBytes(SizeI size, int channels, int bits, const WriteOptions& opt);

/// Free bytes on the volume containing `dir` (0 if unknown).
uint64_t freeDiskBytes(const std::filesystem::path& dir);

/// UTF-8 path helpers for C libraries.
std::FILE* openFile(const std::filesystem::path& p, const char* mode);

} // namespace fk
