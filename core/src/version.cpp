#include "fk/version.hpp"
#include "fk/product_info.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <vector>

namespace fk {
std::string productVersion() {
    return product::kVersion;
}
std::string productNameAndVersion() {
    return std::string(product::kName) + " " + product::kVersion;
}
std::string buildCommit() {
    return product::kGitSha;
}
std::string buildId() {
    return product::kBuildId;
}
std::string buildInfoLine() {
    return productNameAndVersion() + " (commit " + buildCommit() + ", build " + buildId() + ")";
}
int apiVersion() {
    return FK_API_VERSION_MAJOR * 1000 + FK_API_VERSION_MINOR;
}

namespace {
struct SemVer {
    unsigned long long major = 0, minor = 0, patch = 0;
    std::vector<std::string> pre; ///< dot-separated pre-release identifiers (empty = a release)
};
bool allDigits(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s)
        if (c < '0' || c > '9') return false;
    return true;
}
std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t from = 0;
    for (size_t at = s.find(sep); at != std::string::npos; at = s.find(sep, from)) {
        out.push_back(s.substr(from, at - from));
        from = at + 1;
    }
    out.push_back(s.substr(from));
    return out;
}
std::optional<SemVer> parseSemVer(const std::string& text) {
    const std::string v = text.substr(0, text.find('+')); // build metadata does not count
    const size_t dash = v.find('-');
    const std::vector<std::string> core = split(v.substr(0, dash), '.');
    if (core.size() != 3) return std::nullopt;
    for (const auto& c : core)
        if (!allDigits(c) || c.size() > 18) return std::nullopt;
    SemVer s{std::stoull(core[0]), std::stoull(core[1]), std::stoull(core[2]), {}};
    if (dash != std::string::npos) {
        s.pre = split(v.substr(dash + 1), '.');
        for (const auto& id : s.pre) {
            if (id.empty()) return std::nullopt;
            for (char c : id)
                if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-')) return std::nullopt;
        }
    }
    return s;
}
int cmp(unsigned long long a, unsigned long long b) {
    return a < b ? -1 : (a > b ? 1 : 0);
}
} // namespace

int compareVersions(const std::string& a, const std::string& b) {
    const auto x = parseSemVer(a), y = parseSemVer(b);
    if (!x || !y) return 0;
    if (int c = cmp(x->major, y->major)) return c;
    if (int c = cmp(x->minor, y->minor)) return c;
    if (int c = cmp(x->patch, y->patch)) return c;
    if (x->pre.empty() || y->pre.empty())
        return x->pre.empty() == y->pre.empty() ? 0 : (x->pre.empty() ? 1 : -1);
    for (size_t i = 0; i < x->pre.size() && i < y->pre.size(); ++i) {
        const std::string &p = x->pre[i], &q = y->pre[i];
        const bool pn = allDigits(p), qn = allDigits(q);
        if (pn && qn) {
            // numeric identifiers compare as numbers ("alpha.9" < "alpha.15")
            const std::string pv = p.substr(std::min(p.find_first_not_of('0'), p.size() - 1));
            const std::string qv = q.substr(std::min(q.find_first_not_of('0'), q.size() - 1));
            if (pv.size() != qv.size()) return pv.size() < qv.size() ? -1 : 1;
            if (pv != qv) return pv < qv ? -1 : 1;
        } else if (pn != qn) {
            return pn ? -1 : 1; // numeric identifiers have lower precedence than alphanumeric ones
        } else if (p != q) {
            return p < q ? -1 : 1;
        }
    }
    return cmp(x->pre.size(), y->pre.size());
}
} // namespace fk
