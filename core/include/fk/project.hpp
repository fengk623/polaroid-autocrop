// FrameKeeper core — non-destructive projects, sidecars and templates (RFC-0009, ADR-0005,
// docs/project-format.md, PRD F06/F09/F11).
#pragma once

#include "fk/common.hpp"
#include "fk/composite.hpp"
#include "fk/detection.hpp"
#include "fk/export.hpp"
#include "fk/geometry.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace fk {

inline constexpr int kProjectSchemaVersion = 1;
inline constexpr int kSidecarSchemaVersion = 1;
inline constexpr int kTemplateSchemaVersion = 1;

struct Fingerprint {
    uint64_t size = 0;
    std::string mtime;       ///< ISO-8601 UTC (informational)
    std::string quickSha256; ///< SHA-256 of size + first MiB + last MiB
    std::string fullSha256;  ///< optional
    friend bool operator==(const Fingerprint& a, const Fingerprint& b) {
        return a.size == b.size && a.quickSha256 == b.quickSha256;
    }
};
Result<Fingerprint> fingerprintFile(const std::filesystem::path& p, bool full = false);

enum class BoundarySource : int { automatic = 0, templated = 1, manual = 2 };
const char* boundarySourceName(BoundarySource s);

struct EditVersion {
    std::string editVersionId;
    int version = 0;
    std::string path;    ///< UTF-8, absolute
    std::string sidecar; ///< UTF-8, absolute
    std::string exportedAt;
    std::string fileSha256;
    std::string format; ///< tif/png/jpg/dng
    // relink state
    std::string relinkedPath, relinkedSha256, matchedBy, relinkedAt;
};

struct CompositeSettings {
    MaskMode maskMode = MaskMode::hard_edge;
    double featherPx = 0;
    double preserveEdgePx = 0;
    std::vector<MaskStroke> strokes;
    std::string lastOutput;
    std::string lastOutputAt;
};

struct ItemRecord {
    std::string itemId;
    int index = 0;
    std::string label;
    std::string filmType;
    Shape outer{};
    double cornerRadiusPx = 0;
    BoundarySource outerSource = BoundarySource::automatic;
    Shape inner{};
    BoundarySource innerSource = BoundarySource::automatic;
    int upright = 0;
    double confidence = 0, outerConfidence = 0, innerConfidence = 0;
    std::vector<std::string> reasons;
    bool reviewed = false;
    bool excluded = false;
    std::vector<EditVersion> edits;
    CompositeSettings composite;
    std::string finalExport;       ///< last full-print export path
    std::vector<std::string> undo; ///< serialized prior boundary states (bounded)
    std::vector<std::string> redo;
};

struct ScanRecord {
    std::string scanId;
    std::string path;    ///< UTF-8 absolute
    std::string relPath; ///< relative to the project file's folder (portable)
    Fingerprint fingerprint;
    ImageInfo info; ///< summary (icc bytes not stored; iccSha256/iccDescription are)
    std::string iccSha256;
    std::string detectedAt;
    int expectedCount = 4;
    double dpiUsed = 0;
    int proxyScale = 1;
    std::vector<std::string> warnings;
    std::vector<ItemRecord> items;
    bool missing = false; ///< runtime only
};

struct ProjectSettings {
    int expectedCount = 4;
    double safetyMarginPx = 1.0;
    std::string exportFolder;
    std::string exportFormat = "tif";
    int exportBits = 0; ///< 0 = source
    std::string exportCompression = "lzw";
    std::string colorPolicy = "keep";
    std::string privacy = "keep_all_safe";
};

struct Project {
    std::string schema = "fk.project/1";
    std::string projectId;
    std::string name;
    std::string createdAt, modifiedAt;
    ProjectSettings settings;
    std::vector<ScanRecord> scans;
    std::string unknownJson; ///< unknown top-level keys preserved verbatim (forward compatibility)
};

enum class OpenMode : int { read_write = 0, read_only = 1 };

Project newProject(const std::string& name);
/// Loads (and migrates) a project. Newer schemas open read-only (`mode`), damaged files fail with
/// parse_error.
Result<Project> loadProject(const std::filesystem::path& path, OpenMode* mode = nullptr);
/// Atomic save: temp → flush → rename; the previous file is kept as `<name>.bak`.
Status saveProject(const Project& p, const std::filesystem::path& path);
std::string projectToJson(const Project& p);
Result<Project> projectFromJson(const std::string& json, OpenMode* mode = nullptr);

/// Finds missing scans in `folder` by file name, then by quick fingerprint. Returns how many were relocated.
int relocateScans(Project& p, const std::filesystem::path& projectDir, const std::filesystem::path& folder);
/// Resolves `relPath` against the project folder when `path` is missing.
void resolveScanPaths(Project& p, const std::filesystem::path& projectDir);

ItemRecord itemFromDetection(const DetectedItem& d, int index);
ScanRecord* findScan(Project& p, const std::string& scanId);
ItemRecord* findItem(Project& p, const std::string& itemId, ScanRecord** scanOut = nullptr);

// ---- sidecar ------------------------------------------------------------------------------------
struct Sidecar {
    std::string schema = "fk.sidecar/1";
    std::string projectId, scanId, itemId, editVersionId, label;
    int version = 0;
    std::string scanFile;
    Fingerprint scanFingerprint;
    Shape inner{}, outer{};
    ExportGeometry geometry{};
    SizeI sourceSize{}, exportSize{};
    int bits = 0, channels = 0;
    std::string iccDescription, iccSha256;
    double dpiX = 0, dpiY = 0;
    std::string fileName, fileSha256;
    Signature signature{};
    std::string software, exportedAt;
};
Sidecar makeSidecar(const Project& p, const ScanRecord& s, const ItemRecord& it, const EditVersion& ev,
                    const ExportReport& rep);
Status saveSidecar(const Sidecar& s, const std::filesystem::path& path);
Result<Sidecar> loadSidecar(const std::filesystem::path& path);
std::string sidecarPathFor(const std::string& imagePathUtf8); ///< `<stem>.fk.json` next to the image

// ---- relink ---------------------------------------------------------------------------------------
enum class MatchSource : int { xmp = 0, sidecar = 1, file_name = 2, manual = 3, none = 4 };
const char* matchSourceName(MatchSource m);
struct RelinkCandidate {
    MatchSource source = MatchSource::none;
    std::string scanId, itemId, editVersionId;
    std::optional<Sidecar> sidecar;
};
/// Steps 1–3 of the matching order (ADR-0009). Never reads pixels.
RelinkCandidate findMatch(const std::filesystem::path& edited, const Project& p);

struct RelinkCheck {
    bool sizeMatches = false, aspectMatches = false, depthLower = false, iccMissing = false,
         iccDiffers = false;
    int suggestedRotate = 0;
    double signatureScore = 0; ///< best over the 4 rotations
    bool signaturePass = false;
    std::vector<std::string>
        actionsNeeded; ///< "scale", "rotate", "accept_lower_depth", "choose_icc", "confirm_content"
};
RelinkCheck checkEdited(const Sidecar& sc, const ImageBuffer& edited, const ImageInfo& editedInfo);

// ---- templates --------------------------------------------------------------------------------------
Status saveTemplate(const Template& t, const std::filesystem::path& path);
Result<Template> loadTemplate(const std::filesystem::path& path, OpenMode* mode = nullptr);

/// Atomically writes a text file (temp → flush → rename), keeping `<path>.bak` of an existing file if
/// requested.
Status writeFileAtomic(const std::filesystem::path& path, const std::string& content, bool keepBackup);
Result<std::string> readTextFile(const std::filesystem::path& path);

} // namespace fk
