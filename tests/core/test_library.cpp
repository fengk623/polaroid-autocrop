// RFC-0018 / PRD F16 (UX-05, U018): libraries — layout, schema versions, unknown keys, atomic writes, backup
// rotation, restore order, two instances (lock), migration copy of old projects, relative paths, isolation,
// cache.
#include <gtest/gtest.h>

#include "fk/library.hpp"
#include "fk/metadata.hpp"
#include "fk/product_info.hpp"
#include "fk/util.hpp"
#include "support/test_support.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iterator>

using namespace fk;
using fk::test::TempDir;
namespace fs = std::filesystem;
using json = nlohmann::ordered_json;

namespace {

std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}
void writeText(const fs::path& p, const std::string& s) {
    std::ofstream o(p, std::ios::binary | std::ios::trunc);
    o << s;
}

/// A real scan file and a scan record with two prints that reference it.
ScanRecord scanOf(const fs::path& file, uint32_t seed) {
    test::writeTestTiff(file, test::makePatternImage(64, 48, 3, 8, seed));
    ScanRecord s;
    s.scanId = newUuid();
    s.path = pathToUtf8(file);
    s.fingerprint = *fingerprintFile(file);
    s.info.width = 64;
    s.info.height = 48;
    s.info.channels = 3;
    s.info.bitsPerSample = 8;
    for (int i = 1; i <= 2; ++i) {
        DetectedItem d;
        d.outer = Shape::fromQuad(rectQuad(2.0 * i, 3, 20 + i, 40));
        d.inner = Shape::fromQuad(rectQuad(4.0 * i, 6, 18, 36));
        d.filmTypeId = "instax_mini";
        s.items.push_back(itemFromDetection(d, i));
    }
    return s;
}

std::vector<std::string> fileNames(const fs::path& dir) {
    std::vector<std::string> out;
    for (const auto& e : fs::directory_iterator(dir))
        out.push_back(pathToUtf8(e.path().filename()));
    std::sort(out.begin(), out.end());
    return out;
}

const std::string kT0 = "2026-11-12T10:00:00Z";
std::string plus(const std::string& iso, int64_t seconds) {
    return epochToIsoUtc(isoUtcToEpoch(iso) + seconds);
}

} // namespace

TEST(Library, TimesRoundTrip) {
    EXPECT_EQ(isoUtcToEpoch("1970-01-01T00:00:00Z"), 0);
    EXPECT_EQ(isoUtcToEpoch("2026-11-12T21:58:03Z"), 1794520683);
    EXPECT_EQ(epochToIsoUtc(1794520683), "2026-11-12T21:58:03Z");
    EXPECT_EQ(epochToIsoUtc(isoUtcToEpoch("2028-02-29T23:59:59Z")), "2028-02-29T23:59:59Z");
    EXPECT_EQ(isoUtcToEpoch("12 Nov 2026"), -1);
    EXPECT_EQ(isoUtcToEpoch("2026-11-12T21:58:03"), -1) << "local times are not UTC";
}

TEST(Library, FolderNamesWorkOnEveryFileSystem) {
    EXPECT_EQ(libraryFolderName("Summer 2026"), "Summer 2026.fklibrary");
    EXPECT_EQ(libraryFolderName("a/b\\c:d*e?f\"g<h>i|j"), "a_b_c_d_e_f_g_h_i_j.fklibrary");
    EXPECT_EQ(libraryFolderName("  trailing dots... "), "trailing dots.fklibrary");
    EXPECT_EQ(libraryFolderName(""), "Library.fklibrary");
    EXPECT_EQ(libraryFolderName("con"), "con_.fklibrary") << "Windows device name";
    EXPECT_EQ(libraryFolderName("宝丽来 2026"), "宝丽来 2026.fklibrary");
}

TEST(Library, CreateWritesManifestAndCatalogIntoANewFolder) {
    TempDir t;
    auto lib = createLibrary(t.path(), "Summer 2026");
    ASSERT_TRUE(lib.ok()) << lib.error().message;
    EXPECT_EQ(lib->folder, t / "Summer 2026.fklibrary");
    EXPECT_TRUE(isLibraryFolder(lib->folder));
    EXPECT_EQ(fileNames(lib->folder),
              (std::vector<std::string>{"backups", "catalog.fkproj", "library.json"}));
    EXPECT_EQ(fileNames(t.path()), (std::vector<std::string>{"Summer 2026.fklibrary"}))
        << "no temporary folder left";
    const json m = json::parse(slurp(libraryManifestPath(lib->folder)));
    EXPECT_EQ(m["schema"], "fk.library/1");
    EXPECT_EQ(m["name"], "Summer 2026");
    EXPECT_EQ(m["libraryId"].get<std::string>().size(), 36u);
    EXPECT_EQ(json::parse(slurp(libraryCatalogPath(lib->folder)))["schema"], "fk.project/1");

    LibraryOpenInfo info;
    auto back = openLibrary(lib->folder, &info);
    ASSERT_TRUE(back.ok()) << back.error().message;
    EXPECT_EQ(info.mode, OpenMode::read_write);
    EXPECT_EQ(info.manifestSource, "file");
    EXPECT_EQ(back->manifest.libraryId, lib->manifest.libraryId);
    EXPECT_EQ(back->catalog.projectId, lib->catalog.projectId);

    // an existing folder is never reused, whatever it holds
    const std::string before = slurp(libraryCatalogPath(lib->folder));
    auto again = createLibrary(t.path(), "Summer 2026");
    ASSERT_FALSE(again.ok());
    EXPECT_EQ(again.error().code, Errc::exists);
    EXPECT_EQ(slurp(libraryCatalogPath(lib->folder)), before);
    EXPECT_FALSE(createLibrary(t / "no such folder", "x").ok());
}

