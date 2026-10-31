// FrameKeeper core — accounted buffers (RFC-0015, ADR-0007).
//
// Every large pixel buffer in the core is allocated through AccountedBuffer so that
//  * diagnostics can report bytes in use and the peak per category, independent of RSS;
//  * an optional hard limit (mobile_lite profile) turns over-budget allocations into
//    Errc::memory_budget instead of an OS kill.
#pragma once

#include "fk/common.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace fk {

enum class MemCategory : int { proxy = 0, decode_cache, band, region, image, display, other, count_ };
const char* memCategoryName(MemCategory c);

class BufferAccounting {
  public:
    static BufferAccounting& instance();

    /// Reserve bytes; fails (returns false) only if a limit is set and would be exceeded.
    bool tryReserve(MemCategory c, uint64_t bytes);
    /// Records already-allocated bytes (no limit check).
    void forceReserve(MemCategory c, uint64_t bytes);
    void release(MemCategory c, uint64_t bytes);

    uint64_t current() const;
    uint64_t current(MemCategory c) const;
    uint64_t peak() const;
    void resetPeak();

    /// 0 = unlimited (desktop default).
    void setLimit(uint64_t bytes);
    uint64_t limit() const;

  private:
    BufferAccounting() = default;
    std::array<std::atomic<uint64_t>, static_cast<size_t>(MemCategory::count_)> byCat_{};
    std::atomic<uint64_t> total_{0};
    std::atomic<uint64_t> peak_{0};
    std::atomic<uint64_t> limit_{0};
};

/// Owning byte buffer registered with BufferAccounting. Move-only.
class AccountedBuffer {
  public:
    AccountedBuffer() = default;
    static Result<AccountedBuffer> allocate(MemCategory c, size_t bytes, bool zeroFill = false);
    ~AccountedBuffer();
    AccountedBuffer(AccountedBuffer&& o) noexcept;
    AccountedBuffer& operator=(AccountedBuffer&& o) noexcept;
    AccountedBuffer(const AccountedBuffer&) = delete;
    AccountedBuffer& operator=(const AccountedBuffer&) = delete;

    uint8_t* data() { return data_.get(); }
    const uint8_t* data() const { return data_.get(); }
    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    void reset();

  private:
    std::unique_ptr<uint8_t[]> data_;
    size_t size_ = 0;
    MemCategory cat_ = MemCategory::other;
};

/// Registers `bytes` in category `c` until the returned token is destroyed (for std::vector-backed data).
std::shared_ptr<void> accountBytes(MemCategory c, uint64_t bytes);

/// Snapshot of process memory from the OS (best effort; fields 0 if unavailable).
struct MemoryStats {
    uint64_t residentBytes = 0;     ///< current RSS / working set
    uint64_t peakResidentBytes = 0; ///< peak RSS / peak working set
    uint64_t privateBytes = 0;      ///< Windows PrivateUsage, macOS phys_footprint, Linux RssAnon
    uint64_t accountedBytes = 0;    ///< BufferAccounting::current()
    uint64_t accountedPeakBytes = 0;
};
MemoryStats currentMemoryStats();

/// Physical memory of the device (best effort; 0 when unavailable). Sizes the cache budget (FK-005) and
/// tells memory pressure on platforms without a notification (Linux).
struct SystemMemory {
    uint64_t totalBytes = 0;
    uint64_t availableBytes =
        0; ///< Linux MemAvailable, Windows ullAvailPhys, macOS free + inactive + purgeable
};
SystemMemory systemMemory();

} // namespace fk
