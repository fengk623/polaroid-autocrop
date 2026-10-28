// Internal: the shared "write side" of export and compositing — layout, colour policy (one conversion),
// depth, metadata/XMP, streaming sink, verification and report (RFC-0005, RFC-0007).
#pragma once

#include "fk/export.hpp"
#include "fk/hash.hpp"
#include "pixel_ops.hpp"

#include <memory>

namespace fk::detail {

struct OutputLayout {
    int srcCh = 0, srcBits = 0, outCh = 0, outBits = 0;
    bool srcAlpha = false;
    std::vector<std::string> notes;
};

/// Validates format/depth constraints. Errc::needs_confirmation for an unconsented 16→8 reduction.
Result<OutputLayout> computeOutputLayout(const ImageInfo& in, const WriteOptions& write,
                                         bool allowDepthReduction);

class OutputStage {
  public:
    struct Params {
        const ImageInfo* srcInfo = nullptr;
        const MetadataSet* srcMeta = nullptr;
        WriteOptions write;
        ColorPolicy color;
        PrivacyPreset privacy = PrivacyPreset::keep_all_safe;
        XmpIds ids;
        std::filesystem::path output;
        SizeI outSize{};
        double dpiScale = 1.0;
        bool allowDepthReduction = false;
    };
    ~OutputStage();
    Status open(const Params& p, ExportReport& rep);
    /// Rows in the source layout (source channels and depth).
    Status writeRows(const uint8_t* rows, int64_t n, size_t stride);
    Status finish(bool verify, const CancelToken& cancel, ExportReport& rep);
    void abort();
    const OutputLayout& layout() const { return lay_; }

  private:
    OutputLayout lay_;
    WriteOptions w_;
    RowConverter conv_;
    std::unique_ptr<ImageSink> sink_;
    Sha256 hash_;
    std::unique_ptr<SignatureAccumulator> sig_;
    AccountedBuffer outRow_;
    SizeI size_{};
    int64_t y_ = 0;
    bool open_ = false;
};

} // namespace fk::detail
