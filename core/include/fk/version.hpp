#pragma once
#include <string>

#define FK_API_VERSION_MAJOR 1
#define FK_API_VERSION_MINOR 0

namespace fk {
/// Product version string from VERSION, SemVer with optional pre-release (e.g. "0.6.0-alpha.1").
std::string productVersion();
/// "FrameKeeper 0.6.0-alpha.1".
std::string productNameAndVersion();
/// Full commit SHA of the source this binary was built from ("unknown" outside a Git checkout).
std::string buildCommit();
/// Build identifier: CI run number.attempt, or "local".
std::string buildId();
/// One line for logs, reports and --version: "FrameKeeper 0.6.0-alpha.1 (commit <sha>, build <id>)".
std::string buildInfoLine();
/// API version as major*1000 + minor.
int apiVersion();
} // namespace fk
