// FrameKeeper core — print detection (RFC-0004, PRD F02/F03/F09).
#pragma once

#include "fk/common.hpp"
#include "fk/geometry.hpp"
#include "fk/image_io.hpp"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace fk {

struct FilmType {
    std::string id, name;
    double widthMm = 0, heightMm = 0;           ///< card, upright
    double imageWidthMm = 0, imageHeightMm = 0; ///< image area (quad) or circle bounding box
    double imageLeftMm = 0, imageTopMm = 0;     ///< image offset from the card's top-left, upright
    double cornerRadiusMm = 0;
    double toleranceMm = 0;
    ShapeKind innerShape = ShapeKind::quad;
    bool approximatePlacement = true;
};
/// Built-in library (core/detection/film_types.json, embedded at build time).
const std::vector<FilmType>& builtinFilmTypes();
const FilmType* findFilmType(const std::string& id);

/// Downscaled copy of a scan used for detection and preview. Values are the file's encoded samples
/// normalised to [0,1] (grey replicated to RGB, alpha dropped).
struct Proxy {
    int64_t width = 0, height = 0;
    int scale = 1; ///< full-resolution pixels per proxy pixel (box filter)
    int64_t sourceWidth = 0, sourceHeight = 0;
    std::vector<float> rgb;       ///< width*height*3
    std::shared_ptr<void> ticket; ///< BufferAccounting registration (MemCategory::proxy)
    const float* px(int64_t x, int64_t y) const {
        return rgb.data() + (static_cast<size_t>(y) * width + x) * 3;
    }
};

/// Streams the source once. `longSide` is the target long side (default 3000).
Result<Proxy> buildProxy(ImageSource& src, int longSide, const CancelToken& cancel,
                         const ProgressFn& progress);

/// A template slot in relative scan coordinates (u = x / width, v = y / height).
struct TemplateSlot {
    int index = 0;
    std::string filmType;
    std::array<Vec2, 4> outer{};
    Shape inner{}; ///< in relative coordinates (ellipse radii relative to width)
};
struct Template {
    std::string schema = "fk.template/1";
    std::string templateId, name;
    double createdDpi = 0;
    SizeI scanSize{};
    std::vector<TemplateSlot> slots;
};

struct DetectParams {
    int expectedCount = 4;
    int maxCount = 12;
    bool refine = true;             ///< stage B full-resolution refinement
    double safetyMarginPx = 1.0;    ///< extra outward expansion after containment
    double dpiOverride = 0;         ///< use instead of file DPI when > 0
    const Template* tmpl = nullptr; ///< apply a jig template instead of free detection
};

struct DetectedItem {
    Shape outer{}; ///< always a quad, scan coordinates
    double cornerRadiusPx = 0;
    Shape inner{}; ///< quad or ellipse, scan coordinates
    std::string filmTypeId;
    int upright = 0; ///< 90° clockwise turns that bring the widest border to the bottom
    double confidence = 0, outerConfidence = 0, innerConfidence = 0;
    std::vector<std::string> reasons;
    std::array<double, 4> refineOffsetPx{}; ///< outward shift of each outer side by refinement
                                            ///< (top,right,bottom,left of the quad order)
    double contrast = 0;                    ///< paper vs background distance / background noise
};

struct DetectionResult {
    std::vector<DetectedItem> items;
    std::vector<std::string> warnings;
    int proxyScale = 1;
    double dpiUsed = 0;
    double secondsDetect = 0, secondsRefine = 0;
};

/// Full detection (stage A on the proxy + stage B on the full-resolution source).
Result<DetectionResult> detectPrints(ImageSource& src, const Proxy& proxy, const DetectParams& params,
                                     const CancelToken& cancel, const ProgressFn& progress);

/// Re-runs stage B for user-adjusted boundaries ("snap to edge").
Result<DetectedItem> snapToEdges(ImageSource& src, const DetectedItem& seed, double dpi,
                                 const CancelToken& cancel);

/// Confidence classes used by UI and export gating.
enum class ConfidenceClass : int { high = 0, medium = 1, low = 2 };
ConfidenceClass classify(double confidence);
inline constexpr double kLowConfidence = 0.6;
inline constexpr double kHighConfidence = 0.85;

/// Template creation/application helpers.
Template makeTemplate(const std::vector<DetectedItem>& items, SizeI scanSize, double dpi,
                      const std::string& name);

} // namespace fk