TEST(Library, NewerSchemasOpenReadOnly) {
    TempDir t;
    auto lib = createLibrary(t.path(), "L");
    ASSERT_TRUE(lib.ok());
    json m = json::parse(slurp(libraryManifestPath(lib->folder)));
    m["schema"] = "fk.library/2";
    writeText(libraryManifestPath(lib->folder), m.dump());
    LibraryOpenInfo info;
    ASSERT_TRUE(openLibrary(lib->folder, &info).ok());
    EXPECT_EQ(info.mode, OpenMode::read_only) << "a manifest of a later major version";

    m["schema"] = "fk.library/1";
    writeText(libraryManifestPath(lib->folder), m.dump());
    json c = json::parse(slurp(libraryCatalogPath(lib->folder)));
    c["schema"] = "fk.project/2";
    writeText(libraryCatalogPath(lib->folder), c.dump());
    ASSERT_TRUE(openLibrary(lib->folder, &info).ok());
    EXPECT_EQ(info.mode, OpenMode::read_only) << "a catalog of a later major version";
}

TEST(Library, RatingsLabelsAndOrderRoundTripAndUnknownKeysSurvive) {
    TempDir t;
    auto lib = createLibrary(t.path(), "L");
    ASSERT_TRUE(lib.ok());
    lib->catalog.scans.push_back(scanOf(t / "a.tif", 1));
    ItemRecord& it = lib->catalog.scans[0].items[0];
    it.library.rating = 4;
    it.library.pick = "picked";
    it.library.colorLabel = "green";
    it.library.textLabels = {"beach", "家人"};
    it.library.manualOrder = 3;
    it.library.addedAt = kT0;
    it.library.editedAt = plus(kT0, 60);
    it.library.editCount = 2;
    lib->catalog.scans[0].library.rating = 1;
    lib->manifest.cover = it.itemId;
    lib->manifest.sortJson = R"({"grid":{"key":"rating","descending":true}})";
    ASSERT_TRUE(saveLibrary(*lib));

    // a later version adds keys to the manifest and to a print's library data
    json m = json::parse(slurp(libraryManifestPath(lib->folder)));
    m["smartCollections"] = json::array({"later"});
    writeText(libraryManifestPath(lib->folder), m.dump());
    json c = json::parse(slurp(libraryCatalogPath(lib->folder)));
    c["scans"][0]["items"][0]["library"]["faces"] = 2;
    writeText(libraryCatalogPath(lib->folder), c.dump());

    auto back = openLibrary(lib->folder);
    ASSERT_TRUE(back.ok());
    const ItemRecord& b = back->catalog.scans[0].items[0];
    EXPECT_EQ(b.library, it.library);
    EXPECT_EQ(back->catalog.scans[0].library.rating, 1);
    EXPECT_TRUE(back->catalog.scans[0].items[1].library.empty());
    EXPECT_EQ(back->manifest.cover, it.itemId);
    EXPECT_EQ(json::parse(back->manifest.sortJson)["grid"]["key"], "rating");
    ASSERT_TRUE(saveLibrary(*back));
    m = json::parse(slurp(libraryManifestPath(lib->folder)));
    c = json::parse(slurp(libraryCatalogPath(lib->folder)));
    EXPECT_EQ(m["smartCollections"][0], "later");
    EXPECT_EQ(c["scans"][0]["items"][0]["library"]["faces"], 2);
    EXPECT_EQ(c["scans"][0]["items"][0]["library"]["rating"], 4);
    EXPECT_FALSE(c["scans"][0]["items"][1].contains("library")) << "nothing written when nothing is set";

    // a cover that is no longer in the catalog is never named
    back->catalog.scans[0].items.erase(back->catalog.scans[0].items.begin());
    ASSERT_TRUE(saveLibrary(*back));
    EXPECT_EQ(json::parse(slurp(libraryManifestPath(lib->folder)))["cover"], "");
}

