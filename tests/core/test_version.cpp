#include "fk/product_info.hpp"
#include "fk/version.hpp"
#include <gtest/gtest.h>

TEST(Version, MatchesProductInfo) {
    EXPECT_EQ(fk::productVersion(), fk::product::kVersion);
    EXPECT_EQ(fk::apiVersion(), FK_API_VERSION_MAJOR * 1000 + FK_API_VERSION_MINOR);
    EXPECT_NE(fk::productNameAndVersion().find("FrameKeeper"), std::string::npos);
}
