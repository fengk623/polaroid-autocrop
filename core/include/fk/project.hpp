// FrameKeeper core — non-destructive projects, sidecars and templates (RFC-0009, ADR-0005,
// docs/project-format.md, PRD F06/F09/F11).
#pragma once

#include "fk/common.hpp"
#include "fk/composite.hpp"
#include "fk/detection.hpp"
#include "fk/export.hpp"
#include "fk/geometry.hpp"
#include "fk/metadata.hpp"
#include "fk/relink.hpp"

#include <array>
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

/// Transform confirmed when a graded file was relinked (FK-002, U005, RFC-0006 §Confirmed transform): stored
/// with the relink and applied as stored by preview and output — never guessed again.
struct RelinkTransform {
    bool set = false; ///< false: relinked by a version before 0.6.0-alpha.5 (decided again from the content)
    int rotate =
        0; ///< 90° clockwise turns from the export to the graded file (0..3); undone when compositing
    std::string rotateSource; ///< "none" (no turn), "content" (picture, unambiguous), "user" (chosen)
    bool scale = false;       ///< resampling to the export size confirmed (same aspect ratio, after the turn)
    bool lowerDepth = false;  ///< lower bit depth accepted
    double contentScore = 0;  ///< picture correlation at `rotate`
    SizeI gradedSize{};       ///< graded file size when confirmed (a different size later = the file changed)
};

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
    RelinkTransform relinkTransform;
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

// ---- sidecar / export record
// ------------------------------------------------------------------------------------
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
    /// "export" for an export; "manual_alignment" for a graded file placed on a print that has no export of
    /// its own (RFC-0006 §New alignment) — not an export, so no export date or export-file hash is faked.
    std::string origin = "export";
};

struct Project {
    std::string schema = "fk.project/1";
    std::string projectId;
    std::string name;
    std::string createdAt, modifiedAt;
    ProjectSettings settings;
    std::vector<ScanRecord> scans;
    /// One record per export (UX-07): what a sidecar file holds, kept in the project so that relinking does
    /// not depend on a `.fk.json` beside the graded file. Keyed by Sidecar::editVersionId.
    std::vector<Sidecar> exportRecords;
    std::vector<std::string> unparsedExportRecords; ///< records this version cannot read, kept verbatim
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

// ---- sidecar (declared above Project) ------------------------------------------------------
Sidecar makeSidecar(const Project& p, const ScanRecord& s, const ItemRecord& it, const EditVersion& ev,
                    const ExportReport& rep);
Status saveSidecar(const Sidecar& s, const std::filesystem::path& path);
Result<Sidecar> loadSidecar(const std::filesystem::path& path);
std::string sidecarPathFor(const std::string& imagePathUtf8); ///< `<stem>.fk.json` next to the image
/// The export record for `editVersionId` kept in the project, or nullptr.
const Sidecar* findExportRecord(const Project& p, const std::string& editVersionId);
/// The project's record for this export; falls back to the portable sidecar file if one was written.
Result<Sidecar> exportRecordFor(const Project& p, const EditVersion& ev);
/// Copies readable sidecar files of exports that have no record yet into the project (done by loadProject for
/// projects written before export records existed). Returns the number of records added.
int importSidecarRecords(Project& p);

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
    int suggestedRotate = 0;   ///< best of `allowedRotations` (all four when none fits)
    double signatureScore = 0; ///< correlation at suggestedRotate
    bool signaturePass = false;
    /// Turns (0..3) whose size fits the export: {0,2} or {1,3}; all four for a square image area; none = the
    /// graded file is cropped differently (manual alignment).
    std::vector<int> allowedRotations;
    std::array<double, 4> rotationScores{}; ///< picture correlation per turn
    /// The picture tells the turn apart: best passes and leads every other allowed turn by
    /// kOrientationMargin. Otherwise (symmetric, square or low-texture pictures) the user chooses
    /// ("choose_rotation").
    bool rotationReliable = false;
    std::vector<std::string>
        actionsNeeded; ///< "scale", "rotate", "accept_lower_depth", "choose_icc", "confirm_content"
};
inline constexpr double kOrientationMargin = 0.15;
/// Checks a graded file against an export record. Actions: "rotate", "choose_rotation", "scale",
/// "manual_alignment", "accept_lower_depth", "choose_icc", "confirm_content".
RelinkCheck checkEdited(const Sidecar& sc, const ImageBuffer& edited, const ImageInfo& editedInfo);