TEST(Library, ProjectsWithoutLibraryDataAreUnchanged) {
    TempDir t;
    Project p = newProject("plain");
    p.scans.push_back(scanOf(t / "a.tif", 2));
    ASSERT_TRUE(saveProject(p, t / "p.fkproj"));
    const json j = json::parse(slurp(t / "p.fkproj"));
    EXPECT_FALSE(j["scans"][0].contains("library"));
    EXPECT_FALSE(j["scans"][0]["items"][0].contains("library"));
    // out-of-range values are read safely
    json bad = j;
    bad["scans"][0]["items"][0]["library"] = {{"rating", 9}, {"editCount", -3}, {"manualOrder", "x"}};
    writeText(t / "q.fkproj", bad.dump());
    auto q = loadProject(t / "q.fkproj");
    ASSERT_TRUE(q.ok());
    EXPECT_EQ(q->scans[0].items[0].library.rating, 5);
    EXPECT_EQ(q->scans[0].items[0].library.editCount, 0);
    EXPECT_EQ(q->scans[0].items[0].library.manualOrder, 0);
}

TEST(Library, BackupsRotateHourlyAndKeepTen) {
    TempDir t;
    auto lib = createLibrary(t.path(), "L");
    ASSERT_TRUE(lib.ok());
    ASSERT_TRUE(saveLibrary(*lib, kT0));
    EXPECT_EQ(libraryBackups(lib->folder).size(), 1u);
    ASSERT_TRUE(saveLibrary(*lib, plus(kT0, 59 * 60)));
    EXPECT_EQ(libraryBackups(lib->folder).size(), 1u) << "at most one entry per hour";
    for (int h = 1; h <= 12; ++h) {
        lib->catalog.name = "save " + std::to_string(h);
        ASSERT_TRUE(saveLibrary(*lib, plus(kT0, h * 3600)));
    }
    const auto b = libraryBackups(lib->folder);
    ASSERT_EQ(b.size(), static_cast<size_t>(kLibraryBackupsKept));
    EXPECT_EQ(loadProject(b.front())->name, "save 12") << "newest first";
    EXPECT_EQ(loadProject(b.back())->name, "save 3") << "the oldest were removed";
    EXPECT_EQ(pathToUtf8(b.front().filename()), "catalog-20261112-220000.fkproj");
    // every entry is a readable catalog
    for (const auto& f : b)
        EXPECT_TRUE(loadProject(f).ok()) << pathToUtf8(f);
}

TEST(Library, AnInterruptedSaveLeavesTheLibraryIntact) {
    TempDir t;
    auto lib = createLibrary(t.path(), "L");
    ASSERT_TRUE(lib.ok());
    lib->catalog.name = "first";
    ASSERT_TRUE(saveLibrary(*lib, kT0));
    // a save cut off before its rename leaves only a temporary file beside the catalog and the manifest
    writeText(pathFromUtf8(pathToUtf8(libraryCatalogPath(lib->folder)) + ".fk-tmp-abcdefgh"),
              "{ \"schema\": ");
    writeText(pathFromUtf8(pathToUtf8(libraryManifestPath(lib->folder)) + ".fk-tmp-abcdefgh"), "{");
    LibraryOpenInfo info;
    auto back = openLibrary(lib->folder, &info);
    ASSERT_TRUE(back.ok()) << back.error().message;
    EXPECT_EQ(back->catalog.name, "first");
    EXPECT_EQ(info.manifestSource, "file");
    EXPECT_EQ(libraryBackups(lib->folder).size(), 1u);
}

TEST(Library, ADamagedCatalogIsRestoredFromBakThenTheNewestBackupAndKeptAside) {
    TempDir t;
    auto lib = createLibrary(t.path(), "L");
    ASSERT_TRUE(lib.ok());
    lib->catalog.name = "hour 0";
    ASSERT_TRUE(saveLibrary(*lib, kT0));
    lib->catalog.name = "hour 1";
    ASSERT_TRUE(saveLibrary(*lib, plus(kT0, 3600)));
    lib->catalog.name = "hour 1, later";
    ASSERT_TRUE(saveLibrary(*lib, plus(kT0, 3700))); // .bak = "hour 1"; backups: hour 1, hour 0
    const fs::path cat = libraryCatalogPath(lib->folder);
    writeText(cat, "{ damaged");

    auto open = openLibrary(lib->folder);
    ASSERT_FALSE(open.ok());
    EXPECT_EQ(open.error().code, Errc::parse_error);
    EXPECT_NE(open.error().detail.find("catalog.fkproj.bak"), std::string::npos) << open.error().detail;
    EXPECT_EQ(slurp(cat), "{ damaged") << "opening changes nothing";

    auto cands = libraryRestoreCandidates(lib->folder);
    ASSERT_EQ(cands.size(), 3u);
    EXPECT_EQ(pathToUtf8(cands[0].filename()), "catalog.fkproj.bak");
    EXPECT_EQ(loadProject(cands[1])->name, "hour 1");
    EXPECT_EQ(loadProject(cands[2])->name, "hour 0");

    // the .bak is damaged too: the newest backup comes first
    writeText(cands[0], "also damaged");
    cands = libraryRestoreCandidates(lib->folder);
    ASSERT_EQ(cands.size(), 2u);
    EXPECT_EQ(loadProject(cands[0])->name, "hour 1");

    auto aside = restoreLibraryCatalog(lib->folder, cands[0]);
    ASSERT_TRUE(aside.ok()) << aside.error().message;
    EXPECT_EQ(slurp(*aside), "{ damaged") << "the damaged catalog is kept";
    auto back = openLibrary(lib->folder);
    ASSERT_TRUE(back.ok());
    EXPECT_EQ(back->catalog.name, "hour 1");
    EXPECT_EQ(libraryBackups(lib->folder).size(), 2u) << "restoring uses a copy, the history stays";
}

