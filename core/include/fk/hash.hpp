// FrameKeeper core — SHA-256 (self-contained; used for fingerprints, export verification, cache keys).
#pragma once

#include "fk/common.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace fk {

class Sha256 {
  public:
    Sha256();
    void update(const void* data, size_t n);
    std::array<uint8_t, 32> finish();
    std::string finishHex();

  private:
    void block(const uint8_t* p);
    std::array<uint32_t, 8> h_{};
    std::array<uint8_t, 64> buf_{};
    uint64_t len_ = 0;
    size_t used_ = 0;
};

std::string toHex(const uint8_t* p, size_t n);
/// Streams the whole file (1 MiB buffer). Errc::io_open / io_read on failure.
Result<std::string> sha256File(const std::filesystem::path& p, const CancelToken* cancel = nullptr);

} // namespace fk
