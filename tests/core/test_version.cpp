#include "fk/product_info.hpp"
#include "fk/version.hpp"
#include <gtest/gtest.h>

#include <regex>

TEST(Version, MatchesProductInfo) {
    EXPECT_EQ(fk::productVersion(), fk::product::kVersion);
    EXPECT_EQ(fk::apiVersion(), FK_API_VERSION_MAJOR * 1000 + FK_API_VERSION_MINOR);
    EXPECT_NE(fk::productNameAndVersion().find("FrameKeeper"), std::string::npos);
}

TEST(Version, SemVerWithOptionalPreRelease) {
    const std::regex semver(R"(^\d+\.\d+\.\d+(-[0-9A-Za-z.-]+)?$)");
    EXPECT_TRUE(std::regex_match(fk::productVersion(), semver)) << fk::productVersion();
    EXPECT_EQ(fk::productVersion().rfind(fk::product::kVersionNumeric, 0), 0u);
}

TEST(Version, BuildProvenanceIsPresent) {
    EXPECT_FALSE(fk::buildCommit().empty());
    EXPECT_FALSE(fk::buildId().empty());
    const std::string line = fk::buildInfoLine();
    EXPECT_NE(line.find(fk::productVersion()), std::string::npos);
    EXPECT_NE(line.find(fk::buildCommit()), std::string::npos);
}

// FK-014 (U028): which of two versions is later (a project saved by a later version means a rollback
// happened)
TEST(Version, ComparesBySemVerPrecedence) {
    // SemVer 2.0 §11 example order
    const char* order[] = {"1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-alpha.beta",
                           "1.0.0-beta",  "1.0.0-beta.2",  "1.0.0-beta.11",
                           "1.0.0-rc.1",  "1.0.0",         "1.0.1",
                           "1.1.0",       "2.0.0"};
    const size_t n = sizeof(order) / sizeof(order[0]);
    for (size_t i = 0; i < n; ++i)
        for (size_t j = 0; j < n; ++j)
            EXPECT_EQ(fk::compareVersions(order[i], order[j]), i < j ? -1 : (i > j ? 1 : 0))
                << order[i] << " vs " << order[j];
    EXPECT_EQ(fk::compareVersions("0.7.0-alpha.9", "0.7.0-alpha.15"), -1);
    EXPECT_EQ(fk::compareVersions("0.6.0-alpha.1", "0.5.0"), 1);
    EXPECT_EQ(fk::compareVersions("1.0.0+build.5", "1.0.0"), 0); // build metadata does not count
    EXPECT_EQ(fk::compareVersions(fk::productVersion(), fk::productVersion()), 0);
    // not a version: no order (no notice is shown)
    for (const char* bad : {"", "1.0", "1.0.0-", "1.0.0-a..b", "v1.0.0", "unknown"})
        EXPECT_EQ(fk::compareVersions(bad, "1.0.0"), 0) << bad;
}