TEST(Library, TheMostRecentlySavedCopyIsOfferedFirst) {
    TempDir t;
    auto lib = createLibrary(t.path(), "L");
    ASSERT_TRUE(lib.ok());
    lib->catalog.scans.push_back(scanOf(t / "a.tif", 12));
    ASSERT_TRUE(saveLibrary(*lib, kT0)); // .bak = the empty catalog of the creation; backups: this save
    const fs::path cat = libraryCatalogPath(lib->folder);
    const fs::path bak = pathFromUtf8(pathToUtf8(cat) + ".bak");
    json b = json::parse(slurp(bak));
    b["modifiedAt"] = "2026-01-01T00:00:00Z"; // saved earlier (the clock of this test does not tick)
    writeText(bak, b.dump());
    writeText(cat, "{ damaged");
    const auto cands = libraryRestoreCandidates(lib->folder);
    ASSERT_EQ(cands.size(), 2u);
    EXPECT_EQ(loadProject(cands[0])->scans.size(), 1u)
        << "the history entry of the last save, newer than .bak";
    EXPECT_EQ(pathToUtf8(cands[1].filename()), "catalog.fkproj.bak");
}

TEST(Library, WithNoReadableCopyTheLibraryDoesNotOpenAndNothingChanges) {
    TempDir t;
    auto lib = createLibrary(t.path(), "L");
    ASSERT_TRUE(lib.ok());
    fs::remove_all(libraryBackupDir(lib->folder));
    writeText(libraryCatalogPath(lib->folder), "{ damaged");
    const auto before = fileNames(lib->folder);
    auto open = openLibrary(lib->folder);
    ASSERT_FALSE(open.ok());
    EXPECT_EQ(open.error().detail, "no readable copy to restore");
    EXPECT_EQ(fileNames(lib->folder), before);
    EXPECT_FALSE(restoreLibraryCatalog(lib->folder, libraryCatalogPath(lib->folder)).ok())
        << "a damaged copy is never restored";
}

TEST(Library, ADamagedManifestFallsBackToItsBackupThenToTheCatalog) {
    TempDir t;
    auto lib = createLibrary(t.path(), "Trip");
    ASSERT_TRUE(lib.ok());
    lib->catalog.scans.push_back(scanOf(t / "a.tif", 3));
    lib->manifest.cover = lib->catalog.scans[0].items[1].itemId;
    ASSERT_TRUE(saveLibrary(*lib));
    ASSERT_TRUE(saveLibrary(*lib)); // library.json.bak = the first save
    writeText(libraryManifestPath(lib->folder), "{");
    LibraryOpenInfo info;
    auto back = openLibrary(lib->folder, &info);
    ASSERT_TRUE(back.ok());
    EXPECT_EQ(info.manifestSource, "backup");
    EXPECT_EQ(back->manifest.libraryId, lib->manifest.libraryId);
    EXPECT_EQ(back->manifest.cover, lib->manifest.cover);

    fs::remove(pathFromUtf8(pathToUtf8(libraryManifestPath(lib->folder)) + ".bak"));
    back = openLibrary(lib->folder, &info);
    ASSERT_TRUE(back.ok());
    EXPECT_EQ(info.manifestSource, "catalog");
    EXPECT_EQ(back->manifest.name, "Trip") << "named after the folder";
    EXPECT_EQ(back->catalog.scans.size(), 1u) << "the catalog holds everything that matters";
}

