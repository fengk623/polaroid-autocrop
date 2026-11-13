// FK-014 (U028): upgrades and rollbacks keep the user's projects. Files written by earlier releases open in
// this version (tests/data/compat/<release>/, written by those releases' own code —
// tools/compat/make-fixtures.sh); what a later version adds survives being opened and saved here; and a
// rollback round trip through the newest earlier release keeps exactly what it is documented to keep
// (docs/project-format.md §Compatibility).
#include <gtest/gtest.h>

#include "fk/project.hpp"
#include "fk/version.hpp"
#include "support/test_support.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>

using namespace fk;
using fk::test::TempDir;
using json = nlohmann::ordered_json;

namespace {

const std::filesystem::path kCompat = FK_COMPAT_DIR;

json readJson(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return json::parse(ss.str());
}

Quad rect(double x0, double y0, double x1, double y1) {
    Quad q;
    q.p = {Vec2{x0, y0}, Vec2{x1, y0}, Vec2{x1, y1}, Vec2{x0, y1}};
    return q;
}

/// The content every fixture project carries (tools/compat/fixtures.cpp, "write").
void expectFixtureContent(const Project& p) {
    EXPECT_EQ(p.projectId, "8b0c7c36-5d2e-4f51-9a3e-2f7a1d0c4e11");
    EXPECT_EQ(p.name, "Compatibility fixture");
    EXPECT_EQ(p.createdAt, "2026-10-01T10:00:00Z");
    EXPECT_EQ(p.settings.expectedCount, 2);
    EXPECT_DOUBLE_EQ(p.settings.safetyMarginPx, 1.5);
    EXPECT_EQ(p.settings.exportFolder, "/Users/example/Exports");
    EXPECT_EQ(p.settings.exportBits, 16);
    EXPECT_EQ(p.settings.exportCompression, "zip");
    ASSERT_EQ(p.scans.size(), 1u);
    const ScanRecord& s = p.scans[0];
    EXPECT_EQ(s.scanId, "scan-0001");
    EXPECT_EQ(s.relPath, "../../Scans/roll-01.tif");
    EXPECT_EQ(s.fingerprint.size, 123456789u);
    EXPECT_EQ(s.fingerprint.quickSha256, std::string(64, 'a'));
    EXPECT_EQ(s.info.width, 6000);
    EXPECT_EQ(s.info.height, 4000);
    EXPECT_EQ(s.info.bitsPerSample, 16);
    EXPECT_DOUBLE_EQ(s.info.dpiX, 600);
    EXPECT_EQ(s.info.iccDescription, "Adobe RGB (1998)");
    EXPECT_EQ(s.expectedCount, 2);
    EXPECT_EQ(s.proxyScale, 4);
    EXPECT_EQ(s.warnings, std::vector<std::string>{"count_mismatch"});
    ASSERT_EQ(s.items.size(), 2u);
    const ItemRecord& a = s.items[0];
    EXPECT_EQ(a.itemId, "item-0001");
    EXPECT_EQ(a.label, "Beach");
    EXPECT_EQ(a.filmType, "instax_mini");
    EXPECT_EQ(a.outer, Shape::fromQuad(rect(100, 120, 1300, 2020)));
    EXPECT_DOUBLE_EQ(a.cornerRadiusPx, 12);
    EXPECT_EQ(a.outerSource, BoundarySource::manual);
    EXPECT_EQ(a.inner, Shape::fromQuad(rect(180, 260, 1220, 1660)));
    EXPECT_EQ(a.innerSource, BoundarySource::automatic);
    EXPECT_EQ(a.upright, 1);
    EXPECT_TRUE(a.reviewed);
    ASSERT_EQ(a.edits.size(), 1u);
    EXPECT_EQ(a.edits[0].editVersionId, "ev-0001");
    EXPECT_EQ(a.edits[0].path, "/Users/example/Exports/roll-01_01_v1.tif");
    EXPECT_EQ(a.edits[0].relinkedPath, "/Users/example/Graded/roll-01_01_v1.tif");
    EXPECT_EQ(a.edits[0].matchedBy, "xmp");
    ASSERT_EQ(a.composite.strokes.size(), 1u);
    EXPECT_DOUBLE_EQ(a.composite.strokes[0].radiusPx, 15);
    EXPECT_FALSE(a.composite.strokes[0].add);
    EXPECT_EQ(a.composite.lastOutput, "/Users/example/Exports/roll-01_01_final.tif");
    EXPECT_EQ(a.finalExport, "/Users/example/Exports/roll-01_01_final.tif");
    const ItemRecord& b = s.items[1];
    EXPECT_EQ(b.label, "Round");
    EXPECT_EQ(b.inner, Shape::fromEllipse(Ellipse{Vec2{2300, 1000}, 600, 590, 0.0}));
}

} // namespace

