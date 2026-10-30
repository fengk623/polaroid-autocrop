#include "fk/util.hpp"

#include <cstring>

#include <array>
#include <cstdio>
#include <mutex>
#include <random>

namespace fk {

std::string pathToUtf8(const std::filesystem::path& p) {
    const auto u8 = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

std::filesystem::path pathFromUtf8(const std::string& s) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

namespace {
std::mt19937_64& rng() {
    static std::mt19937_64 g{[] {
        std::random_device rd;
        return (static_cast<uint64_t>(rd()) << 32) ^ rd();
    }()};
    return g;
}
std::mutex& rngMutex() {
    static std::mutex m;
    return m;
}
} // namespace

std::string newUuid() {
    uint64_t a, b;
    {
        std::lock_guard<std::mutex> lk(rngMutex());
        a = rng()();
        b = rng()();
    }
    a = (a & 0xFFFFFFFFFFFF0FFFull) | 0x0000000000004000ull; // version 4
    b = (b & 0x3FFFFFFFFFFFFFFFull) | 0x8000000000000000ull; // variant 10
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%08x-%04x-%04x-%04x-%012llx", static_cast<unsigned>(a >> 32),
                  static_cast<unsigned>((a >> 16) & 0xFFFF), static_cast<unsigned>(a & 0xFFFF),
                  static_cast<unsigned>(b >> 48), static_cast<unsigned long long>(b & 0xFFFFFFFFFFFFull));
    return buf;
}

std::string randomToken(size_t n) {
    static const char* chars = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::string s(n, 'x');
    std::lock_guard<std::mutex> lk(rngMutex());
    for (auto& c : s)
        c = chars[rng()() % 36];
    return s;
}

namespace {
constexpr const char* kBase64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
}

std::string base64Encode(const std::vector<uint8_t>& bytes) {
    std::string out;
    out.reserve(bytes.size() * 4 / 3 + 4);
    for (size_t i = 0; i < bytes.size(); i += 3) {
        const uint32_t n = (uint32_t(bytes[i]) << 16) |
                           (i + 1 < bytes.size() ? uint32_t(bytes[i + 1]) << 8 : 0) |
                           (i + 2 < bytes.size() ? uint32_t(bytes[i + 2]) : 0);
        out += kBase64[(n >> 18) & 63];
        out += kBase64[(n >> 12) & 63];
        out += i + 1 < bytes.size() ? kBase64[(n >> 6) & 63] : '=';
        out += i + 2 < bytes.size() ? kBase64[n & 63] : '=';
    }
    return out;
}

std::optional<std::vector<uint8_t>> base64Decode(const std::string& text) {
    std::vector<uint8_t> out;
    out.reserve(text.size() * 3 / 4);
    uint32_t buf = 0;
    int bits = 0;
    for (char c : text) {
        if (c == '=') break;
        if (c == '\n' || c == '\r') continue;
        const char* p = c ? std::strchr(kBase64, c) : nullptr;
        if (!p) return std::nullopt;
        buf = (buf << 6) | static_cast<uint32_t>(p - kBase64);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>(buf >> bits));
        }
    }
    return out;
}

} // namespace fk