TEST(Library, ASecondInstanceFindsTheLockAndAStaleLockIsTakenOverOnlyWhenAsked) {
    TempDir t;
    auto lib = createLibrary(t.path(), "L");
    ASSERT_TRUE(lib.ok());
    LockOwner a = thisLockOwner();
    LockOwner b = thisLockOwner();
    b.host = "other-machine"; // a second instance on another computer sharing the folder
    b.pid = 4242;
    EXPECT_NE(a.token, b.token);

    auto first = acquireLibraryLock(lib->folder, a, kT0);
    ASSERT_TRUE(first.ok());
    EXPECT_EQ(first->state, LockState::acquired);
    EXPECT_TRUE(fs::exists(libraryLockPath(lib->folder)));
    EXPECT_EQ(acquireLibraryLock(lib->folder, a, kT0)->state, LockState::acquired) << "already ours";

    // fresh: the second instance opens read-only and is told who holds it
    auto second = acquireLibraryLock(lib->folder, b, plus(kT0, 30));
    ASSERT_TRUE(second.ok());
    EXPECT_EQ(second->state, LockState::held);
    EXPECT_TRUE(second->lock.readable);
    EXPECT_EQ(second->lock.owner.host, a.host);
    EXPECT_EQ(second->lock.owner.user, a.user);
    EXPECT_FALSE(takeOverLibraryLock(lib->folder, b, second->lock, plus(kT0, 30))->state ==
                 LockState::acquired)
        << "a fresh lock is never taken over";

    // the heartbeat keeps it fresh
    ASSERT_TRUE(refreshLibraryLock(lib->folder, a, plus(kT0, 170)));
    EXPECT_EQ(acquireLibraryLock(lib->folder, b, plus(kT0, 300))->state, LockState::held);

    // no heartbeat for more than 3 minutes: stale — taken over only through takeOverLibraryLock
    auto later = acquireLibraryLock(lib->folder, b, plus(kT0, 170 + kLibraryLockStaleSeconds + 1));
    ASSERT_TRUE(later.ok());
    EXPECT_EQ(later->state, LockState::stale);
    EXPECT_FALSE(readLibraryLock(lib->folder, b).ours) << "finding a stale lock does not take it";
    auto took = takeOverLibraryLock(lib->folder, b, later->lock, plus(kT0, 400));
    ASSERT_TRUE(took.ok());
    EXPECT_EQ(took->state, LockState::acquired);

    // the first instance learns that it lost the library; its release leaves the new lock alone
    auto lost = refreshLibraryLock(lib->folder, a, plus(kT0, 410));
    ASSERT_FALSE(lost.ok());
    EXPECT_EQ(lost.error().code, Errc::conflict);
    releaseLibraryLock(lib->folder, a);
    EXPECT_TRUE(readLibraryLock(lib->folder, b).ours);
    releaseLibraryLock(lib->folder, b);
    EXPECT_FALSE(fs::exists(libraryLockPath(lib->folder)));
}

TEST(Library, ALockLeftByAnEndedProcessOnThisComputerIsStale) {
    TempDir t;
    auto lib = createLibrary(t.path(), "L");
    ASSERT_TRUE(lib.ok());
    LockOwner gone = thisLockOwner();
    gone.pid = 999999999; // no such process
    ASSERT_EQ(acquireLibraryLock(lib->folder, gone, kT0)->state, LockState::acquired);
    const LockOwner me = thisLockOwner();
    auto a = acquireLibraryLock(lib->folder, me, plus(kT0, 5));
    ASSERT_TRUE(a.ok());
    EXPECT_EQ(a->state, LockState::stale) << "same computer, the process ended: no need to wait 3 minutes";
    // the same heartbeat on another computer is fresh
    LockOwner elsewhere = me;
    elsewhere.host = "another";
    EXPECT_EQ(acquireLibraryLock(lib->folder, elsewhere, plus(kT0, 5))->state, LockState::held);
}

TEST(Library, ALockThatChangedSinceTheUserWasAskedIsNotTakenOver) {
    TempDir t;
    auto lib = createLibrary(t.path(), "L");
    ASSERT_TRUE(lib.ok());
    LockOwner a = thisLockOwner(), b = thisLockOwner();
    a.host = "machine-a";
    b.host = "machine-b";
    ASSERT_EQ(acquireLibraryLock(lib->folder, a, kT0)->state, LockState::acquired);
    auto seen = acquireLibraryLock(lib->folder, b, plus(kT0, 600));
    ASSERT_EQ(seen->state, LockState::stale);
    // while the question is open, the first instance wakes up and renews its lock
    ASSERT_TRUE(refreshLibraryLock(lib->folder, a, plus(kT0, 610)));
    auto took = takeOverLibraryLock(lib->folder, b, seen->lock, plus(kT0, 620));
    ASSERT_TRUE(took.ok());
    EXPECT_EQ(took->state, LockState::held);
    EXPECT_TRUE(readLibraryLock(lib->folder, a).ours);
}

TEST(Library, AnUnreadableLockCountsAsHeldUntilItIsOld) {
    TempDir t;
    auto lib = createLibrary(t.path(), "L");
    ASSERT_TRUE(lib.ok());
    writeText(libraryLockPath(lib->folder), ""); // another instance is writing it right now
    const LockOwner me = thisLockOwner();
    const std::string nowIso = isoDateTimeNowUtc();
    auto a = acquireLibraryLock(lib->folder, me, nowIso);
    ASSERT_TRUE(a.ok());
    EXPECT_EQ(a->state, LockState::held);
    EXPECT_FALSE(a->lock.readable);
    EXPECT_EQ(acquireLibraryLock(lib->folder, me, plus(nowIso, kLibraryLockStaleSeconds + 60))->state,
              LockState::stale);
}

