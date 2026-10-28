#pragma once
#include <string>

#define FK_API_VERSION_MAJOR 1
#define FK_API_VERSION_MINOR 0

namespace fk {
/// Product version string from VERSION (e.g. "0.1.0").
std::string productVersion();
/// "FrameKeeper 0.1.0".
std::string productNameAndVersion();
/// API version as major*1000 + minor.
int apiVersion();
} // namespace fk
