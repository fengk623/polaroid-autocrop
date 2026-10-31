// RFC-0015 / ADR-0007 accounting tests; RFC-0014 in-memory cache budget (FK-005).
#include <gtest/gtest.h>

#include "fk/cache.hpp"
#include "fk/image.hpp"
#include "fk/image_io.hpp"
#include "fk/memory.hpp"
#include "support/test_support.hpp"

#include <tiffio.h>

using namespace fk;

TEST(Memory, AccountsAndReleases) {
    auto& acc = BufferAccounting::instance();
    const uint64_t before = acc.current();
    {
        auto b = ImageBuffer::create(1000, 1000, 3, 16);
        ASSERT_TRUE(b.ok());
        EXPECT_EQ(acc.current() - before, 6'000'000u);
        EXPECT_GE(acc.current(MemCategory::image), 6'000'000u);
    }
    EXPECT_EQ(acc.current(), before);
}

TEST(Memory, LimitTurnsIntoBudgetError) {
    auto& acc = BufferAccounting::instance();
    acc.setLimit(acc.current() + 1'000'000);
    auto b = ImageBuffer::create(1000, 1000, 3, 16);
    acc.setLimit(0);
    ASSERT_FALSE(b.ok());
    EXPECT_EQ(b.error().code, Errc::memory_budget);
}

TEST(Memory, OsStatsAvailable) {
    const auto s = currentMemoryStats();
    EXPECT_GT(s.residentBytes, 0u);
}

TEST(Memory, SystemMemoryReported) {
    const SystemMemory m = systemMemory();
    EXPECT_GT(m.totalBytes, 256ull << 20);
    EXPECT_LE(m.availableBytes, m.totalBytes);
}

// ---- CacheLedger (FK-005) ----

TEST(CacheLedger, DropsLeastRecentlyUsedFirst) {
    CacheLedger l;
    l.setBudget(300);
    l.put("display:a", 100);
    l.put("display:b", 100);
    l.put("display:c", 100);
    l.touch("display:a"); // a is now the most recently used
    EXPECT_TRUE(l.enforce().empty());
    l.put("composite:x", 100);
    EXPECT_EQ(l.enforce(), std::vector<std::string>{"display:b"});
    EXPECT_EQ(l.bytes(), 300u);
    EXPECT_FALSE(l.contains("display:b"));
    EXPECT_EQ(l.evictions(), 1u);
    EXPECT_EQ(l.bytes("display:"), 200u);
    EXPECT_EQ(l.count("composite:"), 1u);
}

TEST(CacheLedger, NeverDropsPinnedOrKeptEntries) {
    CacheLedger l;
    l.setBudget(150);
    l.put("thumb:a", 50, true);
    l.put("display:a", 200);
    l.put("display:b", 100);
    l.setPinned({"display:a"});
    const auto dropped = l.enforce();
    EXPECT_EQ(dropped, std::vector<std::string>{"display:b"});
    // what cannot be dropped may exceed the budget: it stays
    EXPECT_EQ(l.bytes(), 250u);
    EXPECT_EQ(l.fixedBytes(), 250u);
    l.setPinned({});
    EXPECT_EQ(l.enforce(), std::vector<std::string>{"display:a"});
    EXPECT_EQ(l.bytes(), 50u); // the kept thumbnail stays
}

TEST(CacheLedger, ReservationsMakeRoomAndAreReleased) {
    CacheLedger l;
    l.setBudget(400);
    for (const char* k : {"display:a", "display:b", "display:c", "display:d"})
        l.put(k, 100);
    const uint64_t t = l.reserve(250);
    EXPECT_EQ(l.reserved(), 250u);
    const auto dropped = l.enforce(); // 400 − 250 = 150 left for caches: a and b and c go
    EXPECT_EQ(dropped, (std::vector<std::string>{"display:a", "display:b", "display:c"}));
    EXPECT_EQ(l.bytes(), 100u);
    l.release(t);
    EXPECT_EQ(l.reserved(), 0u);
    l.release(t); // twice: harmless
    l.put("display:a", 100);
    EXPECT_TRUE(l.enforce().empty());
    // a reservation larger than the budget leaves only what cannot be dropped
    l.setPinned({"display:a"});
    const uint64_t big = l.reserve(1000);
    EXPECT_EQ(l.enforce(), std::vector<std::string>{"display:d"});
    EXPECT_EQ(l.bytes(), 100u);
    l.release(big);
}

TEST(CacheLedger, ShrinkForMemoryPressureAndUpdateSizes) {
    CacheLedger l;
    l.setBudget(1000);
    l.put("display:a", 300);
    l.put("composite:x", 300);
    l.put("source:a", 300);
    l.put("source:a", 100); // the decoded-strip cache shrank: the entry is updated, not added
    EXPECT_EQ(l.bytes(), 700u);
    l.setPinned({"display:a"});
    const auto dropped = l.shrinkTo(0);
    EXPECT_EQ(dropped, (std::vector<std::string>{"composite:x", "source:a"}));
    EXPECT_EQ(l.bytes(), 300u);
    l.erase("display:a");
    l.erase("missing");
    EXPECT_EQ(l.bytes(), 0u);
}

TEST(CacheLedger, DefaultBudgetFollowsDeviceMemory) {
    constexpr uint64_t GB = 1ull << 30, MB = 1ull << 20;
    EXPECT_EQ(defaultCacheBudget(8 * GB), 512 * MB);
    EXPECT_EQ(defaultCacheBudget(16 * GB), 1024 * MB);
    EXPECT_EQ(defaultCacheBudget(2 * GB), 256 * MB);
    EXPECT_EQ(defaultCacheBudget(128 * GB), 2048 * MB);
    EXPECT_EQ(defaultCacheBudget(0), 512 * MB);
}

TEST(CacheLedger, TiffSourceReportsItsDecodedCache) {
    fk::test::TempDir dir;
    const auto img = fk::test::makePatternImage(256, 256, 3, 16);
    fk::test::TestTiffOptions o;
    o.compression = COMPRESSION_LZW;
    o.layout = fk::test::TiffLayout::strips;
    fk::test::writeTestTiff(dir / "s.tif", img, o);
    auto src = openTiff(dir / "s.tif");
    ASSERT_TRUE(src.ok());
    EXPECT_EQ((*src)->cachedBytes(), 0u);
    std::vector<uint8_t> row(256 * 6 * 64);
    ASSERT_TRUE((*src)->readRows(0, 64, 0, 256, row.data(), 256 * 6).ok());
    const uint64_t first = (*src)->cachedBytes();
    EXPECT_GE(first, 64u * 256 * 6);
    ASSERT_TRUE((*src)->readRows(64, 64, 0, 256, row.data(), 256 * 6).ok());
    const uint64_t both = (*src)->cachedBytes();
    EXPECT_GE(both, 2 * first);
    (*src)->setCacheBudget(0); // keeps only the strips of the last request
    EXPECT_EQ((*src)->cachedBytes(), both - first);
}
