// RFC-0015 / ADR-0007 accounting tests.
#include <gtest/gtest.h>

#include "fk/image.hpp"
#include "fk/memory.hpp"

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
