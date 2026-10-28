// RFC-0009 / ADR-0005 / docs/project-format.md: projects, sidecars, relink matching order, templates.
#include <gtest/gtest.h>

#include "fk/project.hpp"
#include "fk/util.hpp"
#include "support/test_support.hpp"

#include <tiffio.h>

#include <algorithm>
#include <fstream>

using namespace fk;
using fk::test::TempDir;

namespace {

Project sampleProject(const std::filesystem::path& scanPath) {
    Project p = newProject("Summer 2024");
    ScanRecord s;
    s.scanId = newUuid();
    s.path = pathToUtf8(scanPath);
    s.relPath = pathToUtf8(scanPath.filename());
    s.info.width = 320;
    s.info.height = 240;
    s.info.channels = 3;
    s.info.bitsPerSample = 16;
    s.expectedCount = 2;
    s.dpiUsed = 600;
    s.warnings = {"dpi_unknown"};
    for (int i = 1; i <= 2; ++i) {
        DetectedItem d;
        d.outer = Shape::fromQuad(rectQuad(10.25 * i, 12.5, 150 + i, 220.75));
        d.inner =
            i == 1 ? Shape::fromQuad(rectQuad(20, 30, 140, 200)) : Shape::fromEllipse({{80, 110}, 40, 40, 0});
        d.filmTypeId = i == 1 ? "instax_mini" : "polaroid_round";
        d.upright = i;
        d.confidence = 0.42;
        d.reasons = {"touching_split"};
        ItemRecord it = itemFromDetection(d, i);
        it.composite.strokes.push_back({{{1, 2}, {3, 4.5}}, 7.5, false});
        s.items.push_back(it);
    }
    p.scans.push_back(s);
    return p;
}

void writeText(const std::filesystem::path& p, const std::string& s) {
    std::ofstream o(p, std::ios::binary);
    o << s;
}

} // namespace

TEST(Project, SaveLoadRoundTripIsLossless) {
    TempDir dir;
    const Project p = sampleProject(dir / "scan.tif");
    const auto file = dir / "a.fkproj";
    ASSERT_TRUE(saveProject(p, file).ok());
    OpenMode mode = OpenMode::read_only;
    auto q = loadProject(file, &mode);
    ASSERT_TRUE(q.ok()) << q.error().message;
    EXPECT_EQ(mode, OpenMode::read_write);
    EXPECT_EQ(projectToJson(*q), projectToJson(p));
    ASSERT_EQ(q->scans.size(), 1u);
    ASSERT_EQ(q->scans[0].items.size(), 2u);
    EXPECT_EQ(q->scans[0].items[1].inner.kind, ShapeKind::ellipse);
    EXPECT_EQ(q->scans[0].items[0].composite.strokes.size(), 1u);
}

TEST(Project, SecondSaveKeepsBackupAndIsAtomic) {
    TempDir dir;
    Project p = sampleProject(dir / "scan.tif");
    const auto file = dir / "a.fkproj";
    ASSERT_TRUE(saveProject(p, file).ok());
    const std::string first = fk::test::sha256OfFile(file);
    p.name = "Renamed";
    ASSERT_TRUE(saveProject(p, file).ok());
    EXPECT_TRUE(std::filesystem::exists(pathFromUtf8(pathToUtf8(file) + ".bak")));
    EXPECT_EQ(fk::test::sha256OfFile(pathFromUtf8(pathToUtf8(file) + ".bak")), first);
    // no temporary files left behind
    for (auto& e : std::filesystem::directory_iterator(dir.path()))
        EXPECT_EQ(e.path().string().find(".tmp"), std::string::npos) << e.path();
}

TEST(Project, WriteFailureLeavesNothingAndReportsError) {
    TempDir dir;
    const Project p = sampleProject(dir / "scan.tif");
    writeText(dir / "notadir", "x");
    const auto bad = dir / "notadir" / "a.fkproj"; // parent is a file
    const Status st = saveProject(p, bad);
    ASSERT_FALSE(st.ok());
    EXPECT_TRUE(st.error().code == Errc::io_write || st.error().code == Errc::io_open)
        << errcName(st.error().code);
}

TEST(Project, UnknownKeysArePreserved) {
    TempDir dir;
    const Project p = sampleProject(dir / "scan.tif");
    std::string js = projectToJson(p);
    const auto pos = js.find('{');
    js.insert(pos + 1, "\"futureFeature\":{\"x\":[1,2,3]},");
    auto q = projectFromJson(js);
    ASSERT_TRUE(q.ok()) << q.error().message;
    std::string again = projectToJson(*q);
    again.erase(std::remove_if(again.begin(), again.end(), [](char c) { return c == ' ' || c == '\n'; }),
                again.end());
    EXPECT_NE(again.find("futureFeature"), std::string::npos);
    EXPECT_NE(again.find("[1,2,3]"), std::string::npos);
}

