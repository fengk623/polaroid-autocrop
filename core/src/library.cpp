// Libraries (RFC-0018, PRD F16, docs/project-format.md §Libraries).
#include "fk/library.hpp"

#include "fk/image_io.hpp"
#include "fk/metadata.hpp"
#include "fk/product_info.hpp"
#include "fk/util.hpp"
#include "sink_base.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fk {

using json = nlohmann::ordered_json;
namespace fs = std::filesystem;

namespace {

constexpr const char* kManifestFile = "library.json";
constexpr const char* kCatalogFile = "catalog.fkproj";
constexpr const char* kLockFile = "library.lock";
constexpr const char* kBackupDir = "backups";
constexpr const char* kCacheDir = "cache";
constexpr const char* kBackupPrefix = "catalog-";

std::string now(const std::string& nowUtc) {
    return nowUtc.empty() ? isoDateTimeNowUtc() : nowUtc;
}

// 2026-11-12T21:58:03Z → 20261112-215803 (file names: sortable, no ':' for Windows)
std::string stampOf(const std::string& iso) {
    std::string s;
    for (char c : iso)
        if (std::isdigit(static_cast<unsigned char>(c))) s += c;
    if (s.size() < 14) return s;
    return s.substr(0, 8) + "-" + s.substr(8, 6);
}
int64_t epochOfStamp(const std::string& stamp) { // inverse of stampOf
    if (stamp.size() < 15 || stamp[8] != '-') return -1;
    const std::string iso = stamp.substr(0, 4) + "-" + stamp.substr(4, 2) + "-" + stamp.substr(6, 2) + "T" +
                            stamp.substr(9, 2) + ":" + stamp.substr(11, 2) + ":" + stamp.substr(13, 2) + "Z";
    return isoUtcToEpoch(iso);
}

std::string genericRelative(const fs::path& target, const fs::path& base) {
    std::error_code ec;
    const fs::path rel = fs::relative(target, base, ec);
    if (ec || rel.empty()) return {};
    const auto g = rel.generic_u8string();
    return std::string(reinterpret_cast<const char*>(g.data()), g.size());
}

int schemaNumber(const std::string& schema, const std::string& prefix) {
    if (schema.rfind(prefix, 0) != 0) return -1;
    try {
        return std::stoi(schema.substr(prefix.size()));
    } catch (...) {
        return -1;
    }
}

// ---- manifest
// -----------------------------------------------------------------------------------------------
const std::vector<std::string> kManifestKeys = {"schema",     "libraryId", "name",      "cover",
                                                "sources",    "sort",      "createdAt", "openedAt",
                                                "modifiedAt", "counts",    "app"};

std::string manifestToJson(const LibraryManifest& m) {
    json j;
    j["schema"] = "fk.library/" + std::to_string(kLibrarySchemaVersion);
    j["libraryId"] = m.libraryId;
    j["name"] = m.name;
    j["cover"] = m.cover;
    j["sources"] = json::array();
    for (const auto& s : m.sources)
        j["sources"].push_back({{"path", s.path}, {"relPath", s.relPath}});
    j["sort"] = json::object();
    if (!m.sortJson.empty()) {
        try {
            const json s = json::parse(m.sortJson);
            if (s.is_object()) j["sort"] = s;
        } catch (...) {
        }
    }
    j["createdAt"] = m.createdAt;
    j["openedAt"] = m.openedAt;
    j["modifiedAt"] = m.modifiedAt;
    if (m.scanCount >= 0) j["counts"] = {{"scans", m.scanCount}, {"prints", m.printCount}};
    j["app"] = {{"name", product::kName}, {"version", product::kVersion}};
    if (!m.unknownJson.empty()) {
        try {
            const json extra = json::parse(m.unknownJson);
            for (auto it = extra.begin(); it != extra.end(); ++it)
                if (!j.contains(it.key())) j[it.key()] = it.value();
        } catch (...) {
        }
    }
    return j.dump(2);
}

Result<LibraryManifest> manifestFromJson(const std::string& text, OpenMode* mode) {
    json j;
    try {
        j = json::parse(text);
    } catch (const std::exception& e) {
        return makeError(Errc::parse_error, "The library file is damaged", e.what());
    }
    try {
        if (!j.is_object()) return makeError(Errc::parse_error, "The library file is damaged");
        const int v = schemaNumber(j.value("schema", ""), "fk.library/");
        if (v < 1) return makeError(Errc::parse_error, "Not a FrameKeeper library file");
        if (mode) *mode = v > kLibrarySchemaVersion ? OpenMode::read_only : OpenMode::read_write;
        LibraryManifest m;
        m.libraryId = j.at("libraryId").get<std::string>();
        m.name = j.value("name", "");
        m.cover = j.value("cover", "");
        for (const auto& s : j.value("sources", json::array()))
            if (s.is_object()) m.sources.push_back({s.value("path", ""), s.value("relPath", "")});
        if (j.contains("sort") && j["sort"].is_object() && !j["sort"].empty()) m.sortJson = j["sort"].dump();
        m.createdAt = j.value("createdAt", "");
        m.openedAt = j.value("openedAt", "");
        m.modifiedAt = j.value("modifiedAt", "");
        if (j.contains("counts") && j["counts"].is_object()) {
            const json sc = j["counts"].value("scans", json(-1)), pc = j["counts"].value("prints", json(-1));
            m.scanCount = sc.is_number_integer() ? sc.get<int>() : -1;
            m.printCount = pc.is_number_integer() ? pc.get<int>() : -1;
        }
        if (j.contains("app") && j["app"].is_object() && j["app"].contains("version") &&
            j["app"]["version"].is_string())
            m.savedBy = j["app"]["version"].get<std::string>();
        json extra = json::object();
        for (auto it = j.begin(); it != j.end(); ++it)
            if (std::find(kManifestKeys.begin(), kManifestKeys.end(), it.key()) == kManifestKeys.end())
                extra[it.key()] = it.value();
        if (!extra.empty()) m.unknownJson = extra.dump();
        return m;
    } catch (const std::exception& e) {
        return makeError(Errc::parse_error, "The library file is damaged", e.what());
    }
}

Result<LibraryManifest> readManifest(const fs::path& file, OpenMode* mode) {
    auto text = readTextFile(file);
    if (!text) return text.error();
    return manifestFromJson(*text, mode);
}

std::string folderStem(const fs::path& folder) {
    std::string n = pathToUtf8(folder.filename());
    const std::string ext = std::string(".") + product::kLibraryExt;
    if (n.size() > ext.size() && n.compare(n.size() - ext.size(), ext.size(), ext) == 0)
        n.resize(n.size() - ext.size());
    return n;
}

bool containsId(const Project& p, const std::string& id) {
    for (const auto& s : p.scans) {
        if (s.scanId == id) return true;
        for (const auto& it : s.items)
            if (it.itemId == id) return true;
    }
    return false;
}

// ---- backups
// ------------------------------------------------------------------------------------------------
// `catalog-YYYYMMDD-HHMMSS[-n].fkproj`, newest first by name (the stamp sorts as text)
std::vector<fs::path> backupFiles(const fs::path& folder) {
    std::vector<fs::path> out;
    std::error_code ec;
    const fs::path dir = libraryBackupDir(folder);
    if (!fs::is_directory(dir, ec)) return out;
    const std::string ext = std::string(".") + product::kProjectExt;
    for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        const std::string n = pathToUtf8(it->path().filename());
        if (n.rfind(kBackupPrefix, 0) == 0 && n.size() > ext.size() &&
            n.compare(n.size() - ext.size(), ext.size(), ext) == 0 && it->is_regular_file(ec))
            out.push_back(it->path());
    }
    std::sort(out.begin(), out.end(), [](const fs::path& a, const fs::path& b) {
        return pathToUtf8(a.filename()) > pathToUtf8(b.filename());
    });
    return out;
}

