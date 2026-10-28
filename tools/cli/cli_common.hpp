// fkcli shared helpers.
#pragma once

#include "fk/detection.hpp"
#include "fk/export.hpp"
#include "fk/geometry.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace fkcli {

using json = nlohmann::ordered_json;

json shapeToJson(const fk::Shape& s);
json detectionToJson(const fk::DetectionResult& r);
json infoToJson(const fk::ImageInfo& i);

/// Draws the proxy with outer (blue) and inner (yellow) shapes into an 8-bit RGB PNG.
bool writeOverlay(const std::filesystem::path& out, const fk::Proxy& proxy,
                  const std::vector<fk::DetectedItem>& items,
                  const std::vector<std::vector<fk::Vec2>>* gtOuter = nullptr);

int cmdEval(const std::vector<std::string>& args);

struct Args {
    std::vector<std::string> positional;
    std::vector<std::pair<std::string, std::string>> opts;
    bool has(const std::string& k) const;
    std::string get(const std::string& k, const std::string& def = "") const;
    double num(const std::string& k, double def) const;
    static Args parse(const std::vector<std::string>& a);
};

} // namespace fkcli
