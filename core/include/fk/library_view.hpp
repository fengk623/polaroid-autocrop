// FrameKeeper core — what the library workspace lists, in which order (UX-05, U020, RFC-0018 §2, PRD F16):
// the prints or the scans of a library as entries with the values they sort and filter by. Sorting is stable
// and puts entries without a value last in both directions; nothing here reads image files.
#pragma once

#include "fk/project.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace fk {

enum class LibrarySortKey : int {
    capture_time = 0, ///< the photo's capture time (EXIF DateTimeOriginal of the scan); never the scan date
    added = 1,        ///< when it entered the library, then the order it was added in (the catalog's)
    edit_time = 2,    ///< last edit of the print
    edit_count = 3,
    rating = 4,
    pick = 5,        ///< picked, none, rejected
    text_label = 6,  ///< first text label, alphabetically
    color_label = 7, ///< red, yellow, green, blue, purple
    file_name = 8,   ///< the scan's file name, then the print's place on it
    pixel_size = 9,  ///< width × height in scan pixels
    extension = 10,
    file_type = 11,
    aspect_ratio = 12, ///< width / height
    manual = 13        ///< the user's own order (LibraryMeta::manualOrder)
};
/// Stable names ("capture_time", "added", …) used in `library.json` (`sort`) and by the app.
const char* librarySortKeyName(LibrarySortKey k);
/// Unknown names → `added`.
LibrarySortKey librarySortKeyFrom(const std::string& name);
/// Every key, in the order the app lists them.
std::vector<LibrarySortKey> librarySortKeys();

/// One print or scan as the library lists it.
struct LibraryEntry {
    bool print = true;
    std::string id;     ///< itemId or scanId
    std::string scanId; ///< the scan (also for a scan entry)
    int index = 0;      ///< place among the scan's prints (scans: 0)
    std::string fileName, extension, fileType, folder;
    int64_t width = 0, height = 0; ///< scan pixels (a print: its Outer bounding box)
    std::string capturedAt;        ///< ISO-8601 or "" (unknown)
    LibraryMeta meta; ///< a scan's own data; a scan's edit time/count = its latest / summed prints
    int order = 0;    ///< place in the catalog (the final tie-break)
};

/// The prints (`prints` true) or the scans of a library, in catalog order. `scanId` limits prints to one
/// scan; `folder` (a scan's parent folder, UTF-8) limits both to the scans in that folder. Excluded prints
/// are listed (the library is about every print; Edit Photos excludes from batches).
std::vector<LibraryEntry> libraryEntries(const Project& p, bool prints, const std::string& scanId = {},
                                         const std::string& folder = {});

struct LibraryFilter {
    int minRating = 0;              ///< 0 = any
    std::string pick = "any";       ///< "any", "picked", "rejected", "unflagged"
    std::string colorLabel = "any"; ///< "any", "none", or a colour
    std::string text; ///< in the file name, the print's label or a text label (case-insensitive)
};
bool matchesLibraryFilter(const LibraryEntry& e, const LibraryFilter& f);

/// Indices into `entries` in display order: by `key`, ascending or descending, entries without a value last
/// in both directions, equal values in catalog order (stable).
std::vector<size_t> sortLibraryEntries(const std::vector<LibraryEntry>& entries, LibrarySortKey key,
                                       bool descending);

/// Manual order after moving `id` to `position` (0-based) among `orderedIds` (the current display order):
/// manualOrder 1…n for every id, in the new order. Returns {id, manualOrder} pairs.
std::vector<std::pair<std::string, int64_t>> reorderManually(const std::vector<std::string>& orderedIds,
                                                             const std::string& id, size_t position);

} // namespace fk