TEST(Compat, OpensProjectsFromEarlierReleases) {
    for (const char* release : {"v0.5.0", "v0.6.0-alpha.1"}) {
        SCOPED_TRACE(release);
        OpenMode mode = OpenMode::read_only;
        auto p = loadProject(kCompat / release / "project.fkproj", &mode);
        ASSERT_TRUE(p.ok()) << p.error().message << " " << p.error().detail;
        EXPECT_EQ(mode, OpenMode::read_write);
        EXPECT_EQ(p->savedBy, std::string(release).substr(1));
        expectFixtureContent(*p);
        // fields added later start from what those releases did: a manual count, no rotation, the project's
        // mask (UX-08: prints from before 0.7.0-alpha.5 keep compositing with the project's output settings)
        EXPECT_FALSE(p->scans[0].autoCount);
        EXPECT_EQ(p->scans[0].rotation, 0);
        EXPECT_FALSE(p->scans[0].items[0].composite.maskSet);
        EXPECT_FALSE(p->scans[0].items[0].edits[0].relinkTransform.set);
        // nothing in these files is unknown to this version, and the per-print mask placeholders of those
        // releases are not carried along
        EXPECT_TRUE(p->settings.unknownJson.empty()) << p->settings.unknownJson;
        EXPECT_TRUE(p->scans[0].unknownJson.empty()) << p->scans[0].unknownJson;
        for (const auto& it : p->scans[0].items)
            EXPECT_TRUE(it.unknownJson.empty()) << it.unknownJson;
        EXPECT_TRUE(p->unknownJson.empty()) << p->unknownJson;
        // saved here, the project keeps its content and opens again
        TempDir dir;
        ASSERT_TRUE(saveProject(*p, dir / "migrated.fkproj").ok());
        auto q = loadProject(dir / "migrated.fkproj");
        ASSERT_TRUE(q.ok()) << q.error().message;
        EXPECT_EQ(q->savedBy, productVersion());
        Project r = *q;
        r.scans[0].relPath = p->scans[0].relPath; // relative to the folder it is saved in
        expectFixtureContent(r);
        EXPECT_FALSE(
            readJson(dir / "migrated.fkproj")["scans"][0]["items"][0]["composite"].contains("maskMode"));
    }
}

TEST(Compat, ExportRecordsFromEarlierReleasesStillRelink) {
    for (const char* release : {"v0.5.0", "v0.6.0-alpha.1"}) {
        SCOPED_TRACE(release);
        auto sc = loadSidecar(kCompat / release / "export_01.fk.json");
        ASSERT_TRUE(sc.ok()) << sc.error().message;
        EXPECT_EQ(sc->projectId, "8b0c7c36-5d2e-4f51-9a3e-2f7a1d0c4e11");
        EXPECT_EQ(sc->scanId, "scan-0001");
        EXPECT_EQ(sc->itemId, "item-0001");
        EXPECT_EQ(sc->editVersionId, "ev-0001");
        EXPECT_EQ(sc->exportSize, (SizeI{1040, 1400}));
        EXPECT_EQ(sc->geometry.outSize, (SizeI{1040, 1400}));
        EXPECT_EQ(sc->geometry.sourceRect.x, 180);
        EXPECT_EQ(sc->bits, 16);
        EXPECT_EQ(sc->fileName, "roll-01_01_v1.tif");
        EXPECT_EQ(sc->origin, "export");
        ASSERT_TRUE(sc->signature.valid);
        EXPECT_NEAR(sc->signature.v[1], 0.37f, 0.005f);
        EXPECT_TRUE(sc->unknownJson.empty()) << sc->unknownJson;
        // the record points at its print in the project of the same release
        auto p = loadProject(kCompat / release / "project.fkproj");
        ASSERT_TRUE(p.ok());
        ScanRecord* scan = nullptr;
        ASSERT_NE(findItem(*p, sc->itemId, &scan), nullptr);
        EXPECT_EQ(scan->scanId, sc->scanId);
    }
}

