#include "film_types_json.hpp"
#include "fk/detection.hpp"

#include <nlohmann/json.hpp>

namespace fk {

const std::vector<FilmType>& builtinFilmTypes() {
    static const std::vector<FilmType> types = [] {
        std::vector<FilmType> out;
        const auto j = nlohmann::json::parse(detail::kFilmTypesJson);
        for (const auto& t : j.at("types")) {
            FilmType f;
            f.id = t.at("id").get<std::string>();
            f.name = t.at("name").get<std::string>();
            f.widthMm = t.at("card")[0].get<double>();
            f.heightMm = t.at("card")[1].get<double>();
            const auto& im = t.at("image");
            f.innerShape =
                im.at("shape").get<std::string>() == "ellipse" ? ShapeKind::ellipse : ShapeKind::quad;
            f.imageLeftMm = im.at("left").get<double>();
            f.imageTopMm = im.at("top").get<double>();
            f.imageWidthMm = im.at("width").get<double>();
            f.imageHeightMm = im.at("height").get<double>();
            f.cornerRadiusMm = t.value("cornerRadius", 0.0);
            f.toleranceMm = t.value("tolerance", 4.0);
            f.approximatePlacement = t.value("approximatePlacement", true);
            out.push_back(f);
        }
        return out;
    }();
    return types;
}

const FilmType* findFilmType(const std::string& id) {
    for (const auto& t : builtinFilmTypes())
        if (t.id == id) return &t;
    return nullptr;
}

ConfidenceClass classify(double c) {
    if (c < kLowConfidence) return ConfidenceClass::low;
    if (c < kHighConfidence) return ConfidenceClass::medium;
    return ConfidenceClass::high;
}

} // namespace fk
