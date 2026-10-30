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
