// FrameKeeper core — libraries: a project kept in a `<Name>.fklibrary/` folder with what a long-lived
// collection needs (RFC-0018, PRD F16, docs/project-format.md §Libraries). The catalog is an ordinary
// `fk.project/1` document; scans and graded files are referenced, never copied, moved or deleted.
#pragma once

#include "fk/common.hpp"
#include "fk/project.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace fk {

inline constexpr int kLibrarySchemaVersion = 1;
inline constexpr int kLibraryBackupsKept = 10;             ///< rotating catalog history in `backups/`
inline constexpr int kLibraryBackupIntervalSeconds = 3600; ///< at most one history entry per hour of work
inline constexpr int kLibraryLockHeartbeatSeconds = 60;    ///< the app refreshes its lock this often
inline constexpr int kLibraryLockStaleSeconds = 180;       ///< a lock not refreshed for this long is stale

/// A folder the library was imported from (left-hand folder list, U020). A reference: removing one never
/// touches the disk.
struct LibrarySource {
    std::string path;    ///< UTF-8, absolute
    std::string relPath; ///< from the library folder, '/' separators
    friend bool operator==(const LibrarySource&, const LibrarySource&) = default;
};

/// `library.json` (schema `fk.library/1`). It never names something the catalog does not contain.
struct LibraryManifest {
    std::string libraryId; ///< new for every library (also a migrated one; the catalog keeps its project id)
    std::string name;
    std::string cover; ///< a print or scan id; empty = the first print
    std::vector<LibrarySource> sources;
    std::string
        sortJson; ///< the app's last sort per view (a JSON object, or empty); kept verbatim by the core
    std::string createdAt, openedAt, modifiedAt; ///< ISO-8601 UTC
    /// Scans and prints in the catalog when it was last saved (home page cards, U019); −1 = not recorded
    /// (written by versions before 0.8.0-alpha.2).
    int scanCount = -1, printCount = -1;
    std::string savedBy;     ///< `app.version` that last wrote it
    std::string unknownJson; ///< keys a later version wrote (JSON object or empty), written back unchanged
};

struct Library {
    std::filesystem::path folder; ///< `…/<Name>.fklibrary`
    LibraryManifest manifest;
    Project catalog; ///< the library's content (`catalog.fkproj`)
};

// ---- layout (RFC-0018 §1)
std::filesystem::path libraryManifestPath(const std::filesystem::path& folder); ///< library.json
std::filesystem::path libraryCatalogPath(const std::filesystem::path& folder);  ///< catalog.fkproj
std::filesystem::path libraryLockPath(const std::filesystem::path& folder);     ///< library.lock
std::filesystem::path libraryBackupDir(const std::filesystem::path& folder);    ///< backups/
std::filesystem::path libraryCacheDir(const std::filesystem::path& folder);     ///< cache/ (deletable)
/// A folder holding `library.json` or `catalog.fkproj`.
bool isLibraryFolder(const std::filesystem::path& folder);
/// `<name>.fklibrary` with characters no file system accepts (`<>:"/\|?*`, control characters, trailing dots
/// and spaces, Windows device names) replaced by `_`; an empty name becomes "Library".
std::string libraryFolderName(const std::string& name);

// ---- create, open, save (RFC-0018 §3–4)
/// Creates `<parent>/<libraryFolderName(name)>` with an empty catalog and its manifest, written into a
/// temporary folder first and renamed into place: an existing folder is never reused (Errc::exists).
Result<Library> createLibrary(const std::filesystem::path& parent, const std::string& name);

struct LibraryOpenInfo {
    /// read_only when the manifest or the catalog was written with a newer schema (a later major version)
    OpenMode mode = OpenMode::read_write;
    /// "file" (library.json read), "backup" (library.json.bak), "catalog" (rebuilt from the catalog: cover,
    /// sources and sort were lost; nothing else lives only in the manifest)
    std::string manifestSource = "file";
};
/// Opens a library folder. A damaged catalog fails with parse_error (detail: whether a restorable copy
/// exists — libraryRestoreCandidates); nothing is changed then. Scan paths resolve absolute → relative →
/// missing (RFC-0018 §7).
Result<Library> openLibrary(const std::filesystem::path& folder, LibraryOpenInfo* info = nullptr);
/// Writes the catalog (temp → flush → rename; the previous good catalog becomes `catalog.fkproj.bak`, a
/// damaged one never does), then rotates `backups/` (a copy of the new catalog when the newest entry is an
/// hour old or there is none; the oldest beyond kLibraryBackupsKept removed after that copy is written), then
/// `library.json`. `nowUtc` (ISO-8601) replaces the clock in tests.
Status saveLibrary(Library& lib, const std::string& nowUtc = {});

struct LockOwner;

// ---- home page (U019, RFC-0018 §10)
/// `library.json` alone (or its `.bak`), without reading the catalog — what a card on the home page shows.
Result<LibraryManifest> readLibraryManifest(const std::filesystem::path& folder);
/// Records that the library was opened now (`openedAt`; only `library.json` is written). Called by the
/// instance that holds the lock.
Status touchLibraryOpened(const std::filesystem::path& folder, const std::string& nowUtc = {});
/// U020: records the library's sort (`sort` in `library.json`, a JSON object; only `library.json` is
/// written). A view setting, not an edit of the catalog.
Status writeLibrarySort(const std::filesystem::path& folder, const std::string& sortJson);
/// Renames the library (its `name`; the folder keeps its name, so references to it stay valid).
/// Errc::conflict when another instance holds a fresh lock on it.
Status renameLibrary(const std::filesystem::path& folder, const std::string& name, const LockOwner& me);

