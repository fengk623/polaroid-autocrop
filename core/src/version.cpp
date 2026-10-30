#include "fk/version.hpp"
#include "fk/product_info.hpp"

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
} // namespace fk
