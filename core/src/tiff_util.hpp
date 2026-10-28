// Internal libtiff helpers shared by the TIFF reader and writers.
#pragma once

#include "fk/common.hpp"
#include "fk/image.hpp"

#include <tiffio.h>

#include <filesystem>
#include <string>

namespace fk::detail {

/// Opens a TIFF with a UTF-8/wide path (TIFFOpenW on Windows).
TIFF* tiffOpenPath(const std::filesystem::path& p, const char* mode);
void clearTiffError();
std::string lastTiffError();

struct RawTiffLayout {
    int spp = 0;
    bool minIsWhite = false;
    bool ycbcrJpeg = false;
};

/// Validates the current directory and fills ImageInfo. Errc::unsupported_format with a reason.
Status readTiffInfo(TIFF* t, const std::filesystem::path& path, ImageInfo& info, RawTiffLayout& raw);

} // namespace fk::detail
