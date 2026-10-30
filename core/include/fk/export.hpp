// FrameKeeper core — export pipeline (RFC-0005, PRD F05).
#pragma once

#include "fk/color.hpp"
#include "fk/common.hpp"
#include "fk/geometry.hpp"
#include "fk/image_io.hpp"
#include "fk/metadata.hpp"
#include "fk/relink.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace fk {

enum class ExportTarget : int { image_area = 0, full_print = 1 };
const char* exportTargetName(ExportTarget t);

struct ColorPolicy {
    enum Kind : int { keep = 0, convert = 1, assign = 2, untagged = 3 } kind = keep;
    std::vector<uint8_t> icc; ///< target (convert) or assigned (assign) profile
    Intent intent = Intent::relative_colorimetric;
    bool bpc = true;
};
const char* colorPolicyName(ColorPolicy::Kind k);

struct ExportRequest {
    ExportTarget target = ExportTarget::image_area;
    Shape region{}; ///< what to export (inner shape or outer quad), scan coordinates
    Quad frame{};   ///< outer quad (defines the straightening angle)
    GeometryMode mode = GeometryMode::pixel_crop;
    int upright = 0;
    std::optional<SizeI> standardSize;
    WriteOptions write; ///< format, bits, compression…; icc/dpi/xmp/meta are filled by the pipeline
    ColorPolicy color;
    PrivacyPreset privacy = PrivacyPreset::keep_all_safe;
    std::filesystem::path output;
    bool reviewed = true; ///< false for unreviewed low-confidence items → Errc::needs_review
    bool allowDepthReduction = false;
    XmpIds ids;
    bool verify = true;
};

struct ExportReport {
    std::filesystem::path output;
    SizeI size{};
    int bits = 0, channels = 0;
    FileFormat format = FileFormat::tiff;
    bool resampled = false;
    bool frameBitExact = false;
    std::vector<std::string> notExactReasons;
    std::vector<std::string> notes;
    std::string sha256;      ///< of the written file
    std::string pixelSha256; ///< of the written pixel rows (lossless formats)
    ExportGeometry geometry{};
    Signature signature{};
    std::string iccDescription, iccSha256;
    double dpiX = 0, dpiY = 0;
    double seconds = 0;
    uint64_t peakAccountedBytes = 0;
};

/// Pre-flight summary shown before export (no file is written).
struct ExportPlan {
    ExportGeometry geometry{};
    int outChannels = 0, outBits = 0;
    bool frameBitExact = false;
    std::vector<std::string> notExactReasons;
    uint64_t estimatedBytes = 0;
    uint64_t freeBytes = 0;
    uint64_t estimatedPeakMemory = 0;
    std::string iccDescription;
    /// FK-010 (U010): the real output, so prompts follow it — not the settings.
    bool reducesDepth = false;      ///< 16 → 8 bits (needs the user's explicit consent)
    std::string interpolation;      ///< "none" (pixel copy) or "Lanczos-3"
    std::vector<std::string> notes; ///< alpha dropped, round image area handling, …
};
Result<ExportPlan> planExport(const ImageSource& src, const ExportRequest& req);
/// The same plan from the scan's summary (no file access; for prompts). The disk figures stay 0.
Result<ExportPlan> planExport(const ImageInfo& in, const ExportRequest& req);

Result<ExportReport> exportRegion(ImageSource& src, const ExportRequest& req, const CancelToken& cancel,
                                  const ProgressFn& progress);

/// `<dir>/<base>_v001.<ext>`, `_v002`, … : the first name that does not exist.
std::filesystem::path nextFreeVersionPath(const std::filesystem::path& dir, const std::string& base,
                                          const std::string& ext, int* versionOut = nullptr);
/// `<base>.<ext>`, then `<base>-2.<ext>`, … : the first name that does not exist.
std::filesystem::path nextFreePath(const std::filesystem::path& dir, const std::string& base,
                                   const std::string& ext);
std::string fileExtension(FileFormat f);
/// Item label used in file names: P01 … P12.
std::string itemLabel(int index);

} // namespace fk