TEST(Project, NewerSchemaOpensReadOnly) {
    const Project p = newProject("x");
    std::string js = projectToJson(p);
    const auto pos = js.find("fk.project/1");
    ASSERT_NE(pos, std::string::npos);
    js.replace(pos, 12, "fk.project/9");
    OpenMode mode = OpenMode::read_write;
    auto q = projectFromJson(js, &mode);
    ASSERT_TRUE(q.ok()) << q.error().message;
    EXPECT_EQ(mode, OpenMode::read_only);
}

TEST(Project, DamagedFileFailsCleanly) {
    TempDir dir;
    writeText(dir / "bad.fkproj", "{\"schema\":\"fk.project/1\", \"scans\": [ {");
    auto q = loadProject(dir / "bad.fkproj");
    ASSERT_FALSE(q.ok());
    EXPECT_EQ(q.error().code, Errc::parse_error);
    writeText(dir / "other.fkproj", "{\"schema\":\"something.else/1\"}");
    EXPECT_FALSE(loadProject(dir / "other.fkproj").ok());
}

TEST(Project, RelocatesMovedScansByNameAndFingerprint) {
    TempDir dir;
    const ImageBuffer img = fk::test::makePatternImage(64, 48, 3, 8, 1);
    std::filesystem::create_directories(dir / "old");
    std::filesystem::create_directories(dir / "new");
    fk::test::writeTestTiff(dir / "old" / "scan.tif", img);
    Project p = sampleProject(dir / "old" / "scan.tif");
    auto fp = fingerprintFile(dir / "old" / "scan.tif");
    ASSERT_TRUE(fp.ok());
    p.scans[0].fingerprint = *fp;
    // moved and renamed
    std::filesystem::rename(dir / "old" / "scan.tif", dir / "new" / "renamed.tif");
    p.scans[0].relPath = "nowhere/scan.tif";
    resolveScanPaths(p, dir.path());
    EXPECT_TRUE(p.scans[0].missing);
    EXPECT_EQ(relocateScans(p, dir.path(), dir / "new"), 1);
    EXPECT_FALSE(p.scans[0].missing);
    EXPECT_EQ(pathFromUtf8(p.scans[0].path).filename(), "renamed.tif");
}

TEST(Project, FingerprintDetectsContentChange) {
    TempDir dir;
    fk::test::writeTestTiff(dir / "a.tif", fk::test::makePatternImage(64, 48, 3, 8, 1));
    fk::test::writeTestTiff(dir / "b.tif", fk::test::makePatternImage(64, 48, 3, 8, 2));
    auto a = fingerprintFile(dir / "a.tif", true), b = fingerprintFile(dir / "b.tif", true);
    ASSERT_TRUE(a.ok());
    ASSERT_TRUE(b.ok());
    EXPECT_FALSE(*a == *b);
    EXPECT_EQ(a->fullSha256, fk::test::sha256OfFile(dir / "a.tif"));
}

// ---- sidecars and the matching order ---------------------------------------------------------------

struct RelinkFixture {
    TempDir dir;
    Project p;
    ExportReport rep;
    std::filesystem::path editPath;
};

std::unique_ptr<RelinkFixture> exportedEdit() {
    auto f = std::make_unique<RelinkFixture>();
    const ImageBuffer img = fk::test::makePatternImage(320, 240, 3, 16, 7);
    fk::test::TestTiffOptions o;
    o.dpi = 600;
    fk::test::writeTestTiff(f->dir / "roll1.tif", img, o);
    f->p = sampleProject(f->dir / "roll1.tif");
    ScanRecord& s = f->p.scans[0];
    ItemRecord& it = s.items[0];
    auto src = openTiff(f->dir / "roll1.tif");
    EXPECT_TRUE(src.ok());
    ExportRequest r;
    r.region = it.inner;
    r.frame = it.outer.quad;
    r.output = f->dir / "roll1_P01_EDIT_v001.tif";
    r.write.format = FileFormat::tiff;
    EditVersion ev;
    ev.editVersionId = newUuid();
    ev.version = 1;
    r.ids =
        XmpIds{f->p.projectId, s.scanId, it.itemId, ev.editVersionId, newUuid(), 1, it.label, "image_area"};
    auto rep = exportRegion(**src, r, {}, {});
    EXPECT_TRUE(rep.ok());
    f->rep = *rep;
    f->editPath = rep->output;
    ev.path = pathToUtf8(rep->output);
    ev.sidecar = sidecarPathFor(ev.path);
    it.edits.push_back(ev);
    const Sidecar sc = makeSidecar(f->p, s, it, ev, *rep);
    EXPECT_TRUE(saveSidecar(sc, pathFromUtf8(ev.sidecar)).ok());
    return f;
}

/// Writes the same pixels as a PNG without any of our metadata.
void writePlainPng(const ImageBuffer& img, const std::filesystem::path& out) {
    WriteOptions w;
    w.format = FileFormat::png;
    w.bits = img.bits();
    auto sink = createSink(out, w);
    ASSERT_TRUE(sink.ok());
    ASSERT_TRUE((*sink)->begin(img.size(), img.channels(), img.bits()).ok());
    ASSERT_TRUE((*sink)->writeRows(img.data(), img.height(), img.rowBytes()).ok());
    ASSERT_TRUE((*sink)->finish().ok());
}

