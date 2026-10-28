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
};

enum class PrivacyPreset : int { keep_all_safe = 0, remove_location = 1, remove_identity = 2 };
const char* privacyPresetName(PrivacyPreset p);

/// Applies a privacy preset: location data is never written by FrameKeeper (we do not copy GPS);
/// remove_identity additionally clears artist, host computer and owner/serial fields.
MetadataSet applyPrivacy(const MetadataSet& in, PrivacyPreset p);

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