int64_t backupEpoch(const fs::path& file) {
    const std::string n = pathToUtf8(file.filename());
    return epochOfStamp(n.substr(std::strlen(kBackupPrefix), 15));
}

bool readsAsProject(const fs::path& file) {
    auto text = readTextFile(file);
    return text && projectFromJson(*text).ok();
}

Status rotateBackups(const fs::path& folder, const std::string& nowIso) {
    std::error_code ec;
    fs::create_directories(libraryBackupDir(folder), ec);
    if (ec) return makeError(Errc::io_write, "Cannot create the library's backup folder", ec.message());
    const auto existing = backupFiles(folder);
    const int64_t t = isoUtcToEpoch(nowIso);
    if (!existing.empty()) {
        const int64_t last = backupEpoch(existing.front());
        if (last >= 0 && t >= 0 && t - last < kLibraryBackupIntervalSeconds) return {};
    }
    auto text = readTextFile(libraryCatalogPath(folder));
    if (!text) return text.error();
    const std::string ext = std::string(".") + product::kProjectExt;
    fs::path target = libraryBackupDir(folder) / (std::string(kBackupPrefix) + stampOf(nowIso) + ext);
    for (int i = 2; fs::exists(target, ec); ++i)
        target = libraryBackupDir(folder) /
                 (std::string(kBackupPrefix) + stampOf(nowIso) + "-" + std::to_string(i) + ext);
    FK_TRY(writeFileAtomic(target, *text, false));
    // the oldest go only after the new entry is written
    const auto all = backupFiles(folder);
    for (size_t i = kLibraryBackupsKept; i < all.size(); ++i)
        fs::remove(all[i], ec);
    return {};
}