TEST(Library, OldProjectsAreImportedAsACopyAndTheProjectIsNeverTouched) {
    TempDir t;
    fs::create_directories(t / "scans");
    fs::create_directories(t / "projects");
    Project p = newProject("Old project");
    p.scans.push_back(scanOf(t / "scans" / "a.tif", 4));
    p.scans.push_back(scanOf(t / "scans" / "b.tif", 5));
    p.scans[0].items[0].library.rating = 3;
    Sidecar rec;
    rec.projectId = p.projectId;
    rec.scanId = p.scans[0].scanId;
    rec.itemId = p.scans[0].items[0].itemId;
    rec.editVersionId = newUuid();
    rec.fileName = "a_01.tif";
    p.exportRecords.push_back(rec);
    p.unknownJson = R"({"fromALaterVersion":true})";
    const fs::path file = t / "projects" / "old.fkproj";
    ASSERT_TRUE(saveProject(p, file));
    ASSERT_TRUE(saveProject(p, file)); // with a .bak
    const std::string projectBytes = slurp(file), bakBytes = slurp(pathFromUtf8(pathToUtf8(file) + ".bak"));

    auto check = checkProjectMigration(file, t.path(), "Imported");
    ASSERT_TRUE(check.ok()) << check.error().message;
    EXPECT_EQ(check->scans, 2);
    EXPECT_EQ(check->prints, 4);
    EXPECT_EQ(check->missingScans, 0);
    EXPECT_EQ(check->exportRecords, 1);
    EXPECT_EQ(check->destination, t / "Imported.fklibrary");

    auto lib = migrateProjectToLibrary(file, t.path(), "Imported");
    ASSERT_TRUE(lib.ok()) << lib.error().message << " " << lib.error().detail;
    EXPECT_EQ(slurp(file), projectBytes) << "the project is only read";
    EXPECT_EQ(slurp(pathFromUtf8(pathToUtf8(file) + ".bak")), bakBytes);
    EXPECT_EQ(fileNames(t / "projects"), (std::vector<std::string>{"old.fkproj", "old.fkproj.bak"}));
    EXPECT_EQ(fileNames(t.path()), (std::vector<std::string>{"Imported.fklibrary", "projects", "scans"}));

    EXPECT_EQ(lib->manifest.name, "Imported");
    EXPECT_NE(lib->manifest.libraryId, p.projectId);
    EXPECT_EQ(lib->catalog.projectId, p.projectId) << "exports keep relinking through the ids they carry";
    ASSERT_EQ(lib->catalog.scans.size(), 2u);
    for (size_t i = 0; i < 2; ++i) {
        EXPECT_EQ(lib->catalog.scans[i].scanId, p.scans[i].scanId);
        EXPECT_EQ(lib->catalog.scans[i].path, p.scans[i].path) << "absolute paths kept";
        EXPECT_EQ(lib->catalog.scans[i].fingerprint, p.scans[i].fingerprint);
        EXPECT_FALSE(lib->catalog.scans[i].missing);
        ASSERT_EQ(lib->catalog.scans[i].items.size(), 2u);
        EXPECT_EQ(lib->catalog.scans[i].items[1].outer, p.scans[i].items[1].outer);
    }
    EXPECT_EQ(lib->catalog.scans[0].items[0].library.rating, 3);
    ASSERT_EQ(lib->catalog.exportRecords.size(), 1u);
    EXPECT_EQ(lib->catalog.exportRecords[0].editVersionId, rec.editVersionId);
    EXPECT_NE(lib->catalog.unknownJson.find("fromALaterVersion"), std::string::npos);
    const json c = json::parse(slurp(libraryCatalogPath(lib->folder)));
    EXPECT_EQ(c["scans"][0]["relPath"], "../scans/a.tif")
        << "relative paths rewritten for the new place, with '/'";
    ASSERT_EQ(lib->manifest.sources.size(), 1u);
    EXPECT_EQ(lib->manifest.sources[0].relPath, "../scans");
    EXPECT_EQ(libraryBackups(lib->folder).size(), 1u) << "the history starts with the imported state";
}

TEST(Library, AFailedImportLeavesNothingBehind) {
    TempDir t;
    Project p = newProject("P");
    p.scans.push_back(scanOf(t / "a.tif", 6));
    const fs::path file = t / "p.fkproj";
    ASSERT_TRUE(saveProject(p, file));
    const auto before = fileNames(t.path());
    const std::string bytes = slurp(file);
    for (const char* step : {"write", "verify", "rename"}) {
        MigrateOptions o;
        o.failAt = step;
        auto lib = migrateProjectToLibrary(file, t.path(), "New", o);
        EXPECT_FALSE(lib.ok()) << step;
        EXPECT_EQ(fileNames(t.path()), before)
            << "no library and no temporary folder after a failure at " << step;
        EXPECT_EQ(slurp(file), bytes);
    }
    EXPECT_TRUE(migrateProjectToLibrary(file, t.path(), "New").ok()) << "nothing left in the way";
}