TEST(Relink, SidecarRoundTrip) {
    auto f = exportedEdit();
    auto sc = loadSidecar(pathFromUtf8(sidecarPathFor(pathToUtf8(f->editPath))));
    ASSERT_TRUE(sc.ok()) << sc.error().message;
    EXPECT_EQ(sc->itemId, f->p.scans[0].items[0].itemId);
    EXPECT_EQ(sc->exportSize, f->rep.size);
    EXPECT_TRUE(sc->signature.valid);
    EXPECT_NEAR(compareSignatures(sc->signature, f->rep.signature), 1.0, 1e-6);
}

TEST(Relink, MatchingOrderXmpThenSidecarThenFileName) {
    auto f = exportedEdit();
    const std::string itemId = f->p.scans[0].items[0].itemId;
    // 1. XMP embedded by our export
    auto m1 = findMatch(f->editPath, f->p);
    EXPECT_EQ(m1.source, MatchSource::xmp);
    EXPECT_EQ(m1.itemId, itemId);
    ASSERT_TRUE(m1.sidecar.has_value());

    auto pixels = readImageFile(f->editPath);
    ASSERT_TRUE(pixels.ok());
    // 2. editor stripped the XMP but kept the name next to the sidecar ("-Edit" suffix)
    const auto noXmp = f->dir / "roll1_P01_EDIT_v001-Edit.png";
    writePlainPng(*pixels, noXmp);
    auto m2 = findMatch(noXmp, f->p);
    EXPECT_EQ(m2.source, MatchSource::sidecar);
    EXPECT_EQ(m2.itemId, itemId);

    // 3. elsewhere, only the file name pattern is left
    std::filesystem::create_directories(f->dir / "elsewhere");
    const auto byName = f->dir / "elsewhere" / "roll1_P01_EDIT_v001.png";
    writePlainPng(*pixels, byName);
    auto m3 = findMatch(byName, f->p);
    EXPECT_EQ(m3.source, MatchSource::file_name);
    EXPECT_EQ(m3.itemId, itemId);

    // nothing
    const auto unknown = f->dir / "elsewhere" / "holiday.png";
    writePlainPng(*pixels, unknown);
    EXPECT_EQ(findMatch(unknown, f->p).source, MatchSource::none);
}

TEST(Relink, CheckEditedFlagsGeometryAndContent) {
    auto f = exportedEdit();
    auto sc = loadSidecar(pathFromUtf8(sidecarPathFor(pathToUtf8(f->editPath))));
    ASSERT_TRUE(sc.ok());
    ImageInfo info;
    auto same = readImageFile(f->editPath, &info);
    ASSERT_TRUE(same.ok());
    auto ok = checkEdited(*sc, *same, info);
    EXPECT_TRUE(ok.sizeMatches);
    EXPECT_TRUE(ok.signaturePass);
    EXPECT_TRUE(ok.actionsNeeded.empty());

    const ImageBuffer other = fk::test::makePatternImage(same->width(), same->height(), 3, 8, 99);
    ImageInfo oi;
    oi.icc = info.icc;
    auto bad = checkEdited(*sc, other, oi);
    EXPECT_FALSE(bad.signaturePass);
    EXPECT_TRUE(bad.depthLower);
    EXPECT_NE(std::find(bad.actionsNeeded.begin(), bad.actionsNeeded.end(), "confirm_content"),
              bad.actionsNeeded.end());
    EXPECT_NE(std::find(bad.actionsNeeded.begin(), bad.actionsNeeded.end(), "accept_lower_depth"),
              bad.actionsNeeded.end());
}

// ---- templates -------------------------------------------------------------------------------------

TEST(Template, SaveLoadRoundTrip) {
    TempDir dir;
    Template t;
    t.name = "4-up jig";
    t.createdDpi = 800;
    t.scanSize = {6800, 9300};
    for (int i = 1; i <= 4; ++i) {
        TemplateSlot s;
        s.index = i;
        s.filmType = "instax_mini";
        for (int k = 0; k < 4; ++k)
            s.outer[k] = {0.1 * i + 0.01 * k, 0.2 + 0.01 * k};
        s.inner = Shape::fromQuad(rectQuad(0.1, 0.1, 0.2, 0.3));
        t.slots.push_back(s);
    }
    ASSERT_TRUE(saveTemplate(t, dir / "jig.fktemplate").ok());
    auto q = loadTemplate(dir / "jig.fktemplate");
    ASSERT_TRUE(q.ok()) << q.error().message;
    EXPECT_EQ(q->name, t.name);
    ASSERT_EQ(q->slots.size(), 4u);
    EXPECT_DOUBLE_EQ(q->slots[2].outer[1].x, t.slots[2].outer[1].x);
    EXPECT_EQ(q->scanSize, t.scanSize);
}
