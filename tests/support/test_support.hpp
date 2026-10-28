// Test helpers: temp dirs, deterministic images, an independent libtiff writer.
#pragma once

#include "fk/image.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace fk::test {

/// Fresh directory under the system temp dir, removed on destruction (AGENTS.md T8).
class TempDir {
  public:
    TempDir();
    ~TempDir();
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    const std::filesystem::path& path() const { return p_; }
    std::filesystem::path operator/(const std::string& name) const;

  private:
    std::filesystem::path p_;
};

/// Deterministic pseudo-random image (seeded), every sample distinct-ish to catch row/column mix-ups.
ImageBuffer makePatternImage(int64_t w, int64_t h, int channels, int bits, uint32_t seed = 1);

enum class TiffLayout { strips, single_strip, tiles };
struct TestTiffOptions {
    TiffLayout layout = TiffLayout::strips;
    uint32_t rowsPerStrip = 8;
    uint32_t tile = 16;
    int compression = 1; ///< libtiff COMPRESSION_* value
    int predictor = 1;
    bool planarSeparate = false;
    bool minIsWhite = false; ///< grey only: stores 1-v
    bool bigTiff = false;
    std::vector<uint8_t> icc;
    double dpi = 0;
};

/// Writes `img` with libtiff directly (independent from the product's writers).
void writeTestTiff(const std::filesystem::path& p, const ImageBuffer& img, const TestTiffOptions& o = {});

std::string sha256OfFile(const std::filesystem::path& p);
bool sameSamples(const ImageBuffer& a, const ImageBuffer& b);

} // namespace fk::test
