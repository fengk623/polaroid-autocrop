#include "fk/util.hpp"

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

} // namespace fk
