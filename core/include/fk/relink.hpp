// FrameKeeper core — relinking graded files (RFC-0006, ADR-0009, PRD F06).
#pragma once

#include "fk/common.hpp"
#include "fk/image.hpp"

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace fk {

/// 64×64 rank-normalised luminance signature (tone- and colour-change tolerant).
struct Signature {
    static constexpr int kSide = 64;
    std::array<float, kSide * kSide> v{};
    bool valid = false;
};

/// Accumulates a signature from streamed rows of an image of size (w,h).
class SignatureAccumulator {
  public:
    SignatureAccumulator(int64_t w, int64_t h);
    /// Adds `n` rows starting at output row y0 (interleaved samples, `bits` 8/16, `channels` 1–4).
    void addRows(int64_t y0, int64_t n, const uint8_t* rows, size_t stride, int channels, int bits);
    Signature finish() const;

  private:
    int64_t w_, h_;
    std::vector<double> sum_;
    std::vector<double> cnt_;
};

Signature computeSignature(const ImageBuffer& img);
/// Signature after rotating the image by k×90° clockwise (k = 0..3).
Signature rotateSignature(const Signature& s, int k);
/// Pearson correlation of two signatures in [-1, 1] (0 if either is invalid).
double compareSignatures(const Signature& a, const Signature& b);
inline constexpr double kSignatureMatchThreshold = 0.80;

std::string encodeSignature(const Signature& s); ///< base64 of float32 LE
Result<Signature> decodeSignature(const std::string& b64);

/// Parsed `<scan>_P<nn>_EDIT_v<nnn>` file-name pattern.
struct EditFileName {
    std::string scanStem;
    int itemIndex = 0;
    int version = 0;
};
std::optional<EditFileName> parseEditFileName(const std::string& stem);
/// Stems tried when looking for a sidecar next to a graded file (editor suffixes removed).
std::vector<std::string> sidecarStemCandidates(const std::string& stem);

} // namespace fk
