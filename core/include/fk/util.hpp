// FrameKeeper core — small utilities (paths, UUIDs).
#pragma once

#include <filesystem>
#include <string>

namespace fk {

/// UTF-8 string of a path (portable; C++20 u8string → std::string).
std::string pathToUtf8(const std::filesystem::path& p);
/// Path from a UTF-8 string.
std::filesystem::path pathFromUtf8(const std::string& s);
/// Random UUID v4, lowercase, 36 chars.
std::string newUuid();
/// Random alphanumeric token for temp names.
std::string randomToken(size_t n = 8);

} // namespace fk