TEST(Compat, TemplatesFromEarlierReleasesOpen) {
    for (const char* release : {"v0.5.0", "v0.6.0-alpha.1"}) {
        SCOPED_TRACE(release);
        OpenMode mode = OpenMode::read_only;
        auto t = loadTemplate(kCompat / release / "jig.fktemplate", &mode);
        ASSERT_TRUE(t.ok()) << t.error().message;
        EXPECT_EQ(mode, OpenMode::read_write);
        EXPECT_EQ(t->name, "Two-up jig");
        EXPECT_DOUBLE_EQ(t->createdDpi, 600);
        ASSERT_EQ(t->slots.size(), 2u);
        EXPECT_EQ(t->slots[1].inner.kind, ShapeKind::ellipse);
        EXPECT_DOUBLE_EQ(t->slots[1].inner.ellipse.rx, 0.1);
    }
}

// A project written by this version, opened and saved by 0.6.0-alpha.1 after a rollback (the file that
// release wrote), then opened here again: the top-level additions survive (export records, ICC profiles —
// that release keeps unknown top-level keys); the fields added inside settings, scans and prints are lost
// and read back as the defaults below. docs/project-format.md §Compatibility states the same list.
TEST(Compat, WhatARollbackToTheLastReleaseKeeps) {
    const auto dir = kCompat / "rollback-v0.6.0-alpha.1";
    auto before = loadProject(dir / "before.fkproj");
    auto after = loadProject(dir / "after.fkproj");
    ASSERT_TRUE(before.ok() && after.ok());
    EXPECT_EQ(after->savedBy, "0.6.0-alpha.1");
    EXPECT_EQ(compareVersions(before->savedBy, after->savedBy), 1);
    expectFixtureContent(*before);
    expectFixtureContent(*after);
    // kept
    ASSERT_EQ(after->exportRecords.size(), 1u);
    EXPECT_EQ(after->exportRecords[0].editVersionId, before->exportRecords[0].editVersionId);
    EXPECT_EQ(after->exportRecords[0].fileSha256, before->exportRecords[0].fileSha256);
    EXPECT_EQ(after->iccProfiles, before->iccProfiles);
    ASSERT_FALSE(before->iccProfiles.empty());
    // lost (what this version wrote and 0.6.0-alpha.1 did not know)
    EXPECT_FALSE(before->settings.autoCount);
    EXPECT_TRUE(after->settings.autoCount); // absent = the default
    EXPECT_FALSE(before->settings.outputJson.empty());
    EXPECT_TRUE(after->settings.outputJson.empty());
    EXPECT_TRUE(before->scans[0].autoCount);
    EXPECT_FALSE(after->scans[0].autoCount);
    EXPECT_EQ(before->scans[0].rotation, 3);
    EXPECT_EQ(after->scans[0].rotation, 0);
    EXPECT_TRUE(before->scans[0].items[0].composite.maskSet);
    EXPECT_FALSE(after->scans[0].items[0].composite.maskSet);
    EXPECT_TRUE(before->scans[0].items[0].edits[0].relinkTransform.set);
    EXPECT_FALSE(after->scans[0].items[0].edits[0].relinkTransform.set);
}

