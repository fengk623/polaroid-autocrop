// Known issues confirmed in Stage A (docs/v1/TRACEABILITY.md). Each test states the behaviour v1.0 requires
// and FAILS on the current code; CMake registers it with WILL_FAIL, so CI stays green while the defect exists
// and turns red as soon as it stops reproducing. The unit that fixes a defect moves its test into the regular
// suite.
#include <gtest/gtest.h>

#include "fk/metadata.hpp"
#include "fk/project.hpp"
#include "fk/util.hpp"
#include "support/test_support.hpp"

#include <fstream>
#include <string>
#include <vector>

using namespace fk;
using fk::test::TempDir;

namespace {
void writeBytes(const std::filesystem::path& p, size_t n, uint8_t seed) {
    std::vector<char> b(n);
    for (size_t i = 0; i < n; ++i)
        b[i] = static_cast<char>((i * 131u + seed) & 0xff);
    std::ofstream(p, std::ios::binary).write(b.data(), static_cast<std::streamsize>(b.size()));
}
} // namespace

// FK-004: "remove identity" must not carry the creator out through our XMP packet.
TEST(KnownIssue, FK004_RemoveIdentityDropsCreatorFromSourceXmp) {
    MetadataSet m;
    m.artist = "Ann Example";
    m.sourceXmp = "<dc:creator><rdf:Seq><rdf:li>Ann Example</rdf:li></rdf:Seq></dc:creator>";
    m.fields = {{"Artist", "Ann Example", PrivacyClass::identity}};
    const MetadataSet filtered = applyPrivacy(m, PrivacyPreset::remove_identity);
    const std::string xmp = buildXmp(XmpIds{}, &filtered);
    EXPECT_EQ(xmp.find("Ann Example"), std::string::npos) << xmp;
}

// FK-007: a scan replaced at the same path (same size, different content) must not be trusted as the
// original.
TEST(KnownIssue, FK007_ReplacedScanAtSamePathIsNotTrusted) {
    TempDir t;
    const auto scan = t / "scan.tif";
    writeBytes(scan, 3u << 20, 1);
    auto fp = fingerprintFile(scan);
    ASSERT_TRUE(fp);
    Project p = newProject("t");
    ScanRecord s;
    s.scanId = newUuid();
    s.path = pathToUtf8(scan);
    s.fingerprint = *fp;
    p.scans.push_back(s);
    writeBytes(scan, 3u << 20, 2); // replaced: same name and size, other content
    resolveScanPaths(p, t.path());
    EXPECT_TRUE(p.scans[0].missing) << "a different file at the recorded path was accepted as the scan";
}

// FK-008: a damaged project whose backup is intact must say so (explicit recovery), not only "damaged".
TEST(KnownIssue, FK008_DamagedProjectOffersItsBackup) {
    TempDir t;
    const auto file = t / "p.fkproj";
    Project p = newProject("t");
    ASSERT_TRUE(saveProject(p, file));
    ASSERT_TRUE(saveProject(p, file)); // second save keeps p.fkproj.bak
    std::ofstream(file, std::ios::binary | std::ios::trunc) << "{ damaged";
    auto loaded = loadProject(file);
    ASSERT_FALSE(loaded);
    const std::string all = loaded.error().message + " " + loaded.error().detail;
    EXPECT_NE(all.find(".bak"), std::string::npos) << all;
}