// ---- relink v2: target first, evidence model (RFC-0006 §Relink v2) ----------------------------------
enum class EvidenceKind : int {
    file_hash = 0,
    embedded_ids = 1,
    file_name = 2,
    content = 3,
    user_target = 4
};
const char* evidenceKindName(EvidenceKind k);
struct Evidence {
    EvidenceKind kind = EvidenceKind::content;
    bool supports = true; ///< false = contradicts this option
    std::string detail;
};
/// One print × export version a graded file could belong to. `editVersionId` empty = new alignment.
struct RelinkOption {
    std::string scanId, itemId, editVersionId;
    bool newAlignment = false;
    bool hashMatch = false;
    bool contentChecked = false, contentPass = false;
    double contentScore = 0;      ///< best correlation over the 4 rotations
    int contentRotate = 0;        ///< 90° clockwise turns of the record that matched best
    bool conflict = false;        ///< some evidence supports and some contradicts this option
    bool contradicted = false;    ///< some evidence contradicts this option
    bool versionEvidence = false; ///< embedded IDs or the file name name this export version
    double score = 0;
    std::vector<Evidence> evidence;
};
enum class RelinkDecision : int { unique = 0, ambiguous = 1, none = 2 };
const char* relinkDecisionName(RelinkDecision d);
struct RelinkAssessment {
    RelinkDecision decision = RelinkDecision::none;
    std::vector<RelinkOption> options; ///< best first; for `unique` the first is the answer
};
/// What can be known about a graded file without the project.
struct RelinkInput {
    std::filesystem::path file;
    std::string sha256;
    XmpIds ids;
    bool hasIds = false;
    Signature signature{};
    SizeI size{};
};
RelinkInput describeGradedFile(const std::filesystem::path& file, const ImageBuffer& pixels);
/// Weighs all evidence (never "first hit wins"). With `targetItemId` only that print is considered and, if it
/// has no export record, a new-alignment option is offered (its content is compared with `targetSignature`,
/// the print's current image area, when given).
RelinkAssessment assessRelink(const RelinkInput& in, const Project& p, const std::string& targetItemId = {},
                              const Signature* targetSignature = nullptr);
/// Geometry of a new alignment for a graded image of size `graded`: the print's current Inner, straightened
/// or as a pixel crop — whichever aspect ratio is closer to the graded image's (either orientation) — upright
/// applied. An unresized export (same size) decides first.
ExportGeometry alignmentGeometry(const ScanRecord& s, const ItemRecord& it, SizeI graded = {});
/// As above; when both shapes fit a resized graded file, the one whose picture (sampled from `preview`,
/// `downscale` times smaller than the scan) matches `gradedSignature` better.
ExportGeometry alignmentGeometry(const ScanRecord& s, const ItemRecord& it, SizeI graded,
                                 const ImageBuffer& preview, double downscale,
                                 const Signature& gradedSignature);
/// Record for a new alignment with geometry `g` (from alignmentGeometry) and `areaSignature`
/// (signatureOfExport of it); origin "manual_alignment", file = the graded file, no export date.
Sidecar alignmentRecord(const Project& p, const ScanRecord& s, const ItemRecord& it, const ExportGeometry& g,
                        const Signature& areaSignature, const std::string& gradedFileName,
                        const std::string& gradedSha256);
/// Signature of what an export with geometry `g` contains, sampled from `img`: the scan, or a preview of it
/// `downscale` times smaller.
Signature signatureOfExport(const ImageBuffer& img, const ExportGeometry& g, double downscale = 1.0);

// ---- templates --------------------------------------------------------------------------------------
Status saveTemplate(const Template& t, const std::filesystem::path& path);
Result<Template> loadTemplate(const std::filesystem::path& path, OpenMode* mode = nullptr);

/// Atomically writes a text file (temp → flush → rename), keeping `<path>.bak` of an existing file if
/// requested.
Status writeFileAtomic(const std::filesystem::path& path, const std::string& content, bool keepBackup);
Result<std::string> readTextFile(const std::filesystem::path& path);

} // namespace fk
