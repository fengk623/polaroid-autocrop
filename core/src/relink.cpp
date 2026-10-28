#include "fk/relink.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <regex>

namespace fk {

SignatureAccumulator::SignatureAccumulator(int64_t w, int64_t h)
    : w_(std::max<int64_t>(1, w)), h_(std::max<int64_t>(1, h)),
      sum_(Signature::kSide * Signature::kSide, 0.0), cnt_(Signature::kSide * Signature::kSide, 0.0) {}

void SignatureAccumulator::addRows(int64_t y0, int64_t n, const uint8_t* rows, size_t stride, int channels,
                                   int bits) {
    const int K = Signature::kSide;
    const double normv = bits == 16 ? 65535.0 : 255.0;
    for (int64_t r = 0; r < n; ++r) {
        const int64_t y = y0 + r;
        const int cy = static_cast<int>(std::min<int64_t>(K - 1, y * K / h_));
        const uint8_t* row = rows + static_cast<size_t>(r) * stride;
        for (int64_t x = 0; x < w_; ++x) {
            double v[3];
            const int cc = channels >= 3 ? 3 : 1;
            for (int c = 0; c < cc; ++c) {
                if (bits == 8)
                    v[c] = row[x * channels + c];
                else {
                    uint16_t t;
                    std::memcpy(&t, row + (x * channels + c) * 2, 2);
                    v[c] = t;
                }
            }
            const double Y = cc == 3 ? (0.2126 * v[0] + 0.7152 * v[1] + 0.0722 * v[2]) / normv : v[0] / normv;
            const int cx = static_cast<int>(std::min<int64_t>(K - 1, x * K / w_));
            sum_[cy * K + cx] += Y;
            cnt_[cy * K + cx] += 1;
        }
    }
}

namespace {
Signature normaliseRanks(const std::vector<double>& vals) {
    const size_t n = vals.size();
    std::vector<size_t> idx(n);
    std::iota(idx.begin(), idx.end(), 0);
    std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return vals[a] < vals[b]; });
    std::vector<double> rank(n);
    for (size_t i = 0; i < n;) {
        size_t j = i;
        while (j + 1 < n && vals[idx[j + 1]] == vals[idx[i]])
            ++j;
        const double r = 0.5 * (static_cast<double>(i) + static_cast<double>(j));
        for (size_t k = i; k <= j; ++k)
            rank[idx[k]] = r;
        i = j + 1;
    }
    double mean = 0;
    for (double r : rank)
        mean += r;
    mean /= static_cast<double>(n);
    double ss = 0;
    for (double& r : rank) {
        r -= mean;
        ss += r * r;
    }
    Signature s;
    const double inv = ss > 0 ? 1.0 / std::sqrt(ss) : 0.0;
    for (size_t i = 0; i < n; ++i)
        s.v[i] = static_cast<float>(rank[i] * inv);
    s.valid = ss > 0;
    return s;
}
} // namespace

Signature SignatureAccumulator::finish() const {
    std::vector<double> vals(sum_.size());
    for (size_t i = 0; i < vals.size(); ++i)
        vals[i] = cnt_[i] > 0 ? sum_[i] / cnt_[i] : 0.0;
    return normaliseRanks(vals);
}

Signature computeSignature(const ImageBuffer& img) {
    SignatureAccumulator acc(img.width(), img.height());
    acc.addRows(0, img.height(), img.data(), img.rowBytes(), img.channels(), img.bits());
    return acc.finish();
}

Signature rotateSignature(const Signature& s, int k) {
    const int K = Signature::kSide;
    Signature r = s;
    for (int t = 0; t < ((k % 4) + 4) % 4; ++t) {
        Signature n = r;
        for (int y = 0; y < K; ++y)
            for (int x = 0; x < K; ++x)
                n.v[x * K + (K - 1 - y)] = r.v[y * K + x]; // 90° clockwise
        r = n;
    }
    return r;
}

