// Display conversion (ADR-0003): scan pixels → sRGB 8-bit QImage, once, on a worker thread. The canvas
// never colour-converts while painting; overlays and the surround never pass through the image transform.
#pragma once

#include "fk/color.hpp"
#include "fk/detection.hpp"
#include "fk/image.hpp"

#include <QImage>
#include <QString>

#include <optional>

class QScreen;

namespace fkapp {

/// A display profile (ADR-0003, FK-011): what the canvas, thumbnails and previews are converted to.
struct DisplayProfileInfo {
    fk::Profile profile = fk::Profile::sRGB();
    QString id = QStringLiteral("sRGB"); ///< "sRGB", or the ICC's SHA-256 (changes when the profile changes)
    QString description = QStringLiteral("sRGB IEC61966-2.1");
    QString source; ///< where it came from (About dialog, diagnostics)
};

/// Reads the display profile of `screen`: on Windows that monitor's ICC profile (GetICMProfileW on the
/// monitor's device context; Windows does not colour-manage windows) when one is installed and usable, else
/// sRGB; sRGB on macOS, where Qt windows are sRGB-tagged and the system converts to the display, and on
/// Linux. FK_DISPLAY_SRGB=1 forces sRGB. Cheap to call again (the ICC file is parsed only when it changed).
DisplayProfileInfo readDisplayProfile(QScreen* screen);
/// The profile conversions use now (thread-safe copy; jobs take it when they start).
DisplayProfileInfo currentDisplayProfile();
/// Sets the current display profile; true when it differs from the previous one.
bool setCurrentDisplayProfile(const DisplayProfileInfo& p);

/// Converts rows of a scan (its bit depth, 1–4 channels) to display RGB32 (see currentDisplayProfile()).
class DisplayConverter {
  public:
    /// `icc` empty = untagged (shown as sRGB / grey 2.2). Converts to the current display profile, or to
    /// `to`.
    bool init(const fk::ImageInfo& info, const DisplayProfileInfo* to = nullptr);
    /// Converts `w` pixels of one source row into `dst` (QImage::Format_RGB32 row). The row is what
    /// ImageSource::readRows returns: normal polarity also for MinIsWhite files (the reader already inverted
    /// them; inverting again showed MinIsWhite scans as negatives at full resolution, FK-011).
    void convertRow(const uint8_t* src, int64_t w, uint32_t* dst) const;
    /// The display profile the converter targets (results made with another one are outdated).
    const QString& profileId() const { return profileId_; }
    /// Converts the detection proxy (float RGB of the encoded values; grey replicated) to an sRGB image.
    QImage convertProxy(const fk::Proxy& p) const;

  private:
    fk::ImageInfo info_;
    fk::Transform rowTransform_;   ///< source layout → sRGB 8-bit RGB
    fk::Transform proxyTransform_; ///< float RGB (or float grey) → sRGB 8-bit RGB
    bool gray_ = false;
    int colorCh_ = 3;
    QString profileId_;
};

/// Converts display RGB32 pixels from one display profile to another in place (thumbnails kept when the
/// window moves to another monitor).
void convertDisplayImage(QImage& img, const fk::Profile& from, const fk::Profile& to);

/// Downscales by an integer box factor (thumbnails).
QImage boxDownscale(const QImage& src, int longSide);

} // namespace fkapp
