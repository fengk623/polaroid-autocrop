// FrameKeeper core — one byte budget for the in-memory caches (RFC-0014, FK-005).
//
// The caches themselves stay with their owners (the app keeps display proxies, decoded-strip sources,
// composite previews and thumbnails in its own containers). The ledger only knows each entry's key, size
// and last use, and decides what to drop: least recently used first, never a pinned entry (what is on
// screen) and never one marked as kept (thumbnails, the UI's index). Jobs reserve their working memory
// before they start, and the caches make room for it. Everything in the ledger can be rebuilt from the
// source files: dropping an entry only costs time, never output quality.
#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fk {

class CacheLedger {
  public:
    struct Entry {
        std::string key;
        uint64_t bytes = 0;
        uint64_t lastUse = 0;
        bool pinned = false;
        bool kept = false; ///< accounted, never dropped by the budget (thumbnails)
    };

    void setBudget(uint64_t bytes) { budget_ = bytes; }
    uint64_t budget() const { return budget_; }

    /// Adds or updates an entry and marks it most recently used. `kept` entries count but are never dropped.
    void put(const std::string& key, uint64_t bytes, bool kept = false);
    /// Marks an entry most recently used (no-op if absent).
    void touch(const std::string& key);
    void erase(const std::string& key);
    bool contains(const std::string& key) const { return entries_.count(key) != 0; }
    /// Pinned keys are never dropped (what is on screen). Replaces the previous set; keys need not exist yet.
    void setPinned(std::set<std::string> keys);
    bool pinned(const std::string& key) const { return pinned_.count(key) != 0; }

    /// Reserves working memory for a job: the caches leave room for it until `release(token)`. Returns a
    /// token (> 0).
    uint64_t reserve(uint64_t bytes);
    void release(uint64_t token);
    uint64_t reserved() const;

    /// Keys to drop so that the droppable bytes fit: total ≤ max(budget − reserved, bytes that cannot be
    /// dropped). Least recently used first. The returned entries are removed from the ledger; the owner
    /// frees them.
    std::vector<std::string> enforce();
    /// Memory pressure: drops droppable entries (least recently used first) until the total is ≤ `bytes`.
    std::vector<std::string> shrinkTo(uint64_t bytes);

    uint64_t bytes() const { return total_; }
    /// Bytes of entries whose key starts with `prefix` (e.g. "display:").
    uint64_t bytes(const std::string& prefix) const;
    /// Bytes that the budget can never drop (pinned or kept).
    uint64_t fixedBytes() const;
    size_t count(const std::string& prefix = {}) const;
    uint64_t evictions() const { return evictions_; }
    std::vector<Entry> entries() const;

  private:
    struct Slot {
        uint64_t bytes = 0;
        uint64_t lastUse = 0;
        bool kept = false;
    };
    bool droppable(const std::string& key, const Slot& s) const { return !s.kept && !pinned_.count(key); }
    std::vector<std::string> dropUntil(uint64_t target);

    std::map<std::string, Slot> entries_;
    std::set<std::string> pinned_;
    std::map<uint64_t, uint64_t> reservations_;
    uint64_t budget_ = 0, total_ = 0, tick_ = 0, nextToken_ = 1, evictions_ = 0;
};

/// Default budget for the in-memory caches on a device with `physicalBytes` of RAM: 1/16 of it, at least
/// 256 MB and at most 2 GB (8 GB → 512 MB, 16 GB → 1 GB). 0 (unknown) → 512 MB.
uint64_t defaultCacheBudget(uint64_t physicalBytes);

} // namespace fk
