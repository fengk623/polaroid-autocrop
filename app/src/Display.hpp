// Display conversion (ADR-0003): scan pixels → sRGB 8-bit QImage, once, on a worker thread. The canvas
// never colour-converts while painting; overlays and the surround never pass through the image transform.
#pragma once

#include "fk/color.hpp"
#include "fk/detection.hpp"
#include "fk/image.hpp"

#include <QImage>

#include <optional>

namespace fkapp {

/// The profile the canvas converts to (ADR-0003, AGENTS §6): on Windows the primary monitor's ICC profile
/// when one is installed and usable (Windows does not colour-manage windows), else sRGB; sRGB on macOS, where
/// Qt windows are sRGB-tagged and the system converts to the display. FK_DISPLAY_SRGB=1 forces sRGB.
const fk::Profile& displayProfile();

/// Converts rows of a scan (its bit depth, 1–4 channels) to display RGB32 (see displayProfile()).
class DisplayConverter {
  public:
    /// `icc` empty = untagged (shown as sRGB / grey 2.2).
    bool init(const fk::ImageInfo& info);
    /// Converts `w` pixels of one source row (interleaved, file depth) into `dst` (QImage::Format_RGB32 row).
    void convertRow(const uint8_t* src, int64_t w, uint32_t* dst) const;
    /// Converts the detection proxy (float RGB of the encoded values; grey replicated) to an sRGB image.
    QImage convertProxy(const fk::Proxy& p) const;

  private:
    fk::ImageInfo info_;
    fk::Transform rowTransform_;   ///< source layout → sRGB 8-bit RGB
    fk::Transform proxyTransform_; ///< float RGB (or float grey) → sRGB 8-bit RGB
    bool gray_ = false;
    int colorCh_ = 3;
};

/// Downscales by an integer box factor (thumbnails).
QImage boxDownscale(const QImage& src, int longSide);

} // namespace fkapp
