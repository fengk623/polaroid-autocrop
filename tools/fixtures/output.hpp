// fkfixture — streaming TIFF writer, ICC profile and preview PNG.
#pragma once

#include "scene.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

struct tiff; // libtiff's TIFF

namespace fkfx {

std::FILE* openFile(const std::filesystem::path& path, const char* mode);
std::string pathString(const std::filesystem::path& path); // UTF-8
bool writeTextFile(const std::filesystem::path& path, const std::string& text, std::string& err);

// sRGB (RGB) or grey-with-sRGB-TRC (1 channel) profile from lcms2, with a fixed creation date so the
// output is byte-identical between runs.
std::vector<std::uint8_t> makeIccProfile(int channels);

// Writes packed rows (contig samples, native byte order) in increasing order. Scanline layouts
// stream through a bounded libtiff buffer; tiled layout takes bands of exactly one tile row.
class TiffWriter {
  public:
    TiffWriter() = default;
    TiffWriter(const TiffWriter&) = delete;
    TiffWriter& operator=(const TiffWriter&) = delete;
    ~TiffWriter();
    bool open(const std::filesystem::path& path, const Scene& scene, std::string& err);
    bool writeBand(const std::uint8_t* band, int y0, int rows, std::string& err);
    bool close(std::string& err);

  private:
    tiff* tif_ = nullptr;
    const Scene* sc_ = nullptr;
    std::size_t rowBytes_ = 0;
    std::vector<std::uint8_t> scratch_;
};

// Running box-filter downsample (long side 1200 px) fed with the same packed rows as the TIFF.
class PreviewBuilder {
  public:
    void init(const Scene& scene);
    void addRow(const std::uint8_t* row, int y);
    // Draws ground-truth overlays (outer quads red, inner shapes green) and writes the PNG.
    bool write(const std::filesystem::path& path, std::string& err);

  private:
    void flushRow();
    void plot(int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b);
    void line(Vec2 a, Vec2 b, std::uint8_t r, std::uint8_t g, std::uint8_t bl);
    Vec2 map(Vec2 p) const { return {p.x * fx_, p.y * fy_}; }

    const Scene* sc_ = nullptr;
    int pw_ = 0, ph_ = 0;
    double fx_ = 1, fy_ = 1;
    std::vector<int> colMap_;
    std::vector<double> acc_;
    std::vector<std::uint32_t> cnt_;
    int curRow_ = -1;
    std::vector<std::uint8_t> img_;
};

} // namespace fkfx
