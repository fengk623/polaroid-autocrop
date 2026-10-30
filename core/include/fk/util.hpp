// FrameKeeper core — small utilities (paths, UUIDs).
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace fk {

/// UTF-8 string of a path (portable; C++20 u8string → std::string).
std::string pathToUtf8(const std::filesystem::path& p);
/// Path from a UTF-8 string.
std::filesystem::path pathFromUtf8(const std::string& s);
/// Random UUID v4, lowercase, 36 chars.
std::string newUuid();
/// Random alphanumeric token for temp names.
std::string randomToken(size_t n = 8);
/// Standard base64 (RFC 4648, padded).
std::string base64Encode(const std::vector<uint8_t>& bytes);
/// nullopt on a character outside the alphabet.
std::optional<std::vector<uint8_t>> base64Decode(const std::string& text);

} // namespace fk
