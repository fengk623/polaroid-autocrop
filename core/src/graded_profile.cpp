// Colour profile of a relinked graded file (FK-003, RFC-0012 §Graded files without a usable profile): a
// missing or unreadable embedded profile is never guessed silently. The user confirms an interpretation when
// relinking; the choice and the profile's bytes are kept in the project, so compositing converts once, the
// same way, after reopening.
#include "fk/color.hpp"
#include "fk/hash.hpp"
#include "fk/project.hpp"

namespace fk {

namespace {

bool usable(const std::vector<uint8_t>& icc) {
    return !icc.empty() && Profile::fromIcc(icc).ok();
}

std::string shaOf(const std::vector<uint8_t>& icc) {
    return sha256Hex(icc.data(), icc.size());
}

} // namespace

std::vector<IccChoice> iccChoicesFor(const Sidecar& rec, const std::vector<uint8_t>& scanIcc,
                                     const IccStore& stored) {
    std::vector<IccChoice> known;
    if (usable(scanIcc)) known.push_back({"scan", iccDescription(scanIcc), shaOf(scanIcc), scanIcc});
    const Profile srgb = Profile::sRGB(), adobe = Profile::adobeRGB1998();
    known.push_back({"srgb", srgb.description(), srgb.sha256(), srgb.icc()});
    known.push_back({"adobe_rgb", adobe.description(), adobe.sha256(), adobe.icc()});
    std::vector<IccChoice> out;
    auto add = [&](IccChoice c) {
        for (const auto& o : out)
            if (o.sha256 == c.sha256) return;
        out.push_back(std::move(c));
    };
    // the export's own profile, identified by its SHA-256 (never by its name)
    if (!rec.iccSha256.empty()) {
        for (const auto& k : known)
            if (k.sha256 == rec.iccSha256) add({"export", k.description, k.sha256, k.icc});
        if (auto f = stored.find(rec.iccSha256); f != stored.end() && usable(f->second))
            add({"export", iccDescription(f->second), f->first, f->second});
    }
    for (auto& k : known)
        add(k);
    return out;
}

Result<std::vector<uint8_t>> gradedProfile(const ImageInfo& graded, const RelinkTransform& t,
                                           const Sidecar& rec, const std::vector<uint8_t>& scanIcc,
                                           const IccStore& stored) {
    if (t.set && !t.iccSha256.empty()) {
        if (auto f = stored.find(t.iccSha256); f != stored.end() && usable(f->second)) return f->second;
        for (const auto& c : iccChoicesFor(rec, scanIcc, stored))
            if (c.sha256 == t.iccSha256) return c.icc;
        return makeError(Errc::not_found, "The colour profile chosen when relinking is no longer available");
    }
    if (!graded.icc.empty()) {
        if (usable(graded.icc)) return graded.icc;
        return makeError(Errc::needs_confirmation,
                         "The graded file's colour profile cannot be read; relink it and "
                         "choose how to interpret its colours");
    }
    if (rec.iccSha256.empty()) return std::vector<uint8_t>{}; // untagged export: the scan's own colour space
    if (!t.set) {
        // relinked by an earlier version, which promised "assume the exported profile": only when it is
        // identified
        for (const auto& c : iccChoicesFor(rec, scanIcc, stored))
            if (c.id == "export") return c.icc;
    }
    return makeError(
        Errc::needs_confirmation,
        "The graded file has no colour profile; relink it and choose how to interpret its colours");
}

} // namespace fk