TEST(Library, TheImportPreCheckRefusesWhatCannotBeCopiedSafely) {
    TempDir t;
    Project p = newProject("P");
    p.scans.push_back(scanOf(t / "a.tif", 7));
    p.scans.push_back(scanOf(t / "b.tif", 8));
    const fs::path file = t / "p.fkproj";
    ASSERT_TRUE(saveProject(p, file));
    fs::remove(t / "b.tif");
    auto c = checkProjectMigration(file, t.path(), "X");
    ASSERT_TRUE(c.ok());
    EXPECT_EQ(c->missingScans, 1) << "counted, not refused: the library references it as the project did";

    fs::create_directory(t / "Taken.fklibrary");
    EXPECT_EQ(checkProjectMigration(file, t.path(), "Taken").error().code, Errc::exists);
    EXPECT_EQ(checkProjectMigration(file, t / "missing", "X").error().code, Errc::io_write);

    json j = json::parse(slurp(file));
    j["schema"] = "fk.project/2";
    writeText(t / "newer.fkproj", j.dump());
    EXPECT_EQ(checkProjectMigration(t / "newer.fkproj", t.path(), "X").error().code, Errc::version_too_new);
    writeText(t / "damaged.fkproj", "{ damaged");
    EXPECT_EQ(checkProjectMigration(t / "damaged.fkproj", t.path(), "X").error().code, Errc::parse_error);
    EXPECT_FALSE(migrateProjectToLibrary(t / "damaged.fkproj", t.path(), "X").ok());
    EXPECT_FALSE(fs::exists(t / "X.fklibrary"));
}

TEST(Library, ALibraryMovedTogetherWithItsScansStillFindsThem) {
    TempDir t;
    fs::create_directories(t / "drive1" / "scans");
    auto lib = createLibrary(t / "drive1", "L");
    ASSERT_TRUE(lib.ok());
    lib->catalog.scans.push_back(scanOf(t / "drive1" / "scans" / "a.tif", 9));
    ASSERT_TRUE(saveLibrary(*lib));
    fs::rename(t / "drive1", t / "drive2"); // the absolute paths no longer exist
    auto back = openLibrary(t / "drive2" / "L.fklibrary");
    ASSERT_TRUE(back.ok());
    ASSERT_EQ(back->catalog.scans.size(), 1u);
    EXPECT_FALSE(back->catalog.scans[0].missing);
    EXPECT_EQ(fs::path(pathFromUtf8(back->catalog.scans[0].path)),
              fs::weakly_canonical(t / "drive2" / "scans" / "a.tif"));

    // a catalog written on Windows before 0.8 (backslashes) resolves on every system
    json c = json::parse(slurp(libraryCatalogPath(t / "drive2" / "L.fklibrary")));
    c["scans"][0]["path"] = "C:\\gone\\a.tif";
    c["scans"][0]["relPath"] = "..\\scans\\a.tif";
    writeText(libraryCatalogPath(t / "drive2" / "L.fklibrary"), c.dump());
    back = openLibrary(t / "drive2" / "L.fklibrary");
    ASSERT_TRUE(back.ok());
    EXPECT_FALSE(back->catalog.scans[0].missing);
}

TEST(Library, TwoLibrariesShareNothing) {
    TempDir t;
    auto a = createLibrary(t.path(), "A");
    auto b = createLibrary(t.path(), "B");
    ASSERT_TRUE(a.ok() && b.ok());
    EXPECT_NE(a->manifest.libraryId, b->manifest.libraryId);
    EXPECT_NE(a->catalog.projectId, b->catalog.projectId);
    const ScanRecord s = scanOf(t / "shared.tif", 10);
    a->catalog.scans.push_back(s);
    b->catalog.scans.push_back(s);
    a->catalog.scans[0].items[0].library.rating = 5;
    ASSERT_TRUE(saveLibrary(*b));
    const std::string bCatalog = slurp(libraryCatalogPath(b->folder)),
                      bManifest = slurp(libraryManifestPath(b->folder));
    ASSERT_TRUE(saveLibrary(*a));
    LockOwner me = thisLockOwner();
    ASSERT_EQ(acquireLibraryLock(a->folder, me)->state, LockState::acquired);
    EXPECT_FALSE(fs::exists(libraryLockPath(b->folder))) << "a lock covers one library";
    EXPECT_EQ(acquireLibraryLock(b->folder, me)->state, LockState::acquired);
    EXPECT_EQ(slurp(libraryCatalogPath(b->folder)), bCatalog) << "saving A never writes B";
    EXPECT_EQ(slurp(libraryManifestPath(b->folder)), bManifest);
    EXPECT_EQ(openLibrary(b->folder)->catalog.scans[0].items[0].library.rating, 0);
    EXPECT_EQ(openLibrary(a->folder)->catalog.scans[0].items[0].library.rating, 5);
    releaseLibraryLock(a->folder, me);
    releaseLibraryLock(b->folder, me);
}