// ---- lock
// ---------------------------------------------------------------------------------------------------
std::string lockToJson(const LockOwner& o, const std::string& startedAt, const std::string& heartbeatAt) {
    json j{
        {"schema", "fk.lock/1"}, {"pid", o.pid},     {"host", o.host},         {"user", o.user},
        {"app", o.app},          {"token", o.token}, {"startedAt", startedAt}, {"heartbeatAt", heartbeatAt}};
    return j.dump(2);
}

bool processAlive(int64_t pid) {
    if (pid <= 0) return false;
#if defined(_WIN32)
    HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!h) return GetLastError() == ERROR_ACCESS_DENIED; // exists, not ours to query
    const bool alive = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
    CloseHandle(h);
    return alive;
#else
    return ::kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
#endif
}

int64_t fileAgeEpoch(const fs::path& p) {
    std::error_code ec;
    const auto ft = fs::last_write_time(p, ec);
    if (ec) return -1;
    // portable file_clock → system_clock (std::chrono::clock_cast is missing from Apple's libc++, T16)
    const auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ft - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    return std::chrono::duration_cast<std::chrono::seconds>(sys.time_since_epoch()).count();
}

/// Creates `p` only when it does not exist yet (O_EXCL / _O_EXCL) and writes `content` into it.
/// 1 = written, 0 = it already exists, −1 = another error (errno text in `why`).
int createExclusive(const fs::path& p, const std::string& content, std::string& why) {
#if defined(_WIN32)
    const int fd =
        _wopen(p.wstring().c_str(), _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY, _S_IREAD | _S_IWRITE);
    if (fd < 0) {
        if (errno == EEXIST) return 0;
        why = std::strerror(errno);
        return -1;
    }
    const bool ok =
        _write(fd, content.data(), static_cast<unsigned>(content.size())) == static_cast<int>(content.size());
    _commit(fd);
    _close(fd);
#else
    const int fd = ::open(p.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0644);
    if (fd < 0) {
        if (errno == EEXIST) return 0;
        why = std::strerror(errno);
        return -1;
    }
    const bool ok = ::write(fd, content.data(), content.size()) == static_cast<ssize_t>(content.size());
    ::fsync(fd);
    ::close(fd);
#endif
    if (!ok) {
        std::error_code ec;
        fs::remove(p, ec);
        why = "writing the lock failed";
        return -1;
    }
    return 1;
}

std::string hostName() {
#if defined(_WIN32)
    wchar_t buf[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD n = MAX_COMPUTERNAME_LENGTH + 1;
    if (GetComputerNameW(buf, &n)) return pathToUtf8(fs::path(std::wstring(buf, n)));
    return {};
#else
    char buf[256] = {};
    if (::gethostname(buf, sizeof(buf) - 1) == 0) return buf;
    return {};
#endif
}

std::string userName() {
#if defined(_WIN32)
    if (const wchar_t* u = _wgetenv(L"USERNAME")) return pathToUtf8(fs::path(std::wstring(u)));
    return {};
#else
    if (const char* u = std::getenv("USER"); u && *u) return u;
    if (const passwd* pw = ::getpwuid(::getuid())) return pw->pw_name;
    return {};
#endif
}

int64_t currentPid() {
#if defined(_WIN32)
    return static_cast<int64_t>(GetCurrentProcessId());
#else
    return static_cast<int64_t>(::getpid());
#endif
}

// ---- migration
// ---------------------------------------------------------------------------------------------- The parts of
// a project that must read back identically: everything but the name, the save time, the writing app and the
// relative paths (rewritten for the new place).
json comparable(const Project& p) {
    json j = json::parse(projectToJson(p));
    for (const char* k : {"name", "modifiedAt", "app"})
        j.erase(k);
    for (auto& s : j["scans"])
        s.erase("relPath");
    return j;
}

} // namespace

