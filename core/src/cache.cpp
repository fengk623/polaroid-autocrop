#include "fk/cache.hpp"

#include <algorithm>

namespace fk {

void CacheLedger::put(const std::string& key, uint64_t bytes, bool kept) {
    Slot& s = entries_[key];
    total_ = total_ - s.bytes + bytes;
    s.bytes = bytes;
    s.kept = kept;
    s.lastUse = ++tick_;
}

void CacheLedger::touch(const std::string& key) {
    if (auto f = entries_.find(key); f != entries_.end()) f->second.lastUse = ++tick_;
}

void CacheLedger::erase(const std::string& key) {
    if (auto f = entries_.find(key); f != entries_.end()) {
        total_ -= f->second.bytes;
        entries_.erase(f);
    }
}

void CacheLedger::setPinned(std::set<std::string> keys) {
    pinned_ = std::move(keys);
}

uint64_t CacheLedger::reserve(uint64_t bytes) {
    const uint64_t t = nextToken_++;
    reservations_[t] = bytes;
    return t;
}

void CacheLedger::release(uint64_t token) {
    reservations_.erase(token);
}

uint64_t CacheLedger::reserved() const {
    uint64_t r = 0;
    for (const auto& [t, b] : reservations_)
        r += b;
    return r;
}

uint64_t CacheLedger::bytes(const std::string& prefix) const {
    uint64_t b = 0;
    for (const auto& [k, s] : entries_)
        if (k.compare(0, prefix.size(), prefix) == 0) b += s.bytes;
    return b;
}

uint64_t CacheLedger::fixedBytes() const {
    uint64_t b = 0;
    for (const auto& [k, s] : entries_)
        if (!droppable(k, s)) b += s.bytes;
    return b;
}

size_t CacheLedger::count(const std::string& prefix) const {
    size_t n = 0;
    for (const auto& [k, s] : entries_)
        if (k.compare(0, prefix.size(), prefix) == 0) ++n;
    return n;
}

std::vector<CacheLedger::Entry> CacheLedger::entries() const {
    std::vector<Entry> v;
    v.reserve(entries_.size());
    for (const auto& [k, s] : entries_)
        v.push_back({k, s.bytes, s.lastUse, pinned_.count(k) != 0, s.kept});
    return v;
}

std::vector<std::string> CacheLedger::dropUntil(uint64_t target) {
    std::vector<std::pair<uint64_t, std::string>> order; // (lastUse, key) of droppable entries
    for (const auto& [k, s] : entries_)
        if (droppable(k, s)) order.emplace_back(s.lastUse, k);
    std::sort(order.begin(), order.end());
    std::vector<std::string> dropped;
    for (const auto& [use, k] : order) {
        if (total_ <= target) break;
        erase(k);
        dropped.push_back(k);
        ++evictions_;
    }
    return dropped;
}

std::vector<std::string> CacheLedger::enforce() {
    const uint64_t r = reserved();
    return dropUntil(budget_ > r ? budget_ - r : 0);
}

std::vector<std::string> CacheLedger::shrinkTo(uint64_t bytes) {
    return dropUntil(bytes);
}

uint64_t defaultCacheBudget(uint64_t physicalBytes) {
    constexpr uint64_t MB = 1ull << 20;
    if (physicalBytes == 0) return 512 * MB;
    return std::clamp<uint64_t>(physicalBytes / 16, 256 * MB, 2048 * MB);
}

} // namespace fk
