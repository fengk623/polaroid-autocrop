// Internal: common temp-file + atomic-rename behaviour for all image sinks (RFC-0003).
#pragma once

#include "fk/image_io.hpp"
#include "fk/util.hpp"

#include <filesystem>

namespace fk::detail {

class SinkBase : public ImageSink {
  public:
    SinkBase(std::filesystem::path finalPath, WriteOptions opt)
        : final_(std::move(finalPath)), opt_(std::move(opt)) {
        temp_ = final_;
        temp_ += ".fk-tmp-" + randomToken(8);
    }
    ~SinkBase() override = default; // derived classes call abort() in their destructors if needed
    const std::filesystem::path& finalPath() const override { return final_; }

  protected:
    /// fsync the temp file, then rename it to the final path without overwriting.
    Status commit();
    void removeTemp();
    Status checkRows(int64_t n) const;

    std::filesystem::path final_, temp_;
    WriteOptions opt_;
    SizeI size_{};
    int channels_ = 0, bits_ = 0;
    int64_t rowsWritten_ = 0;
    bool begun_ = false, done_ = false;
};

/// Flushes file contents to disk (best effort).
void syncFile(const std::filesystem::path& p);

} // namespace fk::detail
