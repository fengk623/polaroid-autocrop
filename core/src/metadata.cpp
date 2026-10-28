#include "fk/metadata.hpp"

#include "fk/product_info.hpp"

#include <chrono>
#include <ctime>
#include <regex>

namespace fk {

const char* privacyClassName(PrivacyClass c) {
    switch (c) {
    case PrivacyClass::safe:
        return "safe";
    case PrivacyClass::location:
        return "location";
    case PrivacyClass::identity:
        return "identity";
    case PrivacyClass::unknown:
        return "unknown";
    }
    return "unknown";
}

const char* privacyPresetName(PrivacyPreset p) {
    switch (p) {
    case PrivacyPreset::keep_all_safe:
        return "keep_all_safe";
    case PrivacyPreset::remove_location:
        return "remove_location";
    case PrivacyPreset::remove_identity:
        return "remove_identity";
    }
    return "keep_all_safe";
}

MetadataSet applyPrivacy(const MetadataSet& in, PrivacyPreset p) {
    MetadataSet out = in;
    out.hasGps = false; // GPS is never copied (we do not write a GPS IFD in any preset)
    if (p == PrivacyPreset::remove_identity) {
        out.artist.clear();
        out.hostComputer.clear();
    }
    std::vector<MetadataField> kept;
    for (const auto& f : in.fields) {
        if (f.cls == PrivacyClass::location && p != PrivacyPreset::keep_all_safe) continue;
        if (f.cls == PrivacyClass::identity && p == PrivacyPreset::remove_identity) continue;
        kept.push_back(f);
    }
    out.fields = std::move(kept);
    return out;
}

namespace {
std::string xmlEscape(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) {
        switch (c) {
        case '&':
            o += "&amp;";
            break;
        case '<':
            o += "&lt;";
            break;
        case '>':
            o += "&gt;";
            break;
        case '"':
            o += "&quot;";
            break;
        default:
            o += c;
        }
    }
    return o;
}

std::string extractDc(const std::string& xmp, const std::string& name) {
    // Copies a whole <dc:name>…</dc:name> element if present (title, description, creator, rights).
    const std::string open = "<dc:" + name, close = "</dc:" + name + ">";
    const auto a = xmp.find(open);
    if (a == std::string::npos) return {};
    const auto b = xmp.find(close, a);
    if (b == std::string::npos) return {};
    return xmp.substr(a, b + close.size() - a);
}

std::string findValue(const std::string& xmp, const std::string& qname) {
    // attribute form: qname="value"
    const std::regex attr(qname + R"re(\s*=\s*"([^"]*)")re");
    std::smatch m;
    if (std::regex_search(xmp, m, attr)) return m[1].str();
    // element form: <qname>value</qname>
    const std::regex elem("<" + qname + R"re(>\s*([^<]*?)\s*</)re" + qname + ">");
    if (std::regex_search(xmp, m, elem)) return m[1].str();
    return {};
}
} // namespace

std::string buildXmp(const XmpIds& ids, const MetadataSet* source) {
    std::string dc;
    if (source && !source->sourceXmp.empty()) {
        for (const char* n : {"title", "description", "creator", "rights"})
            dc += extractDc(source->sourceXmp, n);
    }
    std::string x;
    x += "<?xpacket begin=\"\xEF\xBB\xBF\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n";
    x += "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">\n";
    x += " <rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n";
    x += "  <rdf:Description rdf:about=\"\"\n";
    x += "    xmlns:fk=\"" + std::string(product::kXmpNamespace) + "\"\n";
    x += "    xmlns:xmpMM=\"http://ns.adobe.com/xap/1.0/mm/\"\n";
    x += "    xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\"\n";
    x += "    xmlns:dc=\"http://purl.org/dc/elements/1.1/\"\n";
    x += "    xmp:CreatorTool=\"" + xmlEscape(std::string(product::kName) + " " + product::kVersion) + "\"\n";
    if (!ids.projectId.empty()) x += "    fk:ProjectID=\"" + xmlEscape(ids.projectId) + "\"\n";
    if (!ids.scanId.empty()) x += "    fk:ScanID=\"" + xmlEscape(ids.scanId) + "\"\n";
    if (!ids.itemId.empty()) x += "    fk:ItemID=\"" + xmlEscape(ids.itemId) + "\"\n";
    if (!ids.editVersionId.empty()) {
        x += "    fk:EditVersionID=\"" + xmlEscape(ids.editVersionId) + "\"\n";
        x += "    xmpMM:DocumentID=\"uuid:" + xmlEscape(ids.editVersionId) + "\"\n";
    }
    if (!ids.instanceId.empty()) x += "    xmpMM:InstanceID=\"uuid:" + xmlEscape(ids.instanceId) + "\"\n";
    if (ids.version > 0) x += "    fk:Version=\"" + std::to_string(ids.version) + "\"\n";
    if (!ids.label.empty()) x += "    fk:Label=\"" + xmlEscape(ids.label) + "\"\n";
    if (!ids.frameMode.empty()) x += "    fk:FrameMode=\"" + xmlEscape(ids.frameMode) + "\"\n";
    if (!dc.empty()) {
        x += "  >\n" + dc + "\n  </rdf:Description>\n";
    } else {
        x += "  />\n";
    }
    x += " </rdf:RDF>\n</x:xmpmeta>\n";
    x += std::string(2048, ' ') + "\n"; // padding for in-place editing by other tools
    x += "<?xpacket end=\"w\"?>";
    return x;
}

XmpIds parseXmpIds(const std::string& xmp) {
    XmpIds ids;
    ids.projectId = findValue(xmp, "fk:ProjectID");
    ids.scanId = findValue(xmp, "fk:ScanID");
    ids.itemId = findValue(xmp, "fk:ItemID");
    ids.editVersionId = findValue(xmp, "fk:EditVersionID");
    if (ids.editVersionId.empty()) {
        std::string doc = findValue(xmp, "xmpMM:DocumentID");
        if (doc.rfind("uuid:", 0) == 0) ids.editVersionId = doc.substr(5);
    }
    const std::string v = findValue(xmp, "fk:Version");
    if (!v.empty()) {
        try {
            ids.version = std::stoi(v);
        } catch (...) {
            ids.version = 0;
        }
    }
    ids.label = findValue(xmp, "fk:Label");
    ids.frameMode = findValue(xmp, "fk:FrameMode");
    return ids;
}

std::string tiffDateTimeNow() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y:%m:%d %H:%M:%S", &tm);
    return buf;
}

std::string isoDateTimeNowUtc() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

} // namespace fk