// ---- backups and restore (RFC-0018 §4)
/// Readable entries of `backups/`, newest first.
std::vector<std::filesystem::path> libraryBackups(const std::filesystem::path& folder);
/// What a damaged catalog can be restored from: `catalog.fkproj.bak` and the `backups/` entries that read as
/// projects, the most recently saved first (a history entry copies the catalog as it was saved, so it can be
/// newer than `.bak`, the save before the last); on equal save times `.bak` first.
std::vector<std::filesystem::path> libraryRestoreCandidates(const std::filesystem::path& folder);
/// Moves the damaged catalog aside (`catalog.fkproj.damaged-<UTC>`, kept) and copies `from` (one of
/// libraryRestoreCandidates) in its place. Returns the path of the damaged file kept aside (empty when there
/// was no catalog at all).
Result<std::filesystem::path> restoreLibraryCatalog(const std::filesystem::path& folder,
                                                    const std::filesystem::path& from);

// ---- two app instances (RFC-0018 §5)
/// Who is writing a library.
struct LockOwner {
    int64_t pid = 0;
    std::string host, user, app;
    std::string token; ///< random per app session: a reused process id is not taken for this session
};
/// This process: its id, host and user names, the product version, a new token.
LockOwner thisLockOwner();
struct LibraryLock {
    bool present = false;
    bool readable = false; ///< false: present but unreadable (being written, or damaged)
    LockOwner owner;
    std::string startedAt, heartbeatAt;
    bool ours = false; ///< same token as the caller
    bool stale =
        false; ///< heartbeat older than kLibraryLockStaleSeconds, or a process of this host that ended
};
LibraryLock readLibraryLock(const std::filesystem::path& folder, const LockOwner& me,
                            const std::string& nowUtc = {});
enum class LockState : int { acquired = 0, held = 1, stale = 2 };
const char* lockStateName(LockState s);
struct LockAttempt {
    LockState state = LockState::held;
    LibraryLock lock; ///< the lock found (held, stale) or written (acquired)
};
/// Creates `library.lock` exclusively. A lock already there is `held` (open read-only and say who holds it)
/// or `stale` (ask the user before takeOverLibraryLock — never taken silently). Errors only when the folder
/// cannot be written (then the library opens read-only too).
Result<LockAttempt> acquireLibraryLock(const std::filesystem::path& folder, const LockOwner& me,
                                       const std::string& nowUtc = {});
/// Renews the heartbeat of our lock; Errc::conflict when the lock is no longer ours (taken over).
Status refreshLibraryLock(const std::filesystem::path& folder, const LockOwner& me,
                          const std::string& nowUtc = {});
/// Replaces a stale lock with ours — only when the lock is still the one the user was asked about (`seen`)
/// and still stale; otherwise returns what is there now (`held` or `stale`).
Result<LockAttempt> takeOverLibraryLock(const std::filesystem::path& folder, const LockOwner& me,
                                        const LibraryLock& seen, const std::string& nowUtc = {});
/// Removes our lock (a lock that is no longer ours is left alone).
void releaseLibraryLock(const std::filesystem::path& folder, const LockOwner& me);

// ---- old projects (RFC-0018 §6): a migration copy, never in place
struct MigrationCheck {
    int scans = 0, prints = 0, missingScans = 0, exportRecords = 0;
    uint64_t projectBytes = 0, freeBytes = 0;
    std::filesystem::path destination; ///< `<parent>/<libraryFolderName(name)>`
};
/// Reads the project and checks the destination: Errc::parse_error (damaged), version_too_new (a later
/// schema — copy it with that version), exists (destination taken), disk_full (free space below twice the
/// project's size plus 1 MiB), io_write (parent folder missing). Missing scans are counted, not refused: the
/// library references them as the project did.
Result<MigrationCheck> checkProjectMigration(const std::filesystem::path& project,
                                             const std::filesystem::path& parent, const std::string& name);
struct MigrateOptions {
    std::string failAt; ///< tests only: "write", "verify" or "rename" fails on purpose at that step
};
/// checkProjectMigration, then writes the library into a temporary folder next to the destination, reads it
/// back and compares it with the project (same scans, prints, records, profiles; relative paths rewritten for
/// the new place), then renames it into place. Any failure removes the temporary folder and leaves nothing
/// behind. The project file and its `.bak` are only read.
Result<Library> migrateProjectToLibrary(const std::filesystem::path& project,
                                        const std::filesystem::path& parent, const std::string& name,
                                        const MigrateOptions& options = {});

/// Seconds since the Unix epoch of an ISO-8601 UTC time `YYYY-MM-DDTHH:MM:SSZ`; −1 when it is not one.
int64_t isoUtcToEpoch(const std::string& iso);
/// ISO-8601 UTC of `epochSeconds`.
std::string epochToIsoUtc(int64_t epochSeconds);

} // namespace fk
