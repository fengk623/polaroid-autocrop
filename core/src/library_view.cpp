// The library workspace's lists: entries, filter, stable sort (UX-05, U020).
#include "fk/library_view.hpp"

#include "fk/geometry.hpp"
#include "fk/util.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iterator>
#include <optional>
#include <variant>

namespace fk {

namespace {

constexpr const char* kKeyNames[] = {"capture_time", "added",      "edit_time",    "edit_count", "rating",
                                     "pick",         "text_label", "color_label",  "file_name",  "pixel_size",
                                     "extension",    "file_type",  "aspect_ratio", "manual"};

std::string lower(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

int pickRank(const std::string& p) { // picked first when descending
    if (p == "picked") return 2;
    if (p == "rejected") return 0;
    return 1;
}

int colorRank(const std::string& c) { // the order labels are offered in; "none" has no value
    static const char* order[] = {"red", "yellow", "green", "blue", "purple"};
    for (int i = 0; i < 5; ++i)
        if (c == order[i]) return i;
    return c == "none" || c.empty() ? -1 : 5; // a colour of a later version sorts after the known ones
}

// a sortable value: number, text, or none (sorted last)
using Value = std::variant<std::monostate, double, std::string>;

Value valueOf(const LibraryEntry& e, LibrarySortKey k) {
    const LibraryMeta& m = e.meta;
    switch (k) {
    case LibrarySortKey::capture_time:
        return e.capturedAt.empty() ? Value{} : Value{e.capturedAt};
    case LibrarySortKey::added:
        // the same second (a folder imported at once): the catalog order, i.e. the order they were added
        return m.addedAt.empty() ? Value{} : Value{m.addedAt + "#" + std::to_string(1000000 + e.order)};
    case LibrarySortKey::edit_time:
        return m.editedAt.empty() ? Value{} : Value{m.editedAt};
    case LibrarySortKey::edit_count:
        return Value{static_cast<double>(m.editCount)};
    case LibrarySortKey::rating:
        return Value{static_cast<double>(m.rating)};
    case LibrarySortKey::pick:
        return Value{static_cast<double>(pickRank(m.pick))};
    case LibrarySortKey::text_label: {
        if (m.textLabels.empty()) return Value{};
        std::vector<std::string> l;
        for (const auto& t : m.textLabels)
            l.push_back(lower(t));
        return Value{*std::min_element(l.begin(), l.end())};
    }
    case LibrarySortKey::color_label: {
        const int r = colorRank(m.colorLabel);
        return r < 0 ? Value{} : Value{static_cast<double>(r)};
    }
    case LibrarySortKey::file_name:
        return Value{lower(e.fileName) + "#" + std::to_string(1000000 + e.index)};
    case LibrarySortKey::pixel_size:
        return e.width > 0 && e.height > 0
                   ? Value{static_cast<double>(e.width) * static_cast<double>(e.height)}
                   : Value{};
    case LibrarySortKey::extension:
        return e.extension.empty() ? Value{} : Value{lower(e.extension)};
    case LibrarySortKey::file_type:
        return e.fileType.empty() ? Value{} : Value{lower(e.fileType)};
    case LibrarySortKey::aspect_ratio:
        return e.width > 0 && e.height > 0
                   ? Value{static_cast<double>(e.width) / static_cast<double>(e.height)}
                   : Value{};
    case LibrarySortKey::manual:
        return m.manualOrder > 0 ? Value{static_cast<double>(m.manualOrder)} : Value{};
    }
    return Value{};
}

} // namespace

const char* librarySortKeyName(LibrarySortKey k) {
    const int i = static_cast<int>(k);
    return i >= 0 && i < static_cast<int>(std::size(kKeyNames)) ? kKeyNames[i] : "added";
}

LibrarySortKey librarySortKeyFrom(const std::string& name) {
    for (int i = 0; i < static_cast<int>(std::size(kKeyNames)); ++i)
        if (name == kKeyNames[i]) return static_cast<LibrarySortKey>(i);
    return LibrarySortKey::added;
}

std::vector<LibrarySortKey> librarySortKeys() {
    std::vector<LibrarySortKey> out;
    for (int i = 0; i < static_cast<int>(std::size(kKeyNames)); ++i)
        out.push_back(static_cast<LibrarySortKey>(i));
    return out;
}

std::vector<LibraryEntry> libraryEntries(const Project& p, bool prints, const std::string& scanId,
                                         const std::string& folder) {
    std::vector<LibraryEntry> out;
    int order = 0;
    for (const auto& s : p.scans) {
        const std::filesystem::path path = pathFromUtf8(s.path);
        const std::string dir = pathToUtf8(path.parent_path());
        if (!folder.empty() && dir != folder) continue;
        std::string ext = pathToUtf8(path.extension());
        if (!ext.empty() && ext.front() == '.') ext.erase(ext.begin());
        LibraryEntry base;
        base.scanId = s.scanId;
        base.fileName = pathToUtf8(path.filename());
        base.extension = lower(ext);
        base.fileType = (base.extension == "tif" || base.extension == "tiff") ? "TIFF" : lower(ext);
        base.folder = dir;
        base.capturedAt = s.library.capturedAt;
        if (prints) {
            if (!scanId.empty() && s.scanId != scanId) continue;
            for (const auto& it : s.items) {
                LibraryEntry e = base;
                e.print = true;
                e.id = it.itemId;
                e.index = it.index;
                const RectI b = boundingRect(it.outer);
                e.width = b.w;
                e.height = b.h;
                e.meta = it.library;
                // a print entered the library with its scan
                if (e.meta.addedAt.empty()) e.meta.addedAt = s.library.addedAt;
                e.order = order++;
                out.push_back(std::move(e));
            }
        } else {
            LibraryEntry e = base;
            e.print = false;
            e.id = s.scanId;
            e.width = s.info.width;
            e.height = s.info.height;
            e.meta = s.library;
            for (const auto& it : s.items) {
                e.meta.editCount += it.library.editCount;
                e.meta.editedAt = std::max(e.meta.editedAt, it.library.editedAt);
            }
            e.order = order++;
            out.push_back(std::move(e));
        }
    }
    return out;
}

bool matchesLibraryFilter(const LibraryEntry& e, const LibraryFilter& f) {
    if (e.meta.rating < f.minRating) return false;
    if (f.pick == "picked" && e.meta.pick != "picked") return false;
    if (f.pick == "rejected" && e.meta.pick != "rejected") return false;
    if (f.pick == "unflagged" && (e.meta.pick == "picked" || e.meta.pick == "rejected")) return false;
    if (f.colorLabel != "any" && e.meta.colorLabel != f.colorLabel) return false;
    if (!f.text.empty()) {
        const std::string q = lower(f.text);
        bool hit = lower(e.fileName).find(q) != std::string::npos;
        for (const auto& t : e.meta.textLabels)
            hit = hit || lower(t).find(q) != std::string::npos;
        if (!hit) return false;
    }
    return true;
}

std::vector<size_t> sortLibraryEntries(const std::vector<LibraryEntry>& entries, LibrarySortKey key,
                                       bool descending) {
    std::vector<size_t> idx(entries.size());
    for (size_t i = 0; i < idx.size(); ++i)
        idx[i] = i;
    std::vector<Value> v;
    v.reserve(entries.size());
    for (const auto& e : entries)
        v.push_back(valueOf(e, key));
    std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
        const Value &x = v[a], &y = v[b];
        const bool nx = std::holds_alternative<std::monostate>(x),
                   ny = std::holds_alternative<std::monostate>(y);
        if (nx || ny) return !nx && ny; // values first, then the entries without one
        if (x == y) return entries[a].order < entries[b].order;
        return descending ? y < x : x < y;
    });
    return idx;
}

std::vector<std::pair<std::string, int64_t>> reorderManually(const std::vector<std::string>& orderedIds,
                                                             const std::string& id, size_t position) {
    std::vector<std::string> ids = orderedIds;
    auto it = std::find(ids.begin(), ids.end(), id);
    if (it != ids.end()) {
        ids.erase(it);
        ids.insert(ids.begin() + static_cast<std::ptrdiff_t>(std::min(position, ids.size())), id);
    }
    std::vector<std::pair<std::string, int64_t>> out;
    for (size_t i = 0; i < ids.size(); ++i)
        out.emplace_back(ids[i], static_cast<int64_t>(i + 1));
    return out;
}

} // namespace fk
