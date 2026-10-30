// FrameKeeper core — metadata policy and XMP (RFC-0013).
#pragma once

#include <string>
#include <vector>

namespace fk {

enum class PrivacyClass : int { safe = 0, location = 1, identity = 2, unknown = 3 };
const char* privacyClassName(PrivacyClass c);

struct MetadataField {
    std::string name;  ///< e.g. "Make", "GPSInfo", "Exif.BodySerialNumber"
    std::string value; ///< printable (truncated to 200 chars)
    PrivacyClass cls = PrivacyClass::unknown;
};

/// Descriptive metadata carried from a source file to outputs. Structural tags never appear here.
struct MetadataSet {
    std::string make, model, dateTime, artist, copyright, imageDescription, documentName, hostComputer;
    std::string dateTimeOriginal; ///< from EXIF if present
    std::string sourceXmp;        ///< raw packet read from the source (not copied verbatim)
    bool hasGps = false;
    std::vector<MetadataField> fields; ///< everything found, classified, for the privacy panel
    /// Set by applyPrivacy; every writer (TIFF tags, XMP for TIFF/PNG/JPEG/DNG) follows it (FK-004).
    int preset = 0; ///< PrivacyPreset
};

enum class PrivacyPreset : int { keep_all_safe = 0, remove_location = 1, remove_identity = 2 };
const char* privacyPresetName(PrivacyPreset p);

/// Applies a privacy preset: location data is never written by FrameKeeper (we do not copy GPS);
/// remove_identity additionally clears artist, host computer and owner/serial fields, and the creator copied
/// from the source XMP (FK-004). The result is the only input of every metadata writer.
MetadataSet applyPrivacy(const MetadataSet& in, PrivacyPreset p);

/// Descriptive tags a format carries besides XMP: none (PNG, JPEG), TIFF, or DNG (whose Make/Model name
/// FrameKeeper's linear scan, not the scanner).
enum class TagChannel : int { none = 0, tiff = 1, dng = 2 };
/// FK-004 "kept fields, stated explicitly": the descriptive fields an output written from `filtered` (the
/// result of applyPrivacy) carries, per channel ("TIFF …" tags, "XMP dc:…" in every format), with their
/// values. FrameKeeper's own Software, DateTime and identifiers are not listed. GPS, EXIF owner and serial
/// numbers are never written.
std::vector<MetadataField> writtenMetadata(const MetadataSet& filtered, TagChannel tags);

struct XmpIds {
    std::string projectId, scanId, itemId, editVersionId, instanceId;
    int version = 0;
    std::string label;     ///< "P01"
    std::string frameMode; ///< "image_area" | "real_scan" | "synthetic"
};

/// Builds our XMP packet (fk namespace + xmpMM ids + selected dc fields from the source XMP).
std::string buildXmp(const XmpIds& ids, const MetadataSet* source = nullptr);
/// Extracts our ids from any XMP packet (attribute or element form). Missing fields stay empty.
XmpIds parseXmpIds(const std::string& xmp);

/// TIFF "YYYY:MM:DD HH:MM:SS" for now (local time).
std::string tiffDateTimeNow();
/// ISO-8601 UTC "YYYY-MM-DDTHH:MM:SSZ".
std::string isoDateTimeNowUtc();

} // namespace fk