// ---- time
// ---------------------------------------------------------------------------------------------------
int64_t isoUtcToEpoch(const std::string& iso) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    char z = 0;
    if (std::sscanf(iso.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d%c", &y, &mo, &d, &h, &mi, &s, &z) != 7 || z != 'Z')
        return -1;
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || s > 60) return -1;
    // days from civil (H. Hinnant)
    const int64_t yy = y - (mo <= 2 ? 1 : 0);
    const int64_t era = (yy >= 0 ? yy : yy - 399) / 400;
    const int64_t yoe = yy - era * 400;
    const int64_t doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const int64_t days = era * 146097 + doe - 719468;
    return days * 86400 + h * 3600 + mi * 60 + s;
}

std::string epochToIsoUtc(int64_t t) {
    int64_t days = t >= 0 ? t / 86400 : (t - 86399) / 86400;
    int64_t sec = t - days * 86400;
    days += 719468;
    const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const int64_t doe = days - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    const int64_t d = doy - (153 * mp + 2) / 5 + 1;
    const int64_t m = mp < 10 ? mp + 3 : mp - 9;
    const int64_t y = yoe + era * 400 + (m <= 2 ? 1 : 0);
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%04lld-%02lld-%02lldT%02lld:%02lld:%02lldZ", static_cast<long long>(y),
                  static_cast<long long>(m), static_cast<long long>(d), static_cast<long long>(sec / 3600),
                  static_cast<long long>(sec / 60 % 60), static_cast<long long>(sec % 60));
    return buf;
}

// ---- layout
// -------------------------------------------------------------------------------------------------
fs::path libraryManifestPath(const fs::path& folder) {
    return folder / kManifestFile;
}
fs::path libraryCatalogPath(const fs::path& folder) {
    return folder / kCatalogFile;
}
fs::path libraryLockPath(const fs::path& folder) {
    return folder / kLockFile;
}
fs::path libraryBackupDir(const fs::path& folder) {
    return folder / kBackupDir;
}
fs::path libraryCacheDir(const fs::path& folder) {
    return folder / kCacheDir;
}

bool isLibraryFolder(const fs::path& folder) {
    std::error_code ec;
    return fs::is_directory(folder, ec) && (fs::is_regular_file(libraryManifestPath(folder), ec) ||
                                            fs::is_regular_file(libraryCatalogPath(folder), ec));
}