double compareSignatures(const Signature& a, const Signature& b) {
    if (!a.valid || !b.valid) return 0.0;
    double d = 0;
    for (size_t i = 0; i < a.v.size(); ++i)
        d += static_cast<double>(a.v[i]) * b.v[i];
    return d;
}

namespace {
const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
}

std::string encodeSignature(const Signature& s) {
    std::vector<uint8_t> bytes(s.v.size() * 4);
    for (size_t i = 0; i < s.v.size(); ++i) {
        uint32_t u;
        std::memcpy(&u, &s.v[i], 4);
        bytes[i * 4] = static_cast<uint8_t>(u);
        bytes[i * 4 + 1] = static_cast<uint8_t>(u >> 8);
        bytes[i * 4 + 2] = static_cast<uint8_t>(u >> 16);
        bytes[i * 4 + 3] = static_cast<uint8_t>(u >> 24);
    }
    std::string out;
    out.reserve(bytes.size() * 4 / 3 + 4);
    for (size_t i = 0; i < bytes.size(); i += 3) {
        const uint32_t n = (uint32_t(bytes[i]) << 16) |
                           (i + 1 < bytes.size() ? uint32_t(bytes[i + 1]) << 8 : 0) |
                           (i + 2 < bytes.size() ? uint32_t(bytes[i + 2]) : 0);
        out += kB64[(n >> 18) & 63];
        out += kB64[(n >> 12) & 63];
        out += i + 1 < bytes.size() ? kB64[(n >> 6) & 63] : '=';
        out += i + 2 < bytes.size() ? kB64[n & 63] : '=';
    }
    return out;
}

Result<Signature> decodeSignature(const std::string& b64) {
    std::vector<uint8_t> bytes;
    uint32_t buf = 0;
    int bits = 0;
    for (char c : b64) {
        if (c == '=') break;
        const char* p = std::strchr(kB64, c);
        if (!p || c == 0) return makeError(Errc::parse_error, "Invalid signature encoding");
        buf = (buf << 6) | static_cast<uint32_t>(p - kB64);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            bytes.push_back(static_cast<uint8_t>(buf >> bits));
        }
    }
    Signature s;
    if (bytes.size() != s.v.size() * 4) return makeError(Errc::parse_error, "Signature has the wrong length");
    for (size_t i = 0; i < s.v.size(); ++i) {
        const uint32_t u = uint32_t(bytes[i * 4]) | (uint32_t(bytes[i * 4 + 1]) << 8) |
                           (uint32_t(bytes[i * 4 + 2]) << 16) | (uint32_t(bytes[i * 4 + 3]) << 24);
        std::memcpy(&s.v[i], &u, 4);
    }
    double ss = 0;
    for (float f : s.v)
        ss += static_cast<double>(f) * f;
    s.valid = ss > 0.5;
    return s;
}

std::optional<EditFileName> parseEditFileName(const std::string& stem) {
    static const std::regex re(R"re(^(.*)_P(\d{2,3})_EDIT_v(\d{3,})$)re");
    for (const std::string& cand : sidecarStemCandidates(stem)) {
        std::smatch m;
        if (std::regex_match(cand, m, re)) {
            EditFileName e;
            e.scanStem = m[1].str();
            e.itemIndex = std::stoi(m[2].str());
            e.version = std::stoi(m[3].str());
            return e;
        }
    }
    return std::nullopt;
}

std::vector<std::string> sidecarStemCandidates(const std::string& stem) {
    static const std::regex suffix(R"re((-Edit(-\d+)?|_edit| copy( \d+)?| ?\(\d+\)|-\d+)$)re",
                                   std::regex::icase);
    std::vector<std::string> out{stem};
    std::string cur = stem;
    for (int i = 0; i < 4; ++i) {
        std::smatch m;
        if (!std::regex_search(cur, m, suffix)) break;
        cur = cur.substr(0, static_cast<size_t>(m.position(0)));
        if (cur.empty()) break;
        out.push_back(cur);
    }
    return out;
}

} // namespace fk
