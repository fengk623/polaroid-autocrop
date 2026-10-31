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

/// UX-08: a print's mask as the user chose it. The width (feather ramp or preserve-edge inset; ignored for a
/// hard edge) is in mm when the scan's DPI is reliable — converted when compositing, so a choice is the same
/// physical edge at any DPI — or in px when there is no reliable DPI (or the print was set up before
/// 0.7.0-alpha.5).
struct MaskChoice {
    MaskMode mode = MaskMode::hard_edge;
    double width = 0;
    bool mm = false;
    friend bool operator==(const MaskChoice&, const MaskChoice&) = default;
};
/// Natural-blend presets (RFC-0007 §Natural blend), chosen with `fkcli maskeval` on the owner's real scans (7
/// prints, 826 and 1200 dpi, ground truth): feather 0.05 mm had the smallest worst-case seam at 50 % (p99
/// ΔE00 3.64 vs 6.61 for a hard edge, every print better) with an ungraded rim ≤ 0.33 px; the trial value
/// 0.03 mm left 4.71. Preserve edge: 0.02 mm (3.96, rim ≤ 0.16 px). 1.6 / 2.4 / 3.1 px and 0.63 / 0.94 / 1.26
/// px at 800 / 1200 / 1600 dpi.
inline constexpr double kNaturalFeatherMm = 0.05;
inline constexpr double kNaturalInsetMm = 0.02;
/// The same edges in px for scans without a reliable DPI (as at 1200 dpi).
inline constexpr double kNaturalFeatherPx = 2.4;
inline constexpr double kNaturalInsetPx = 0.9;
/// The natural-blend preset of `mode` (feather or preserve edge; hard edge = 0 px, the archival exact path).
MaskChoice naturalBlend(MaskMode mode, bool dpiReliable);
/// The renderer's parameters for a choice: the one conversion used by the composite preview and the final
/// output. A width in mm with no DPI (≤ 0) is converted at 300 dpi.
MaskParams resolveMask(const MaskChoice& c, double dpi);

/// UX-08: how strongly a composite's picture differs from the scan just inside the image-area edge: the
/// median ΔE00 (values taken as sRGB) over pixels 0.2–0.6 mm inside `inner`. Both buffers cover the same
/// area: buffer pixel (u, v) is scan point `origin` + (u + 0.5, v + 0.5) / `scale`. Above kStrongEdgeChange
/// no single default mask can be promised to look seamless (RFC-0007 §Natural blend): the app asks for a
/// check at 100 % and an adjusted mask or alignment. 0 when nothing could be measured.
inline constexpr double kStrongEdgeChange = 10.0;
double edgeChange(const ImageBuffer& composite, const ImageBuffer& scanOnly, const Shape& inner, Vec2 origin,
                  double scale, double pxPerMm);

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