std::string libraryFolderName(const std::string& name) {
    std::string n;
    for (unsigned char c : name)
        n += (c < 0x20 || std::strchr("<>:\"/\\|?*", c)) ? '_' : static_cast<char>(c);
    while (!n.empty() && (n.back() == '.' || n.back() == ' '))
        n.pop_back();
    while (!n.empty() && n.front() == ' ')
        n.erase(n.begin());
    if (n.empty()) n = "Library";
    std::string upper;
    for (char c : n.substr(0, n.find('.')))
        upper += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    static const std::set<std::string> reserved = {
        "CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7",
        "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
    if (reserved.count(upper)) n += "_";
    return n + "." + product::kLibraryExt;
}

// ---- create, open, save
// -------------------------------------------------------------------------------------
Result<Library> createLibrary(const fs::path& parent, const std::string& name) {
    std::error_code ec;
    if (!fs::is_directory(parent, ec))
        return makeError(Errc::io_write, "The folder for the new library does not exist", pathToUtf8(parent));
    const fs::path dest = parent / pathFromUtf8(libraryFolderName(name));
    if (fs::exists(dest, ec))
        return makeError(Errc::exists, "A folder with this name already exists", pathToUtf8(dest));
    const fs::path tmp = parent / pathFromUtf8("." + libraryFolderName(name) + ".fk-tmp-" + randomToken(8));
    if (!fs::create_directory(tmp, ec) || ec)
        return makeError(Errc::io_write, "Cannot create the library folder", ec.message());
    auto fail = [&](Error e) -> Result<Library> {
        std::error_code ignored;
        fs::remove_all(tmp, ignored); // only the folder made here
        return e;
    };
    Library lib;
    const std::string t = isoDateTimeNowUtc();
    lib.manifest.libraryId = newUuid();
    lib.manifest.name = name;
    lib.manifest.createdAt = lib.manifest.openedAt = lib.manifest.modifiedAt = t;
    lib.catalog = newProject(name);
    fs::create_directory(libraryBackupDir(tmp), ec);
    if (auto st = saveProject(lib.catalog, libraryCatalogPath(tmp)); !st) return fail(st.error());
    if (auto st = writeFileAtomic(libraryManifestPath(tmp), manifestToJson(lib.manifest), false); !st)
        return fail(st.error());
    fs::rename(tmp, dest, ec);
    if (ec) return fail(makeError(Errc::io_write, "Cannot move the new library into place", ec.message()));
    lib.folder = dest;
    lib.manifest.savedBy = product::kVersion;
    return lib;
}

Result<Library> openLibrary(const fs::path& folder, LibraryOpenInfo* info) {
    std::error_code ec;
    if (!fs::is_directory(folder, ec))
        return makeError(Errc::not_found, "Library not found", pathToUtf8(folder));
    LibraryOpenInfo local;
    LibraryOpenInfo& in = info ? *info : local;
    in = {};
    OpenMode catalogMode = OpenMode::read_write;
    auto catalog = loadProject(libraryCatalogPath(folder), &catalogMode);
    if (!catalog) {
        const Errc code = catalog.error().code;
        if (code != Errc::not_found && code != Errc::parse_error) return catalog.error();
        const auto candidates = libraryRestoreCandidates(folder);
        const std::string detail =
            candidates.empty() ? std::string("no readable copy to restore")
                               : "a copy can be restored: " + pathToUtf8(candidates.front().filename());
        if (code == Errc::not_found)
            return makeError(Errc::parse_error, "The library has no catalog", detail);
        return makeError(Errc::parse_error, "The library's catalog is damaged", detail);
    }
    Library lib;
    lib.folder = folder;
    lib.catalog = std::move(*catalog);
    OpenMode manifestMode = OpenMode::read_write;
    auto m = readManifest(libraryManifestPath(folder), &manifestMode);
    if (!m) {
        fs::path bak = libraryManifestPath(folder);
        bak += ".bak";
        m = readManifest(bak, &manifestMode);
        in.manifestSource = "backup";
    }
    if (m) {
        lib.manifest = std::move(*m);
    } else {
        // nothing unique lives only in the manifest except cover, sources and sort, which are lost
        in.manifestSource = "catalog";
        manifestMode = OpenMode::read_write;
        lib.manifest.libraryId = newUuid();
        lib.manifest.name = folderStem(folder);
        lib.manifest.createdAt = lib.catalog.createdAt;
        lib.manifest.modifiedAt = lib.catalog.modifiedAt;
    }
    if (!lib.manifest.cover.empty() && !containsId(lib.catalog, lib.manifest.cover))
        lib.manifest.cover.clear();
    in.mode = (catalogMode == OpenMode::read_only || manifestMode == OpenMode::read_only)
                  ? OpenMode::read_only
                  : OpenMode::read_write;
    return lib;
}

Status saveLibrary(Library& lib, const std::string& nowUtc) {
    std::error_code ec;
    if (!fs::is_directory(lib.folder, ec))
        return makeError(Errc::not_found, "The library folder is gone", pathToUtf8(lib.folder));
    const std::string t = now(nowUtc);
    FK_TRY(saveProject(lib.catalog, libraryCatalogPath(lib.folder)));
    lib.catalog.modifiedAt = t;
    FK_TRY(rotateBackups(lib.folder, t));
    if (!lib.manifest.cover.empty() && !containsId(lib.catalog, lib.manifest.cover))
        lib.manifest.cover.clear();
    for (auto& s : lib.manifest.sources)
        if (auto rel = genericRelative(pathFromUtf8(s.path), lib.folder); !rel.empty()) s.relPath = rel;
    lib.manifest.scanCount = static_cast<int>(lib.catalog.scans.size());
    lib.manifest.printCount = 0;
    for (const auto& sc : lib.catalog.scans)
        lib.manifest.printCount += static_cast<int>(sc.items.size());
    LibraryManifest m = lib.manifest;
    m.modifiedAt = t;
    FK_TRY(writeFileAtomic(libraryManifestPath(lib.folder), manifestToJson(m), true));
    lib.manifest.modifiedAt = t;
    lib.manifest.savedBy = product::kVersion;
    return {};
}

// ---- home page
// ----------------------------------------------------------------------------------------------
Result<LibraryManifest> readLibraryManifest(const fs::path& folder) {
    std::error_code ec;
    if (!fs::is_directory(folder, ec))
        return makeError(Errc::not_found, "Library not found", pathToUtf8(folder));
    auto m = readManifest(libraryManifestPath(folder), nullptr);
    if (m) return m;
    fs::path bak = libraryManifestPath(folder);
    bak += ".bak";
    auto b = readManifest(bak, nullptr);
    if (b) return b;
    return m.error();
}

Status touchLibraryOpened(const fs::path& folder, const std::string& nowUtc) {
    auto text = readTextFile(libraryManifestPath(folder));
    if (!text) return text.error();
    OpenMode mode = OpenMode::read_write;
    auto m = manifestFromJson(*text, &mode);
    if (!m) return m.error();
    if (mode == OpenMode::read_only) return {}; // a later version's file is never rewritten
    m->openedAt = now(nowUtc);
    return writeFileAtomic(libraryManifestPath(folder), manifestToJson(*m), false);
}

Status writeLibrarySort(const fs::path& folder, const std::string& sortJson) {
    auto text = readTextFile(libraryManifestPath(folder));
    if (!text) return text.error();
    OpenMode mode = OpenMode::read_write;
    auto m = manifestFromJson(*text, &mode);
    if (!m) return m.error();
    if (mode == OpenMode::read_only) return {}; // a later version's file is never rewritten
    m->sortJson = sortJson;
    return writeFileAtomic(libraryManifestPath(folder), manifestToJson(*m), false);
}

Status renameLibrary(const fs::path& folder, const std::string& name, const LockOwner& me) {
    const LibraryLock l = readLibraryLock(folder, me);
    if (l.present && !l.ours && !l.stale)
        return makeError(Errc::conflict, "The library is open in another FrameKeeper", l.owner.host);
    auto text = readTextFile(libraryManifestPath(folder));
    if (!text) return text.error();
    OpenMode mode = OpenMode::read_write;
    auto m = manifestFromJson(*text, &mode);
    if (!m) return m.error();
    if (mode == OpenMode::read_only)
        return makeError(Errc::version_too_new, "The library was saved by a newer version");
    m->name = name;
    return writeFileAtomic(libraryManifestPath(folder), manifestToJson(*m), true);
}

// ---- backups and restore
// ------------------------------------------------------------------------------------
std::vector<fs::path> libraryBackups(const fs::path& folder) {
    std::vector<fs::path> out;
    for (const auto& f : backupFiles(folder))
        if (readsAsProject(f)) out.push_back(f);
    return out;
}

std::vector<fs::path> libraryRestoreCandidates(const fs::path& folder) {
    std::vector<fs::path> out;
    if (auto bak = usableBackup(libraryCatalogPath(folder))) out.push_back(*bak);
    for (const auto& f : libraryBackups(folder))
        out.push_back(f);
    // the most recent save first: a history entry is a copy of the catalog as saved, so the newest one can be
    // newer than `.bak` (the save before the last); on equal save times `.bak` comes first
    std::vector<std::pair<int64_t, fs::path>> keyed;
    for (const auto& f : out) {
        auto text = readTextFile(f);
        auto p = text ? projectFromJson(*text) : Result<Project>(text.error());
        keyed.emplace_back(p ? isoUtcToEpoch(p->modifiedAt) : -1, f);
    }
    std::stable_sort(keyed.begin(), keyed.end(),
                     [](const auto& a, const auto& b) { return a.first > b.first; });
    out.clear();
    for (auto& k : keyed)
        out.push_back(std::move(k.second));
    return out;
}

Result<fs::path> restoreLibraryCatalog(const fs::path& folder, const fs::path& from) {
    auto text = readTextFile(from);
    if (!text) return text.error();
    if (!projectFromJson(*text))
        return makeError(Errc::parse_error, "The copy to restore is not readable", pathToUtf8(from));
    std::error_code ec;
    fs::path aside;
    if (fs::exists(libraryCatalogPath(folder), ec)) {
        auto a = setAsideDamaged(libraryCatalogPath(folder));
        if (!a) return a.error();
        aside = *a;
    }
    FK_TRY(writeFileAtomic(libraryCatalogPath(folder), *text, false));
    return aside;
}

// ---- lock
// ---------------------------------------------------------------------------------------------------
LockOwner thisLockOwner() {
    LockOwner o;
    o.pid = currentPid();
    o.host = hostName();
    o.user = userName();
    o.app = std::string(product::kName) + " " + product::kVersion;
    o.token = newUuid();
    return o;
}

const char* lockStateName(LockState s) {
    switch (s) {
    case LockState::acquired:
        return "acquired";
    case LockState::held:
        return "held";
    case LockState::stale:
        return "stale";
    }
    return "held";
}

LibraryLock readLibraryLock(const fs::path& folder, const LockOwner& me, const std::string& nowUtc) {
    LibraryLock l;
    const fs::path p = libraryLockPath(folder);
    std::error_code ec;
    if (!fs::exists(p, ec)) return l;
    l.present = true;
    const int64_t t = isoUtcToEpoch(now(nowUtc));
    auto text = readTextFile(p);
    json j;
    try {
        if (text) j = json::parse(*text);
    } catch (...) {
    }
    if (!j.is_object() || !j.contains("token")) {
        // being written right now, or damaged: stale only once nobody has touched it for a while
        const int64_t age = fileAgeEpoch(p);
        l.stale = age >= 0 && t >= 0 && t - age > kLibraryLockStaleSeconds;
        return l;
    }
    l.readable = true;
    l.owner.pid = j.value("pid", int64_t{0});
    l.owner.host = j.value("host", "");
    l.owner.user = j.value("user", "");
    l.owner.app = j.value("app", "");
    l.owner.token = j.value("token", "");
    l.startedAt = j.value("startedAt", "");
    l.heartbeatAt = j.value("heartbeatAt", "");
    l.ours = !me.token.empty() && l.owner.token == me.token;
    if (l.ours) return l;
    const int64_t beat = isoUtcToEpoch(l.heartbeatAt);
    const bool silent = beat < 0 || (t >= 0 && t - beat > kLibraryLockStaleSeconds);
    const bool ended = !l.owner.host.empty() && l.owner.host == me.host && !processAlive(l.owner.pid);
    l.stale = silent || ended;
    return l;
}

Result<LockAttempt> acquireLibraryLock(const fs::path& folder, const LockOwner& me,
                                       const std::string& nowUtc) {
    const std::string t = now(nowUtc);
    std::string why;
    const int r = createExclusive(libraryLockPath(folder), lockToJson(me, t, t), why);
    if (r < 0) return makeError(Errc::io_write, "The library folder cannot be written", why);
    LockAttempt a;
    a.lock = readLibraryLock(folder, me, t);
    if (r == 1 || a.lock.ours)
        a.state = LockState::acquired;
    else
        a.state = a.lock.stale ? LockState::stale : LockState::held;
    return a;
}

Status refreshLibraryLock(const fs::path& folder, const LockOwner& me, const std::string& nowUtc) {
    const std::string t = now(nowUtc);
    const LibraryLock l = readLibraryLock(folder, me, t);
    if (!l.ours) return makeError(Errc::conflict, "Another app instance took over this library");
    return writeFileAtomic(libraryLockPath(folder), lockToJson(me, l.startedAt, t), false);
}

Result<LockAttempt> takeOverLibraryLock(const fs::path& folder, const LockOwner& me, const LibraryLock& seen,
                                        const std::string& nowUtc) {
    const std::string t = now(nowUtc);
    LockAttempt a;
    a.lock = readLibraryLock(folder, me, t);
    if (a.lock.ours) {
        a.state = LockState::acquired;
        return a;
    }
    if (!a.lock.present) return acquireLibraryLock(folder, me, t); // released meanwhile
    // the user was asked about a particular lock: a lock renewed or replaced since is asked about again
    const bool same = a.lock.owner.token == seen.owner.token && a.lock.heartbeatAt == seen.heartbeatAt;
    if (!a.lock.stale || !same) {
        a.state = a.lock.stale ? LockState::stale : LockState::held;
        return a;
    }
    FK_TRY(writeFileAtomic(libraryLockPath(folder), lockToJson(me, t, t), false));
    a.lock =
        readLibraryLock(folder, me, t); // another instance taking over at the same moment wins or loses here
    a.state = a.lock.ours ? LockState::acquired : (a.lock.stale ? LockState::stale : LockState::held);
    return a;
}

void releaseLibraryLock(const fs::path& folder, const LockOwner& me) {
    if (!readLibraryLock(folder, me).ours) return;
    std::error_code ec;
    fs::remove(libraryLockPath(folder), ec);
}

// ---- migration
// ----------------------------------------------------------------------------------------------
Result<MigrationCheck> checkProjectMigration(const fs::path& project, const fs::path& parent,
                                             const std::string& name) {
    auto text = readTextFile(project);
    if (!text) return text.error();
    OpenMode mode = OpenMode::read_write;
    auto p = projectFromJson(*text, &mode);
    if (!p)
        return makeError(Errc::parse_error,
                         "The project file is damaged; restore its backup before importing it",
                         p.error().detail);
    if (mode == OpenMode::read_only)
        return makeError(Errc::version_too_new,
                         "The project was saved by a newer version; import it with that version");
    std::error_code ec;
    if (!fs::is_directory(parent, ec))
        return makeError(Errc::io_write, "The folder for the new library does not exist", pathToUtf8(parent));
    MigrationCheck c;
    c.destination = parent / pathFromUtf8(libraryFolderName(name));
    if (fs::exists(c.destination, ec))
        return makeError(Errc::exists, "A folder with this name already exists", pathToUtf8(c.destination));
    c.projectBytes = text->size();
    const auto space = fs::space(parent, ec);
    if (!ec) {
        c.freeBytes = space.available;
        if (c.freeBytes < 2 * c.projectBytes + (1u << 20))
            return makeError(Errc::disk_full, "Not enough free space for the new library",
                             pathToUtf8(parent));
    }
    resolveScanPaths(*p, project.parent_path());
    c.scans = static_cast<int>(p->scans.size());
    for (const auto& s : p->scans) {
        c.prints += static_cast<int>(s.items.size());
        if (s.missing) ++c.missingScans;
    }
    c.exportRecords = static_cast<int>(p->exportRecords.size());
    return c;
}

Result<Library> migrateProjectToLibrary(const fs::path& project, const fs::path& parent,
                                        const std::string& name, const MigrateOptions& options) {
    auto check = checkProjectMigration(project, parent, name);
    if (!check) return check.error();
    auto source = loadProject(project);
    if (!source) return source.error();
    const fs::path dest = check->destination;
    const fs::path tmp =
        parent / pathFromUtf8("." + pathToUtf8(dest.filename()) + ".fk-tmp-" + randomToken(8));
    std::error_code ec;
    if (!fs::create_directory(tmp, ec) || ec)
        return makeError(Errc::io_write, "Cannot create the library folder", ec.message());
    auto fail = [&](Error e) -> Result<Library> {
        std::error_code ignored;
        fs::remove_all(tmp, ignored); // only the folder made here; the project is never touched
        return e;
    };
    Library lib;
    const std::string t = isoDateTimeNowUtc();
    lib.manifest.libraryId = newUuid();
    lib.manifest.name = name;
    lib.manifest.createdAt = lib.manifest.openedAt = lib.manifest.modifiedAt = t;
    // the catalog keeps the project id: exports carry it, so their graded files still relink (RFC-0006)
    lib.catalog = *source;
    lib.catalog.name = name;
    // folders the scans came from, for the folder list (references only)
    for (const auto& s : lib.catalog.scans) {
        const fs::path dir = pathFromUtf8(s.path).parent_path();
        LibrarySource src{pathToUtf8(dir), genericRelative(dir, dest)};
        if (!dir.empty() && std::find(lib.manifest.sources.begin(), lib.manifest.sources.end(), src) ==
                                lib.manifest.sources.end())
            lib.manifest.sources.push_back(src);
    }
    fs::create_directory(libraryBackupDir(tmp), ec);
    // tmp and dest are both direct children of `parent`: relative paths written here are those of dest
    if (auto st = saveProject(lib.catalog, libraryCatalogPath(tmp)); !st) return fail(st.error());
    if (options.failAt == "write") return fail(makeError(Errc::io_write, "Writing failed (test)"));
    if (auto st = writeFileAtomic(libraryManifestPath(tmp), manifestToJson(lib.manifest), false); !st)
        return fail(st.error());
    if (auto st = rotateBackups(tmp, t); !st)
        return fail(st.error()); // history starts with the imported state
    auto back = loadProject(libraryCatalogPath(tmp));
    if (!back) return fail(back.error());
    if (comparable(*back) != comparable(lib.catalog) || options.failAt == "verify")
        return fail(makeError(Errc::internal, "The new library does not read back as the project",
                              pathToUtf8(libraryCatalogPath(tmp))));
    if (options.failAt == "rename") return fail(makeError(Errc::io_write, "Renaming failed (test)"));
    fs::rename(tmp, dest, ec);
    if (ec) return fail(makeError(Errc::io_write, "Cannot move the new library into place", ec.message()));
    return openLibrary(dest);
}

} // namespace fk
