// FrameKeeper core — compositing graded images into the real scanned frame (RFC-0007, ADR-0010, PRD F07)
// and PhotoFrame-style synthetic frames (RFC-0008, PRD F08).
#pragma once

#include "fk/export.hpp"
#include "fk/geometry.hpp"
#include "fk/image_io.hpp"

#include <optional>
#include <vector>

namespace fk {

struct MaskStroke {
    std::vector<Vec2> points; ///< scan coordinates
    double radiusPx = 10;
    bool add = true; ///< add (graded) or remove (keep scan)
};

/// Confirmed adjustments for a graded file whose geometry differs from the export (PRD F06 table).
struct EditAdjust {
    int rotate = 0;          ///< extra 90° clockwise turns the user applied in the editor (0..3)
    bool allowScale = false; ///< same aspect, different size: resample after confirmation
    bool allowDepthReduction = false;
};

struct CompositeRequest {
    Shape outerRegion{}; ///< full print (outer quad) — what is exported
    Quad frame{};
    Shape inner{}; ///< image area in scan coordinates
    /// Mapping from scan coordinates to the coordinates of the image exported for editing (sidecar
    /// scanToOut).
    Mat3 scanToEdit{};
    SizeI editSize{};       ///< exported image size (sidecar exportSize)
    bool editExact = false; ///< exported in pixel_crop mode (integer translation / 90° permutation)
    const ImageBuffer* edited = nullptr;
    std::vector<uint8_t>
        editedIcc; ///< resolved by the caller (assume-exported or user choice); empty = same as scan
    EditAdjust adjust{};
    MaskParams mask{};
    std::vector<MaskStroke> strokes;
    GeometryMode outputMode = GeometryMode::pixel_crop;
    int upright = 0;
    WriteOptions write{};
    ColorPolicy color{};
    PrivacyPreset privacy = PrivacyPreset::keep_all_safe;
    std::filesystem::path output;
    XmpIds ids{};
    bool verify = true;
};

/// Checks the graded image against the export geometry (size, aspect, depth) without compositing.
struct CompositeCheck {
    bool sizeMatches = false;
    bool aspectMatches = false;
    int suggestedRotate = 0;
    bool depthLower = false;
    std::vector<std::string> actionsNeeded; ///< "scale", "rotate", "accept_lower_depth"
};
CompositeCheck checkEditedGeometry(SizeI editSize, const ImageBuffer& edited, int scanBits);

/// Composites and writes the full print. Frame pixels come from the original scan.
Result<ExportReport> compositeItem(ImageSource& scan, const CompositeRequest& req, const CancelToken& cancel,
                                   const ProgressFn& progress);

/// Same pipeline into memory, in the scan's depth and space (preview, tests). `scale` < 1 renders smaller.
Result<ImageBuffer> compositeToBuffer(ImageSource& scan, const CompositeRequest& req, double scale,
                                      const CancelToken& cancel);

// ---- synthetic frame (RFC-0008) ----------------------------------------------------------------------
enum class FrameSpecMode : int { equal = 0, canvas_ratio = 1, sides = 2 };
enum class FrameUnit : int { percent_long_edge = 0, pixels = 1 };
struct FrameSpec {
    FrameSpecMode mode = FrameSpecMode::sides;
    FrameUnit unit = FrameUnit::percent_long_edge;
    double top = 5, right = 5, bottom = 18, left = 5; ///< instant-film style default
    double ratioW = 4, ratioH = 5;                    ///< canvas_ratio
    double minMargin = 4;                             ///< canvas_ratio, same unit
    std::array<uint16_t, 3> color{65535, 65535, 65535};
    std::optional<SizeI> platformTarget; ///< pad to ratio, then resize to this size
};
struct FrameLayout {
    SizeI canvas{};
    int64_t offsetX = 0, offsetY = 0; ///< picture position in the canvas
    int64_t top = 0, right = 0, bottom = 0, left = 0;
};
FrameLayout computeFrameLayout(SizeI picture, const FrameSpec& spec);
/// Renders picture + synthetic frame (picture pixels copied unchanged; colour in the picture's depth).
Result<ImageBuffer> renderSyntheticFrame(const ImageBuffer& picture, const FrameSpec& spec);
/// Median colour (16-bit) of the scanned frame band of a print (for the synthetic frame default colour).
Result<std::array<uint16_t, 3>> sampleFrameColor(ImageSource& scan, const Shape& outer, const Shape& inner);

} // namespace fk