// FK-014: what a later version adds is kept here, at every level of the project, so that a rollback to this
// version and a later upgrade lose nothing.
TEST(Compat, KeysOfLaterVersionsSurviveOpeningAndSaving) {
    json j = readJson(kCompat / "rollback-v0.6.0-alpha.1" / "before.fkproj");
    j["futureTop"] = {{"a", 1}};
    j["settings"]["futureSetting"] = "x";
    j["settings"]["output"]["futureOutput"] = 2;
    j["scans"][0]["futureScan"] = json::array({1, 2, 3});
    j["scans"][0]["detection"]["futureDetection"] = true;
    j["scans"][0]["view"]["futureView"] = 0.5;
    auto& item = j["scans"][0]["items"][0];
    item["futureItem"] = {{"nested", {{"deep", "kept"}}}};
    item["outer"]["futureOuter"] = 7;
    item["composite"]["futureComposite"] = "y";
    item["composite"]["mask"]["futureMask"] = 1;
    j["exportRecords"][0]["futureRecord"] = "z";
    j["exportRecords"][0]["geometry"]["futureGeometry"] = 3;

    TempDir dir;
    {
        std::ofstream o(dir / "later.fkproj", std::ios::binary);
        o << j.dump(2);
    }
    auto p = loadProject(dir / "later.fkproj");
    ASSERT_TRUE(p.ok()) << p.error().message;
    EXPECT_FALSE(p->scans[0].items[0].unknownJson.empty());
    // this version edits what it knows; its own value wins over nothing of the unknown part
    p->scans[0].items[0].label = "Edited here";
    ASSERT_TRUE(saveProject(*p, dir / "later.fkproj").ok());
    const json k = readJson(dir / "later.fkproj");
    EXPECT_EQ(k["futureTop"], j["futureTop"]);
    EXPECT_EQ(k["settings"]["futureSetting"], "x");
    EXPECT_EQ(k["settings"]["output"]["futureOutput"], 2);
    EXPECT_EQ(k["scans"][0]["futureScan"], json::array({1, 2, 3}));
    EXPECT_EQ(k["scans"][0]["detection"]["futureDetection"], true);
    EXPECT_EQ(k["scans"][0]["view"]["futureView"], 0.5);
    const auto& ki = k["scans"][0]["items"][0];
    EXPECT_EQ(ki["futureItem"]["nested"]["deep"], "kept");
    EXPECT_EQ(ki["outer"]["futureOuter"], 7);
    EXPECT_EQ(ki["composite"]["futureComposite"], "y");
    EXPECT_EQ(ki["composite"]["mask"]["futureMask"], 1);
    EXPECT_EQ(ki["label"], "Edited here");
    EXPECT_EQ(k["exportRecords"][0]["futureRecord"], "z");
    EXPECT_EQ(k["exportRecords"][0]["geometry"]["futureGeometry"], 3);
    // the second print had nothing unknown and gets nothing
    EXPECT_FALSE(k["scans"][0]["items"][1].contains("futureItem"));
    // and what this version knows reads back the same
    auto q = loadProject(dir / "later.fkproj");
    ASSERT_TRUE(q.ok());
    EXPECT_EQ(q->scans[0].items[0].label, "Edited here");
    q->scans[0].items[0].label = "Beach";            // the one edit made here
    q->scans[0].relPath = "../../Scans/roll-01.tif"; // relative to the folder it is saved in
    expectFixtureContent(*q);
    EXPECT_EQ(q->scans[0].rotation, 3);
    EXPECT_TRUE(q->scans[0].items[0].composite.maskSet);
}

TEST(Compat, ProjectRecordsTheVersionThatSavedIt) {
    TempDir dir;
    Project p = newProject("x");
    ASSERT_TRUE(saveProject(p, dir / "x.fkproj").ok());
    EXPECT_EQ(readJson(dir / "x.fkproj")["app"]["version"], productVersion());
    auto q = loadProject(dir / "x.fkproj");
    ASSERT_TRUE(q.ok());
    EXPECT_EQ(q->savedBy, productVersion());
    EXPECT_EQ(compareVersions(q->savedBy, productVersion()), 0);
}
