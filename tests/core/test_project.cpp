// RFC-0009 / ADR-0005 / docs/project-format.md: projects, sidecars, relink matching order, templates.
#include <gtest/gtest.h>

#include "fk/project.hpp"
#include "fk/util.hpp"
#include "support/test_support.hpp"

#include <tiffio.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <regex>

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

std::unique_ptr<RelinkFixture> exportedEditOf(const ImageBuffer& img) {
    auto f = std::make_unique<RelinkFixture>();
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

std::unique_ptr<RelinkFixture> exportedEdit() {
    return exportedEditOf(fk::test::makePatternImage(320, 240, 3, 16, 7));
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

// ---- export records in the project (UX-07, U003) ----------------------------------------------------

std::string slurp(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

/// The same export as exportedEdit(), recorded only in the project: no sidecar file anywhere.
std::unique_ptr<RelinkFixture> exportedEditRecordOnly() {
    auto f = exportedEdit();
    ItemRecord& it = f->p.scans[0].items[0];
    EditVersion& ev = it.edits.back();
    f->p.exportRecords.push_back(makeSidecar(f->p, f->p.scans[0], it, ev, f->rep));
    std::filesystem::remove(pathFromUtf8(ev.sidecar));
    ev.sidecar.clear();
    return f;
}

TEST(ExportRecords, SaveLoadRoundTrip) {
    auto f = exportedEditRecordOnly();
    const auto file = f->dir / "p.fkproj";
    ASSERT_TRUE(saveProject(f->p, file).ok());
    auto back = loadProject(file);
    ASSERT_TRUE(back.ok()) << back.error().message;
    ASSERT_EQ(back->exportRecords.size(), 1u);
    const Sidecar& a = f->p.exportRecords[0];
    const Sidecar& b = back->exportRecords[0];
    EXPECT_EQ(b.editVersionId, a.editVersionId);
    EXPECT_EQ(b.itemId, a.itemId);
    EXPECT_EQ(b.exportSize, a.exportSize);
    EXPECT_EQ(b.geometry.sourceRect.x, a.geometry.sourceRect.x);
    EXPECT_EQ(b.geometry.sourceRect.w, a.geometry.sourceRect.w);
    EXPECT_EQ(b.fileSha256, a.fileSha256);
    EXPECT_NEAR(compareSignatures(b.signature, a.signature), 1.0, 1e-6);
    // the project file is the only place the record lives
    EXPECT_FALSE(std::filesystem::exists(f->dir / "roll1_P01_EDIT_v001.fk.json"));
}

TEST(ExportRecords, RelinkUsesTheProjectRecordWithoutASidecar) {
    auto f = exportedEditRecordOnly();
    // by embedded XMP
    auto m1 = findMatch(f->editPath, f->p);
    EXPECT_EQ(m1.source, MatchSource::xmp);
    ASSERT_TRUE(m1.sidecar.has_value());
    EXPECT_EQ(m1.sidecar->exportSize, f->rep.size);
    // by file name, elsewhere, without our metadata
    auto pixels = readImageFile(f->editPath);
    ASSERT_TRUE(pixels.ok());
    std::filesystem::create_directories(f->dir / "graded");
    const auto byName = f->dir / "graded" / "roll1_P01_EDIT_v001.png";
    writePlainPng(*pixels, byName);
    auto m2 = findMatch(byName, f->p);
    EXPECT_EQ(m2.source, MatchSource::file_name);
    ASSERT_TRUE(m2.sidecar.has_value()) << "geometry must come from the project record";
    EXPECT_EQ(m2.sidecar->editVersionId, f->p.exportRecords[0].editVersionId);
}

TEST(ExportRecords, RecordsOfANewerSchemaAreKeptVerbatim) {
    TempDir t;
    Project p = newProject("t");
    const auto file = t / "p.fkproj";
    ASSERT_TRUE(saveProject(p, file).ok());
    std::string text = slurp(file);
    const auto pos = text.rfind('}');
    text.insert(pos, R"(, "exportRecords": [{"schema": "fk.sidecar/99", "future": true}])");
    std::ofstream(file, std::ios::binary | std::ios::trunc) << text;
    auto loaded = loadProject(file);
    ASSERT_TRUE(loaded.ok()) << loaded.error().message;
    EXPECT_TRUE(loaded->exportRecords.empty());
    ASSERT_EQ(loaded->unparsedExportRecords.size(), 1u);
    ASSERT_TRUE(saveProject(*loaded, file).ok());
    EXPECT_NE(slurp(file).find("fk.sidecar/99"), std::string::npos);
}

TEST(ExportRecords, OlderProjectsImportTheirSidecarsOnOpen) {
    auto f = exportedEdit(); // pre-U003 style: sidecar file only, no record in the project
    ASSERT_TRUE(f->p.exportRecords.empty());
    const auto file = f->dir / "old.fkproj";
    ASSERT_TRUE(saveProject(f->p, file).ok());
    auto back = loadProject(file);
    ASSERT_TRUE(back.ok());
    ASSERT_EQ(back->exportRecords.size(), 1u);
    EXPECT_EQ(back->exportRecords[0].editVersionId, f->p.scans[0].items[0].edits[0].editVersionId);
    // after saving again the sidecar file is no longer needed
    ASSERT_TRUE(saveProject(*back, file).ok());
    std::filesystem::remove(pathFromUtf8(f->p.scans[0].items[0].edits[0].sidecar));
    auto again = loadProject(file);
    ASSERT_TRUE(again.ok());
    auto m = findMatch(f->editPath, *again);
    ASSERT_TRUE(m.sidecar.has_value());
}

// ---- relink v2: evidence model (RFC-0006 §Relink v2, UX-07, U004)
// ----------------------------------------------

/// Smooth picture (signatures of the same content correlate; different `seed`s do not).
ImageBuffer makeSmoothImage(int64_t w, int64_t h, double seed) {
    auto r = ImageBuffer::create(w, h, 3, 16);
    EXPECT_TRUE(r.ok());
    ImageBuffer b = std::move(r.value());
    for (int64_t y = 0; y < h; ++y) {
        auto* row = reinterpret_cast<uint16_t*>(b.row(y));
        for (int64_t x = 0; x < w; ++x)
            for (int c = 0; c < 3; ++c) {
                const double v = 0.5 + 0.2 * std::sin(x * 0.047 * seed + c) +
                                 0.15 * std::cos(y * 0.031 * (seed + 1)) +
                                 0.1 * std::sin((x - y) * 0.023 * seed + 2.0 * seed);
                row[x * 3 + c] = static_cast<uint16_t>(std::clamp(v, 0.0, 1.0) * 65535.0);
            }
    }
    return b;
}

/// One print exported once, the record only in the project (no sidecar file).
std::unique_ptr<RelinkFixture> smoothExport() {
    auto f = exportedEditOf(makeSmoothImage(320, 240, 1.0));
    ItemRecord& it = f->p.scans[0].items[0];
    EditVersion& ev = it.edits.back();
    f->p.exportRecords.push_back(makeSidecar(f->p, f->p.scans[0], it, ev, f->rep));
    std::filesystem::remove(pathFromUtf8(ev.sidecar));
    ev.sidecar.clear();
    return f;
}

/// The exported pixels saved under `name` as a PNG without any of our metadata.
std::filesystem::path plainCopy(RelinkFixture& f, const std::string& name) {
    auto pixels = readImageFile(f.editPath);
    EXPECT_TRUE(pixels.ok());
    std::filesystem::create_directories(f.dir / "graded");
    const auto out = f.dir / "graded" / name;
    writePlainPng(*pixels, out);
    return out;
}

RelinkInput describe(const std::filesystem::path& file) {
    auto pixels = readImageFile(file);
    EXPECT_TRUE(pixels.ok());
    return describeGradedFile(file, *pixels);
}

bool hasEvidence(const RelinkOption& o, EvidenceKind k, bool supports) {
    return std::any_of(o.evidence.begin(), o.evidence.end(),
                       [&](const Evidence& e) { return e.kind == k && e.supports == supports; });
}

TEST(RelinkV2, UnchangedExportIsDecidedByItsHash) {
    auto f = smoothExport();
    const RelinkInput in = describe(f->editPath);
    EXPECT_TRUE(in.hasIds);
    const auto a = assessRelink(in, f->p);
    EXPECT_EQ(a.decision, RelinkDecision::unique);
    ASSERT_FALSE(a.options.empty());
    EXPECT_TRUE(a.options[0].hashMatch);
    EXPECT_EQ(a.options[0].itemId, f->p.scans[0].items[0].itemId);
    EXPECT_TRUE(hasEvidence(a.options[0], EvidenceKind::file_hash, true));
    EXPECT_TRUE(hasEvidence(a.options[0], EvidenceKind::embedded_ids, true));
    EXPECT_TRUE(a.options[0].contentPass);
}

TEST(RelinkV2, RenamedFileWithoutMetadataNeedsTheTarget) {
    // UX-07 (test doc fig. 15): a graded file renamed by the editor, no XMP, no sidecar
    auto f = smoothExport();
    const RelinkInput in = describe(plainCopy(*f, "final look.png"));
    EXPECT_FALSE(in.hasIds);
    const auto a = assessRelink(in, f->p);
    // the picture matches, but similarity alone never decides: shown as a candidate, not pre-selected
    EXPECT_EQ(a.decision, RelinkDecision::ambiguous);
    ASSERT_EQ(a.options.size(), 1u);
    EXPECT_TRUE(a.options[0].contentPass);
    EXPECT_FALSE(a.options[0].hashMatch);
    // target first: the user picks the print, the content confirms it
    const auto t = assessRelink(in, f->p, f->p.scans[0].items[0].itemId);
    EXPECT_EQ(t.decision, RelinkDecision::unique);
    ASSERT_FALSE(t.options.empty());
    EXPECT_FALSE(t.options[0].newAlignment);
    EXPECT_EQ(t.options[0].editVersionId, f->p.exportRecords[0].editVersionId);
    EXPECT_TRUE(hasEvidence(t.options[0], EvidenceKind::user_target, true));
}

TEST(RelinkV2, EditorSuffixUnderscoreNumberIsStripped) {
    // UX-07 (test doc fig. 14): "<name>_1" written by an editor
    const auto e = parseEditFileName("roll1_P01_EDIT_v001_1");
    ASSERT_TRUE(e.has_value());
    EXPECT_EQ(e->scanStem, "roll1");
    EXPECT_EQ(e->itemIndex, 1);
    EXPECT_EQ(e->version, 1);
    auto f = smoothExport();
    const auto a = assessRelink(describe(plainCopy(*f, "roll1_P01_EDIT_v001_1.png")), f->p);
    EXPECT_EQ(a.decision, RelinkDecision::unique);
    ASSERT_FALSE(a.options.empty());
    EXPECT_TRUE(hasEvidence(a.options[0], EvidenceKind::file_name, true));
    EXPECT_TRUE(a.options[0].contentPass);
}

TEST(RelinkV2, EmbeddedIdsAndContentThatDisagreeAreShownAsAConflict) {
    auto f = smoothExport();
    RelinkInput in = describe(plainCopy(*f, "x.png"));
    // the file claims to be print 2, but its picture is print 1's export
    in.hasIds = true;
    in.ids.itemId = f->p.scans[0].items[1].itemId;
    const auto a = assessRelink(in, f->p);
    EXPECT_EQ(a.decision, RelinkDecision::ambiguous);
    ASSERT_FALSE(a.options.empty());
    EXPECT_TRUE(a.options[0].conflict);
    EXPECT_TRUE(hasEvidence(a.options[0], EvidenceKind::embedded_ids, false));
    EXPECT_TRUE(hasEvidence(a.options[0], EvidenceKind::content, true));
}

TEST(RelinkV2, TheExportVersionIsChosenByTheEmbeddedIds) {
    auto f = smoothExport();
    // a second export of the same print with the same geometry
    Sidecar v2 = f->p.exportRecords[0];
    v2.editVersionId = newUuid();
    v2.version = 2;
    v2.fileSha256 = std::string(64, 'a');
    f->p.exportRecords.push_back(v2);
    RelinkInput in = describe(plainCopy(*f, "x.png"));
    in.hasIds = true;
    in.ids.itemId = v2.itemId;
    in.ids.editVersionId = v2.editVersionId;
    const auto a = assessRelink(in, f->p);
    EXPECT_EQ(a.decision, RelinkDecision::unique);
    ASSERT_EQ(a.options.size(), 2u);
    EXPECT_EQ(a.options[0].editVersionId, v2.editVersionId);
    // without the version id both exports fit equally: the user picks
    in.ids.editVersionId.clear();
    EXPECT_EQ(assessRelink(in, f->p).decision, RelinkDecision::ambiguous);
}

TEST(RelinkV2, TwoPrintsWithTheSamePictureAreAmbiguousUntilTheTargetIsChosen) {
    auto f = smoothExport();
    Sidecar other = f->p.exportRecords[0];
    other.itemId = f->p.scans[0].items[1].itemId;
    other.editVersionId = newUuid();
    other.fileSha256 = std::string(64, 'b');
    f->p.exportRecords.push_back(other);
    const RelinkInput in = describe(plainCopy(*f, "x.png"));
    const auto a = assessRelink(in, f->p);
    EXPECT_EQ(a.decision, RelinkDecision::ambiguous);
    EXPECT_EQ(a.options.size(), 2u);
    const auto t = assessRelink(in, f->p, other.itemId);
    EXPECT_EQ(t.decision, RelinkDecision::unique);
    ASSERT_EQ(t.options.size(), 1u);
    EXPECT_EQ(t.options[0].itemId, other.itemId);
}

TEST(RelinkV2, AnUnrelatedPictureIsNoMatch) {
    auto f = smoothExport();
    const auto file = f->dir / "graded" / "unrelated.png";
    std::filesystem::create_directories(file.parent_path());
    const ImageBuffer other = makeSmoothImage(f->rep.size.w, f->rep.size.h, 3.7);
    writePlainPng(other, file);
    const RelinkInput in = describe(file);
    const auto a = assessRelink(in, f->p);
    EXPECT_EQ(a.decision, RelinkDecision::none);
    ASSERT_EQ(a.options.size(), 1u);
    EXPECT_FALSE(a.options[0].contentPass);
    // with a target the user decides; a new alignment is offered because no export of that print matches
    const auto t = assessRelink(in, f->p, f->p.scans[0].items[0].itemId);
    EXPECT_EQ(t.decision, RelinkDecision::ambiguous);
    EXPECT_TRUE(std::any_of(t.options.begin(), t.options.end(),
                            [](const RelinkOption& o) { return o.newAlignment; }));
}

TEST(RelinkV2, NewAlignmentForAPrintThatThisProjectNeverExported) {
    // UX-07: exported by another project (or the project was not saved) — the user aligns the file explicitly
    auto f = smoothExport();
    f->p.exportRecords.clear();
    const RelinkInput in = describe(plainCopy(*f, "from another project.png"));
    EXPECT_EQ(assessRelink(in, f->p).decision, RelinkDecision::none);

    ScanRecord& s = f->p.scans[0];
    ItemRecord& it = s.items[0];
    it.upright = 0; // as exported by the fixture
    auto scan = readImageFile(pathFromUtf8(s.path));
    ASSERT_TRUE(scan.ok());
    const ExportGeometry g = alignmentGeometry(s, it, in.size);
    const Signature area = signatureOfExport(*scan, g);
    ASSERT_TRUE(area.valid);
    const auto t = assessRelink(in, f->p, it.itemId, &area);
    EXPECT_EQ(t.decision, RelinkDecision::unique);
    ASSERT_EQ(t.options.size(), 1u);
    EXPECT_TRUE(t.options[0].newAlignment);
    EXPECT_TRUE(t.options[0].contentPass) << t.options[0].contentScore;

    const Sidecar rec = alignmentRecord(f->p, s, it, g, area, "from another project.png", in.sha256);
    EXPECT_EQ(rec.origin, "manual_alignment");
    EXPECT_TRUE(rec.exportedAt.empty()) << "a manual alignment is not an export";
    EXPECT_EQ(rec.itemId, it.itemId);
    EXPECT_EQ(rec.version, 2); // the print already has export version 1
    EXPECT_EQ(rec.fileSha256, in.sha256);
    EXPECT_EQ(rec.exportSize, in.size);
    auto again = readImageFile(plainCopy(*f, "again.png"));
    ASSERT_TRUE(again.ok());
    const RelinkCheck same = checkEdited(rec, *again, {});
    EXPECT_TRUE(same.sizeMatches);
    EXPECT_TRUE(same.signaturePass);
    EXPECT_EQ(same.suggestedRotate, 0);
    {
        // the user has turned the print upright since: the alignment is turned, the content check finds the
        // turn
        ItemRecord turned = it;
        turned.upright = 1;
        const ExportGeometry tg = alignmentGeometry(s, turned, in.size);
        const Sidecar tr =
            alignmentRecord(f->p, s, turned, tg, signatureOfExport(*scan, tg), "x.png", in.sha256);
        EXPECT_EQ(tr.exportSize, (SizeI{in.size.h, in.size.w}));
        const RelinkCheck c = checkEdited(tr, *again, {});
        EXPECT_TRUE(c.signaturePass);
        EXPECT_NE(c.suggestedRotate, 0);
    }
    f->p.exportRecords.push_back(rec);
    const auto file = f->dir / "aligned.fkproj";
    ASSERT_TRUE(saveProject(f->p, file).ok());
    EXPECT_NE(slurp(file).find("manual_alignment"), std::string::npos);
    auto back = loadProject(file);
    ASSERT_TRUE(back.ok());
    ASSERT_EQ(back->exportRecords.size(), 1u);
    EXPECT_EQ(back->exportRecords[0].origin, "manual_alignment");
    // relinking the same file again is now decided by its hash
    EXPECT_EQ(assessRelink(in, *back).decision, RelinkDecision::unique);
}

TEST(RelinkV2, ExportRecordsDoNotWriteAnOriginKey) {
    auto f = smoothExport();
    const auto file = f->dir / "p.fkproj";
    ASSERT_TRUE(saveProject(f->p, file).ok());
    EXPECT_EQ(slurp(file).find("\"origin\""), std::string::npos) << "files stay readable as before";
    auto back = loadProject(file);
    ASSERT_TRUE(back.ok());
    EXPECT_EQ(back->exportRecords[0].origin, "export");
}

TEST(RelinkV2, AreaSignatureMatchesTheExportIncludingItsOrientation) {
    // the new-alignment content check samples the scan (or its preview) the way an export would
    TempDir t;
    const ImageBuffer img = makeSmoothImage(320, 240, 1.0);
    fk::test::writeTestTiff(t / "s.tif", img, {});
    Project p = sampleProject(t / "s.tif");
    ScanRecord& s = p.scans[0];
    ItemRecord& it = s.items[0];
    it.upright = 1;
    auto src = openTiff(t / "s.tif");
    ASSERT_TRUE(src.ok());
    ExportRequest r;
    r.region = it.inner;
    r.frame = it.outer.quad;
    r.mode = GeometryMode::straightened;
    r.upright = 1;
    r.output = t / "e.tif";
    r.write.format = FileFormat::tiff;
    auto rep = exportRegion(**src, r, {}, {});
    ASSERT_TRUE(rep.ok());
    const ExportGeometry g = alignmentGeometry(s, it);
    EXPECT_EQ(g.outSize, rep->size);
    const Signature full = signatureOfExport(img, g);
    EXPECT_GT(compareSignatures(full, rep->signature), 0.95);
    // from a preview 4× smaller
    auto small = ImageBuffer::create(80, 60, 3, 16);
    ASSERT_TRUE(small.ok());
    for (int64_t y = 0; y < 60; ++y)
        for (int64_t x = 0; x < 80; ++x)
            for (int c = 0; c < 3; ++c) {
                const uint16_t v = img.sample16(x * 4 + 2, y * 4 + 2, c);
                std::memcpy(small->row(y) + (x * 3 + c) * 2, &v, 2);
            }
    const Signature preview = signatureOfExport(*small, g, 4.0);
    EXPECT_GT(compareSignatures(preview, rep->signature), kSignatureMatchThreshold);
    // orientation is part of it: the upside-down picture does not match without turning it
    EXPECT_LT(compareSignatures(rotateSignature(full, 2), rep->signature), kSignatureMatchThreshold);
}

TEST(RelinkV2, NewAlignmentFollowsTheGradedAspectRatio) {
    // a print tilted by a few degrees: a pixel-crop export is larger than the straightened image area
    TempDir t;
    Project p = sampleProject(t / "s.tif");
    ScanRecord& s = p.scans[0];
    ItemRecord& it = s.items[0];
    const Mat3 rot = Mat3::translate(80, 115) * Mat3::rotate(0.05) * Mat3::translate(-80, -115);
    Quad q = it.inner.quad, o = it.outer.quad;
    for (auto& v : q.p)
        v = rot.apply(v);
    for (auto& v : o.p)
        v = rot.apply(v);
    it.inner = Shape::fromQuad(q);
    it.outer.quad = o;
    it.upright = 0;
    const SizeI scan{s.info.width, s.info.height};
    const auto crop = computeExportGeometry(it.inner, o, GeometryMode::pixel_crop, 0, std::nullopt, scan);
    const auto straight =
        computeExportGeometry(it.inner, o, GeometryMode::straightened, 0, std::nullopt, scan);
    ASSERT_NE(crop.outSize, straight.outSize);
    EXPECT_EQ(alignmentGeometry(s, it, crop.outSize).mode, GeometryMode::pixel_crop);
    EXPECT_EQ(alignmentGeometry(s, it, straight.outSize).mode, GeometryMode::straightened);
    // a smaller graded file (resized in the editor) of the straightened shape, and the same turned by 90°
    EXPECT_EQ(alignmentGeometry(s, it, {straight.outSize.w / 2, straight.outSize.h / 2}).mode,
              GeometryMode::straightened);
    EXPECT_EQ(alignmentGeometry(s, it, {straight.outSize.h, straight.outSize.w}).mode,
              GeometryMode::straightened);
    EXPECT_EQ(alignmentGeometry(s, it).mode, GeometryMode::straightened);
    // untilted print: both shapes have the same size → the pixel crop (default export, no resampling)
    ItemRecord flat = it;
    flat.inner = Shape::fromQuad(rectQuad(20, 30, 140, 200));
    flat.outer.quad = rectQuad(10, 12, 150, 220);
    const auto fc =
        computeExportGeometry(flat.inner, flat.outer.quad, GeometryMode::pixel_crop, 0, std::nullopt, scan);
    EXPECT_EQ(alignmentGeometry(s, flat, fc.outSize).mode, GeometryMode::pixel_crop);
}

TEST(RelinkV2, AResizedGradedFileOfASquarePrintIsPlacedByItsPicture) {
    // square image area tilted by 3°: crop and straightened shapes are both square; size alone cannot tell
    TempDir t;
    const ImageBuffer img = makeSmoothImage(320, 240, 1.0);
    fk::test::writeTestTiff(t / "s.tif", img, {});
    Project p = sampleProject(t / "s.tif");
    ScanRecord& s = p.scans[0];
    ItemRecord& it = s.items[0];
    const Mat3 rot = Mat3::translate(100, 120) * Mat3::rotate(0.05) * Mat3::translate(-100, -120);
    Quad q = rectQuad(40, 40, 160, 160), o = rectQuad(30, 30, 170, 200);
    for (auto& v : q.p)
        v = rot.apply(v);
    for (auto& v : o.p)
        v = rot.apply(v);
    it.inner = Shape::fromQuad(q);
    it.outer.quad = o;
    it.upright = 0;
    auto src = openTiff(t / "s.tif");
    ASSERT_TRUE(src.ok());
    for (GeometryMode mode : {GeometryMode::pixel_crop, GeometryMode::straightened}) {
        ExportRequest r;
        r.region = it.inner;
        r.frame = o;
        r.mode = mode;
        r.output = t / (std::string(geometryModeName(mode)) + ".tif");
        r.write.format = FileFormat::tiff;
        auto rep = exportRegion(**src, r, {}, {});
        ASSERT_TRUE(rep.ok()) << rep.error().message;
        const SizeI resized{rep->size.w / 2, rep->size.h / 2};
        const ExportGeometry g = alignmentGeometry(s, it, resized, img, 1.0, rep->signature);
        EXPECT_EQ(g.mode, mode) << geometryModeName(mode);
    }
}

// ---- confirmed relink transform (FK-002, U005, RFC-0006 §Confirmed transform)
// ------------------------------------

/// `img` turned by k × 90° clockwise.
ImageBuffer turned(const ImageBuffer& img, int k) {
    k = ((k % 4) + 4) % 4;
    const int64_t W = img.width(), H = img.height();
    const bool swap = k & 1;
    auto r = ImageBuffer::create(swap ? H : W, swap ? W : H, img.channels(), img.bits());
    EXPECT_TRUE(r.ok());
    ImageBuffer out = std::move(r.value());
    const size_t px = static_cast<size_t>(img.channels()) * (img.bits() / 8);
    for (int64_t y = 0; y < H; ++y)
        for (int64_t x = 0; x < W; ++x) {
            int64_t nx = x, ny = y;
            if (k == 1) nx = H - 1 - y, ny = x;
            if (k == 2) nx = W - 1 - x, ny = H - 1 - y;
            if (k == 3) nx = y, ny = W - 1 - x;
            std::memcpy(out.row(ny) + nx * px, img.row(y) + x * px, px);
        }
    return out;
}

TEST(RelinkTransform, TheSizeLimitsTheTurnsAndThePictureDecides) {
    auto f = smoothExport(); // 120 × 170 image area: not square
    const Sidecar& rec = f->p.exportRecords[0];
    auto px = readImageFile(f->editPath);
    ASSERT_TRUE(px.ok());
    for (int k = 0; k < 4; ++k) {
        const RelinkCheck c = checkEdited(rec, turned(*px, k), {});
        const std::vector<int> expect = (k & 1) ? std::vector<int>{1, 3} : std::vector<int>{0, 2};
        EXPECT_EQ(c.allowedRotations, expect) << k;
        EXPECT_EQ(c.suggestedRotate, k);
        EXPECT_TRUE(c.rotationReliable) << k << " margin " << c.signatureScore;
        const auto& a = c.actionsNeeded;
        EXPECT_EQ(std::count(a.begin(), a.end(), "rotate"), k ? 1 : 0) << k;
        EXPECT_EQ(std::count(a.begin(), a.end(), "scale"), 0) << k;
    }
    // a different crop cannot be placed in any orientation
    auto wrong = ImageBuffer::create(150, 150, 3, 16);
    ASSERT_TRUE(wrong.ok());
    const RelinkCheck w = checkEdited(rec, *wrong, {});
    EXPECT_TRUE(w.allowedRotations.empty());
    EXPECT_NE(std::find(w.actionsNeeded.begin(), w.actionsNeeded.end(), "manual_alignment"),
              w.actionsNeeded.end());
}

TEST(RelinkTransform, ASymmetricPictureLeavesTheTurnToTheUser) {
    // picture symmetric under a half turn: 0° and 180° look the same, so the picture cannot decide
    const int64_t W = 120, H = 170;
    auto r = ImageBuffer::create(W, H, 3, 16);
    ASSERT_TRUE(r.ok());
    ImageBuffer sym = std::move(r.value());
    for (int64_t y = 0; y < H; ++y)
        for (int64_t x = 0; x < W; ++x) {
            const double dx = (x - (W - 1) / 2.0) / W, dy = (y - (H - 1) / 2.0) / H;
            const double v = 0.5 + 0.3 * std::cos(9 * dx * dy) * std::cos(7 * dx) + 0.1 * std::cos(11 * dy);
            for (int c = 0; c < 3; ++c) {
                const uint16_t u = static_cast<uint16_t>(std::clamp(v, 0.0, 1.0) * 65535);
                std::memcpy(sym.row(y) + (x * 3 + c) * 2, &u, 2);
            }
        }
    Sidecar rec;
    rec.exportSize = {W, H};
    rec.bits = 16;
    rec.signature = computeSignature(sym);
    const RelinkCheck c = checkEdited(rec, turned(sym, 2), {});
    EXPECT_TRUE(c.signaturePass);
    EXPECT_FALSE(c.rotationReliable);
    EXPECT_NE(std::find(c.actionsNeeded.begin(), c.actionsNeeded.end(), "choose_rotation"),
              c.actionsNeeded.end());
    // a square image area allows all four turns
    Sidecar sq = rec;
    sq.exportSize = {120, 120};
    auto s2 = ImageBuffer::create(120, 120, 3, 16);
    ASSERT_TRUE(s2.ok());
    EXPECT_EQ(checkEdited(sq, *s2, {}).allowedRotations, (std::vector<int>{0, 1, 2, 3}));
}

TEST(RelinkTransform, StoredWithTheRelinkAndKeptBySaving) {
    auto f = smoothExport();
    EditVersion& ev = f->p.scans[0].items[0].edits.back();
    ev.relinkedPath = pathToUtf8(f->dir / "graded.tif");
    ev.matchedBy = "file_name+content";
    ev.relinkTransform.set = true;
    ev.relinkTransform.rotate = 2;
    ev.relinkTransform.rotateSource = "user";
    ev.relinkTransform.scale = true;
    ev.relinkTransform.contentScore = 0.93;
    ev.relinkTransform.gradedSize = {85, 60};
    const auto file = f->dir / "t.fkproj";
    ASSERT_TRUE(saveProject(f->p, file).ok());
    auto back = loadProject(file);
    ASSERT_TRUE(back.ok());
    const RelinkTransform& t = back->scans[0].items[0].edits.back().relinkTransform;
    EXPECT_TRUE(t.set);
    EXPECT_EQ(t.rotate, 2);
    EXPECT_EQ(t.rotateSource, "user");
    EXPECT_TRUE(t.scale);
    EXPECT_FALSE(t.lowerDepth);
    EXPECT_NEAR(t.contentScore, 0.93, 1e-9);
    EXPECT_EQ(t.gradedSize, (SizeI{85, 60}));
    // a relink saved by an earlier version has no transform: decided again from the picture
    std::string text = slurp(file);
    const auto pos = text.find("\"transform\"");
    ASSERT_NE(pos, std::string::npos);
    const auto end = text.find('}', pos);
    text.erase(pos, end - pos + 1);
    // drop the comma left before the removed member
    const auto comma = text.rfind(',', pos);
    text.erase(comma, 1);
    std::ofstream(file, std::ios::binary | std::ios::trunc) << text;
    auto old = loadProject(file);
    ASSERT_TRUE(old.ok()) << old.error().message;
    EXPECT_FALSE(old->scans[0].items[0].edits.back().relinkTransform.set);
    EXPECT_FALSE(old->scans[0].items[0].edits.back().relinkedPath.empty());
}

// ---- source identity (FK-007, U007, RFC-0009 §Source identity)
// ---------------------------------------------------

void writePatternBytes(const std::filesystem::path& p, size_t n, uint8_t seed, size_t flipAt = SIZE_MAX) {
    std::vector<char> b(n);
    for (size_t i = 0; i < n; ++i)
        b[i] = static_cast<char>((i * 131u + seed) & 0xff);
    if (flipAt < n) b[flipAt] = static_cast<char>(b[flipAt] ^ 0x5a);
    std::ofstream(p, std::ios::binary | std::ios::trunc)
        .write(b.data(), static_cast<std::streamsize>(b.size()));
}

TEST(SourceIdentity, ReplacedScanAtSamePathIsNotTrusted) {
    // formerly KnownIssue.FK007_ReplacedScanAtSamePathIsNotTrusted
    TempDir t;
    const auto scan = t / "scan.tif";
    writePatternBytes(scan, 3u << 20, 1);
    auto fp = fingerprintFile(scan, true);
    ASSERT_TRUE(fp);
    Project p = newProject("t");
    ScanRecord s;
    s.scanId = newUuid();
    s.path = pathToUtf8(scan);
    s.fingerprint = *fp;
    p.scans.push_back(s);
    writePatternBytes(scan, 3u << 20, 2); // replaced: same name and size, other content
    resolveScanPaths(p, t.path());
    EXPECT_FALSE(p.scans[0].missing);
    EXPECT_EQ(verifyScans(p), 1);
    EXPECT_TRUE(p.scans[0].changed) << "a different file at the recorded path was accepted as the scan";
    EXPECT_EQ(p.scans[0].changeReason, "quick");
}

TEST(SourceIdentity, TheWholeFileIsComparedWhenItsHashIsKnown) {
    TempDir t;
    const auto f = t / "scan.tif";
    const size_t n = 5u << 20;
    writePatternBytes(f, n, 7);
    auto quickOnly = fingerprintFile(f, false);
    auto full = fingerprintFile(f, true);
    ASSERT_TRUE(quickOnly && full);
    EXPECT_FALSE(full->fullSha256.empty());
    EXPECT_EQ(verifyFileIdentity(f, *full).state, Identity::same);
    EXPECT_TRUE(verifyFileIdentity(f, *full).fullChecked);
    // edited in the middle (outside the first and last MiB): only the full hash sees it
    writePatternBytes(f, n, 7, n / 2);
    EXPECT_EQ(verifyFileIdentity(f, *quickOnly).state, Identity::same) << "quick fingerprint cannot see it";
    const IdentityCheck c = verifyFileIdentity(f, *full);
    EXPECT_EQ(c.state, Identity::changed);
    EXPECT_EQ(c.reason, "full");
    // size changed / gone
    writePatternBytes(f, n + 1, 7);
    EXPECT_EQ(verifyFileIdentity(f, *full).reason, "size");
    std::filesystem::remove(f);
    EXPECT_EQ(verifyFileIdentity(f, *full).state, Identity::missing);
}

TEST(SourceIdentity, RenamedOrMovedScansAreFoundByContentOnly) {
    TempDir t;
    std::filesystem::create_directories(t / "old");
    std::filesystem::create_directories(t / "new" / "sub");
    const auto orig = t / "old" / "a.tif";
    writePatternBytes(orig, 2u << 20, 3);
    Project p = newProject("t");
    ScanRecord s;
    s.scanId = newUuid();
    s.path = pathToUtf8(orig);
    s.fingerprint = *fingerprintFile(orig, true);
    p.scans.push_back(s);
    // moved and renamed; a decoy with the old name but other content sits next to it
    std::filesystem::rename(orig, t / "new" / "sub" / "renamed.tif");
    writePatternBytes(t / "new" / "a.tif", 2u << 20, 4);
    EXPECT_EQ(relocateScans(p, t.path(), t / "new"), 1);
    EXPECT_EQ(pathFromUtf8(p.scans[0].path).filename(), "renamed.tif");
    EXPECT_EQ(verifyScans(p), 0);
}

TEST(SourceIdentity, ChangesWhileReadingAreSeenByTheStamp) {
    TempDir t;
    const auto f = t / "x.bin";
    writePatternBytes(f, 1000, 1);
    const FileStamp before = fileStamp(f);
    EXPECT_TRUE(before.exists);
    EXPECT_EQ(fileStamp(f), before);
    writePatternBytes(f, 1001, 1);
    EXPECT_NE(fileStamp(f), before);
    std::filesystem::remove(f);
    EXPECT_FALSE(fileStamp(f).exists);
}

TEST(SourceIdentity, AGradedFileSavedOverIsCheckedAgain) {
    auto f = smoothExport();
    const Sidecar& rec = f->p.exportRecords[0];
    auto px = readImageFile(f->editPath);
    ASSERT_TRUE(px.ok());
    RelinkTransform t;
    t.set = true;
    t.gradedSize = {px->width(), px->height()};
    t.gradedIccRecorded = true; // no embedded profile at relink
    // graded again in the editor (brighter, same picture) → still the same picture
    ImageBuffer regraded = std::move(px->clone().value());
    for (int64_t y = 0; y < regraded.height(); ++y) {
        auto* r = reinterpret_cast<uint16_t*>(regraded.row(y));
        for (int64_t i = 0; i < regraded.width() * 3; ++i)
            r[i] = static_cast<uint16_t>(std::min(65535, r[i] + 3000));
    }
    const GradedRecheck same = recheckGraded(rec, t, regraded, {});
    EXPECT_TRUE(same.samePicture) << same.reason << " " << same.score;
    // replaced by another picture of the same size
    const ImageBuffer other = makeSmoothImage(px->width(), px->height(), 3.7);
    const GradedRecheck diff = recheckGraded(rec, t, other, {});
    EXPECT_FALSE(diff.samePicture);
    EXPECT_EQ(diff.reason, "picture");
    // resized
    auto small = ImageBuffer::create(px->width() / 2, px->height() / 2, 3, 16);
    ASSERT_TRUE(small.ok());
    EXPECT_EQ(recheckGraded(rec, t, *small, {}).reason, "size");
    // a profile appeared → colours would be read differently
    ImageInfo withIcc;
    withIcc.icc = {1, 2, 3};
    EXPECT_EQ(recheckGraded(rec, t, regraded, withIcc).reason, "profile");
    // turned 180° at relink: the picture is compared at the confirmed turn
    RelinkTransform turnedT = t;
    turnedT.rotate = 2;
    EXPECT_TRUE(recheckGraded(rec, turnedT, turned(regraded, 2), {}).samePicture);
    EXPECT_FALSE(recheckGraded(rec, turnedT, regraded, {}).samePicture);
}

// ---- data protection (FK-008, U008, RFC-0017)
// --------------------------------------------------------------------

TEST(DataProtection, DamagedProjectOffersItsBackup) {
    // formerly KnownIssue.FK008_DamagedProjectOffersItsBackup
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
    ASSERT_TRUE(usableBackup(file).has_value());
}

TEST(DataProtection, ADamagedFileNeverReplacesAGoodBackup) {
    TempDir t;
    const auto file = t / "p.fkproj";
    Project p = newProject("good");
    ASSERT_TRUE(saveProject(p, file));
    p.name = "good, second save";
    ASSERT_TRUE(saveProject(p, file)); // .bak = "good"
    std::ofstream(file, std::ios::binary | std::ios::trunc) << "{ damaged";
    p.name = "saved over the damaged file";
    ASSERT_TRUE(saveProject(p, file));
    auto bak = loadProject(pathFromUtf8(pathToUtf8(file) + ".bak"));
    ASSERT_TRUE(bak.ok()) << "the backup must still be the last good save";
    EXPECT_EQ(bak->name, "good");
    EXPECT_EQ(loadProject(file)->name, "saved over the damaged file");
}

TEST(DataProtection, RestoringKeepsTheDamagedFile) {
    TempDir t;
    const auto file = t / "p.fkproj";
    Project p = newProject("restore me");
    ASSERT_TRUE(saveProject(p, file));
    ASSERT_TRUE(saveProject(p, file));
    std::ofstream(file, std::ios::binary | std::ios::trunc) << "{ damaged";
    auto bak = usableBackup(file);
    ASSERT_TRUE(bak.has_value());
    auto aside = setAsideDamaged(file);
    ASSERT_TRUE(aside.ok()) << aside.error().message;
    EXPECT_FALSE(std::filesystem::exists(file));
    EXPECT_EQ(slurp(*aside), "{ damaged") << "the damaged original is kept unchanged";
    auto restored = loadProject(*bak);
    ASSERT_TRUE(restored.ok());
    ASSERT_TRUE(saveProject(*restored, file));
    EXPECT_EQ(loadProject(file)->name, "restore me");
    EXPECT_TRUE(std::filesystem::exists(*aside));
    // a second damage the same second gets its own name
    std::ofstream(file, std::ios::binary | std::ios::trunc) << "{ again";
    auto aside2 = setAsideDamaged(file);
    ASSERT_TRUE(aside2.ok());
    EXPECT_NE(*aside2, *aside);
}

TEST(DataProtection, OutputParametersAreSavedWithTheProject) {
    TempDir t;
    const auto file = t / "p.fkproj";
    Project p = newProject("out");
    p.settings.outputJson = R"({"geometryMode":"straighten","finalMode":"synthetic","featherPx":2.5})";
    ASSERT_TRUE(saveProject(p, file));
    auto back = loadProject(file);
    ASSERT_TRUE(back.ok());
    EXPECT_NE(back->settings.outputJson.find("\"geometryMode\":\"straighten\""), std::string::npos)
        << back->settings.outputJson;
    EXPECT_NE(back->settings.outputJson.find("2.5"), std::string::npos);
}

TEST(DataProtection, AnInterruptedSaveLeavesTheProjectIntact) {
    TempDir t;
    const auto file = t / "p.fkproj";
    Project p = newProject("intact");
    ASSERT_TRUE(saveProject(p, file));
    // what a save interrupted before its rename leaves behind
    std::ofstream(pathFromUtf8(pathToUtf8(file) + ".fk-tmp-abcd1234"), std::ios::binary) << "{ half writ";
    auto back = loadProject(file);
    ASSERT_TRUE(back.ok());
    EXPECT_EQ(back->name, "intact");
    // a save into a folder that cannot be written fails and reports it; nothing is changed
    auto st = saveProject(p, t / "no-such-dir" / "p.fkproj");
    EXPECT_FALSE(st.ok());
    EXPECT_EQ(loadProject(file)->name, "intact");
}

// ---------------------------------------------------------------- UX-01: automatic count (U011)

namespace {
bool hasCountWarning(const ScanRecord& s) {
    return std::any_of(s.warnings.begin(), s.warnings.end(),
                       [](const std::string& w) { return w.rfind("count_mismatch", 0) == 0; });
}
bool itemsFlagged(const ScanRecord& s) {
    return !s.items.empty() && std::all_of(s.items.begin(), s.items.end(), [](const ItemRecord& it) {
        return std::find(it.reasons.begin(), it.reasons.end(), "count_mismatch") != it.reasons.end();
    });
}
} // namespace

TEST(CountSetting, WarningFollowsTheSettingWithoutDetectingAgain) {
    TempDir dir;
    Project p = sampleProject(dir / "scan.tif"); // 2 prints
    ScanRecord& s = p.scans[0];
    s.detectedAt = "2026-10-30T00:00:00Z";
    s.warnings = {"dpi_unknown", "possible_missed: 1"};
    s.expectedCount = 4;
    s.autoCount = false;
    updateCountWarning(s);
    EXPECT_TRUE(hasCountWarning(s));
    EXPECT_TRUE(itemsFlagged(s));
    // the manual count corrected: the warning goes, nothing else changes
    s.expectedCount = 2;
    updateCountWarning(s);
    EXPECT_FALSE(hasCountWarning(s));
    for (const auto& it : s.items)
        EXPECT_EQ(std::count(it.reasons.begin(), it.reasons.end(), "count_mismatch"), 0);
    EXPECT_EQ(s.warnings, (std::vector<std::string>{"dpi_unknown", "possible_missed: 1"}));
    // automatic: never a count warning, whatever the manual value kept for later
    s.expectedCount = 3;
    s.autoCount = true;
    updateCountWarning(s);
    EXPECT_FALSE(hasCountWarning(s));
    EXPECT_EQ(s.expectedCount, 3) << "the manual value is kept";
    // manual again: the kept value applies; updating twice does not duplicate anything
    s.autoCount = false;
    updateCountWarning(s);
    updateCountWarning(s);
    EXPECT_EQ(std::count_if(s.warnings.begin(), s.warnings.end(),
                            [](const std::string& w) { return w.rfind("count_mismatch", 0) == 0; }),
              1);
    EXPECT_NE(std::find(s.warnings.begin(), s.warnings.end(), "count_mismatch: found 2, expected 3"),
              s.warnings.end());
    for (const auto& it : s.items)
        EXPECT_EQ(std::count(it.reasons.begin(), it.reasons.end(), "count_mismatch"), 1);
}

TEST(CountSetting, BlankScanWarnsOnlyWhenACountWasGiven) {
    ScanRecord s;
    s.detectedAt = "2026-10-30T00:00:00Z";
    s.warnings = {"no_prints_found"};
    s.autoCount = true;
    updateCountWarning(s);
    EXPECT_EQ(s.warnings, std::vector<std::string>{"no_prints_found"});
    s.autoCount = false;
    s.expectedCount = 1;
    updateCountWarning(s);
    EXPECT_TRUE(hasCountWarning(s));
    // a scan not detected yet has no count to compare
    ScanRecord fresh;
    fresh.autoCount = false;
    updateCountWarning(fresh);
    EXPECT_TRUE(fresh.warnings.empty());
}

TEST(CountSetting, SavedPerScanAndAsTheProjectDefault) {
    TempDir dir;
    Project p = sampleProject(dir / "scan.tif");
    p.settings.autoCount = false;
    p.settings.expectedCount = 1;
    p.scans[0].autoCount = true;
    p.scans[0].expectedCount = 6;
    ScanRecord second = p.scans[0];
    second.scanId = newUuid();
    second.autoCount = false;
    second.expectedCount = 1;
    p.scans.push_back(second); // a mixed batch: one scan automatic, one with one print
    const auto file = dir / "count.fkproj";
    ASSERT_TRUE(saveProject(p, file).ok());
    auto q = loadProject(file);
    ASSERT_TRUE(q.ok()) << q.error().message;
    EXPECT_FALSE(q->settings.autoCount);
    EXPECT_EQ(q->settings.expectedCount, 1);
    ASSERT_EQ(q->scans.size(), 2u);
    EXPECT_TRUE(q->scans[0].autoCount);
    EXPECT_EQ(q->scans[0].expectedCount, 6);
    EXPECT_FALSE(q->scans[1].autoCount);
    EXPECT_EQ(q->scans[1].expectedCount, 1);
}

TEST(CountSetting, ProjectsWithoutTheSettingKeepTheirManualCount) {
    // a 0.6 project: scans were detected against their expected count; new scans follow the new default
    TempDir dir;
    Project p = sampleProject(dir / "scan.tif");
    // JSON objects are written with sorted keys: "autoCount" is never the last key of its object
    const std::string js =
        std::regex_replace(projectToJson(p), std::regex(R"("autoCount"\s*:\s*(true|false)\s*,)"), "");
    ASSERT_EQ(js.find("autoCount"), std::string::npos);
    auto q = projectFromJson(js);
    ASSERT_TRUE(q.ok()) << q.error().message;
    EXPECT_FALSE(q->scans[0].autoCount);
    EXPECT_EQ(q->scans[0].expectedCount, 2);
    EXPECT_TRUE(q->settings.autoCount);
}