TEST(Library, DeletingTheCacheOnlyCostsTime) {
    TempDir t;
    auto lib = createLibrary(t.path(), "L");
    ASSERT_TRUE(lib.ok());
    lib->catalog.scans.push_back(scanOf(t / "a.tif", 11));
    lib->catalog.scans[0].items[0].library.rating = 2;
    ASSERT_TRUE(saveLibrary(*lib));
    fs::create_directories(libraryCacheDir(lib->folder) / "thumbs");
    writeText(libraryCacheDir(lib->folder) / "thumbs" / "x.bin", "rebuildable");
    const std::string catalog = slurp(libraryCatalogPath(lib->folder));
    fs::remove_all(libraryCacheDir(lib->folder));
    auto back = openLibrary(lib->folder);
    ASSERT_TRUE(back.ok());
    EXPECT_EQ(back->catalog.scans[0].items[0].library.rating, 2);
    ASSERT_TRUE(saveLibrary(*back));
    auto again = openLibrary(lib->folder);
    ASSERT_TRUE(again.ok());
    EXPECT_EQ(projectToJson(again->catalog).size(), projectToJson(back->catalog).size());
    EXPECT_EQ(json::parse(catalog)["scans"], json::parse(slurp(libraryCatalogPath(lib->folder)))["scans"]);
}

// U019: the home page reads library.json only
TEST(Library, TheHomePageReadsCountsAndTimesWithoutTheCatalog) {
    TempDir t;
    auto lib = createLibrary(t.path(), "Cards");
    ASSERT_TRUE(lib.ok());
    auto fresh = readLibraryManifest(lib->folder);
    ASSERT_TRUE(fresh.ok());
    EXPECT_EQ(fresh->scanCount, -1) << "not recorded before the first save";
    lib->catalog.scans.push_back(scanOf(t / "a.tif", 13));
    lib->catalog.scans.push_back(scanOf(t / "b.tif", 14));
    lib->catalog.scans[1].items.pop_back();
    ASSERT_TRUE(saveLibrary(*lib, kT0));
    writeText(libraryCatalogPath(lib->folder), "{ damaged"); // the card does not need the catalog
    auto m = readLibraryManifest(lib->folder);
    ASSERT_TRUE(m.ok()) << m.error().message;
    EXPECT_EQ(m->name, "Cards");
    EXPECT_EQ(m->scanCount, 2);
    EXPECT_EQ(m->printCount, 3);
    EXPECT_EQ(m->modifiedAt, kT0);
    const std::string bak = slurp(pathFromUtf8(pathToUtf8(libraryManifestPath(lib->folder)) + ".bak"));
    ASSERT_TRUE(touchLibraryOpened(lib->folder, plus(kT0, 600)));
    EXPECT_EQ(readLibraryManifest(lib->folder)->openedAt, plus(kT0, 600));
    EXPECT_EQ(readLibraryManifest(lib->folder)->scanCount, 2) << "only openedAt changes";
    EXPECT_EQ(slurp(pathFromUtf8(pathToUtf8(libraryManifestPath(lib->folder)) + ".bak")), bak)
        << "opening does not rotate the manifest's backup";
    EXPECT_EQ(slurp(libraryCatalogPath(lib->folder)), "{ damaged") << "the catalog is not touched";
    writeText(libraryManifestPath(lib->folder), "{");
    EXPECT_TRUE(readLibraryManifest(lib->folder).ok()) << "falls back to library.json.bak";
    EXPECT_FALSE(readLibraryManifest(t / "gone.fklibrary").ok());
}

TEST(Library, RenamingKeepsTheFolderAndWaitsForAnotherInstance) {
    TempDir t;
    auto lib = createLibrary(t.path(), "Old name");
    ASSERT_TRUE(lib.ok());
    const LockOwner me = thisLockOwner();
    ASSERT_TRUE(renameLibrary(lib->folder, "新名字", me));
    EXPECT_EQ(readLibraryManifest(lib->folder)->name, "新名字");
    EXPECT_TRUE(fs::exists(t / "Old name.fklibrary")) << "the folder keeps its name";
    EXPECT_EQ(openLibrary(lib->folder)->manifest.name, "新名字");
    LockOwner other = thisLockOwner();
    other.host = "elsewhere";
    ASSERT_EQ(acquireLibraryLock(lib->folder, other)->state, LockState::acquired);
    auto r = renameLibrary(lib->folder, "Third", me);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, Errc::conflict);
    EXPECT_EQ(readLibraryManifest(lib->folder)->name, "新名字");
    ASSERT_TRUE(renameLibrary(lib->folder, "Third", other)) << "the instance that holds it may rename";
    EXPECT_EQ(readLibraryManifest(lib->folder)->name, "Third");
}
