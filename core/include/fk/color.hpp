// FrameKeeper core — colour management (RFC-0012, ADR-0003). Wraps LittleCMS 2.
#pragma once

#include "fk/common.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace fk {

/// ICC profile (immutable, shareable).
class Profile {
  public:
    static Result<Profile> fromIcc(const std::vector<uint8_t>& bytes);
    static Profile sRGB();
    static Profile adobeRGB1998();
    static Profile proPhoto();
    static Profile gray22();
    /// Same primaries/white as `p` with a linear (γ 1.0) TRC; RGB matrix-shaper profiles only.
    static Result<Profile> linearized(const Profile& p);

    const std::vector<uint8_t>& icc() const { return impl_->bytes; }
    const std::string& description() const { return impl_->description; }
    const std::string& sha256() const { return impl_->sha256; }
    bool isGray() const { return impl_->gray; }
    bool isRgb() const { return impl_->rgb; }
    void* handle() const { return impl_->handle; } ///< cmsHPROFILE
    /// Colourants as XYZ (D50-adapted, from the rXYZ/gXYZ/bXYZ tags) for matrix-shaper RGB profiles.
    bool colorants(std::array<double, 9>& xyzColumns) const;

  private:
    struct Impl {
        void* handle = nullptr;
        std::vector<uint8_t> bytes;
        std::string description, sha256;
        bool gray = false, rgb = false;
        ~Impl();
    };
    std::shared_ptr<Impl> impl_;
    static Profile wrap(void* handle);
};

enum class Intent : int {
    perceptual = 0,
    relative_colorimetric = 1,
    saturation = 2,
    absolute_colorimetric = 3
};

/// Pixel layout for a transform.
struct PixelLayout {
    int channels = 3; ///< 1 or 3 colour channels, +1 if alpha
    int bits = 16;    ///< 8, 16, or 32 (float)
    bool alpha = false;
};

/// Colour transform; apply() is thread-safe for distinct buffers.
class Transform {
  public:
    static Result<Transform> create(const Profile& src, const PixelLayout& in, const Profile& dst,
                                    const PixelLayout& out, Intent intent = Intent::relative_colorimetric,
                                    bool blackPointCompensation = true);
    void apply(const void* in, void* out, size_t pixels) const;
    bool valid() const { return impl_ != nullptr; }

  private:
    struct Impl {
        void* handle = nullptr;
        ~Impl();
    };
    std::shared_ptr<Impl> impl_;
};

/// Description of an ICC blob ("" if unreadable).
std::string iccDescription(const std::vector<uint8_t>& icc);

/// SHA-256 helper (hex, lowercase).
std::string sha256Hex(const void* data, size_t n);

/// sRGB-encoded [0,1] → approximate CIELAB (D65), used only for detection distances.
void srgbToLab(const float* rgb, float* lab);
/// CIEDE2000 colour difference of two CIELAB colours (Sharma, Wu, Dalal 2005). UX-08 edge measurements.
double deltaE2000(const float* lab1, const float* lab2);

} // namespace fk
