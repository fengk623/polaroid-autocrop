// FK-013 (U027): API-SPEC-core.md and the public headers agree. The enum values, constants and function
// signatures the spec lists are checked when this file compiles (a changed signature fails the build); the
// stable names used in JSON and logs are checked when it runs. When the API changes on purpose, this file,
// API-SPEC-core.md (§10 change log) and CONTRACT-ui-core.md change in the same commit.
#include "fk/cache.hpp"
#include "fk/common.hpp"
#include "fk/composite.hpp"
#include "fk/detection.hpp"
#include "fk/export.hpp"
#include "fk/geometry.hpp"
#include "fk/image.hpp"
#include "fk/image_io.hpp"
#include "fk/memory.hpp"
#include "fk/metadata.hpp"
#include "fk/project.hpp"
#include "fk/relink.hpp"
#include "fk/version.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace {
using fk::Errc;
using path = std::filesystem::path;
template <class E> constexpr int v(E e) {
    return static_cast<int>(e);
}

// ---- §2 errors
static_assert(v(Errc::ok) == 0 && v(Errc::invalid_argument) == 1);
static_assert(v(Errc::io_open) == 10 && v(Errc::io_read) == 11 && v(Errc::io_write) == 12 &&
              v(Errc::disk_full) == 13 && v(Errc::exists) == 14 && v(Errc::not_found) == 15);
static_assert(v(Errc::unsupported_format) == 20 && v(Errc::damaged_file) == 21 &&
              v(Errc::color_profile) == 30);
static_assert(v(Errc::cancelled) == 40 && v(Errc::memory_budget) == 41 && v(Errc::capability) == 42);
static_assert(v(Errc::needs_review) == 50 && v(Errc::needs_confirmation) == 51 && v(Errc::conflict) == 52);
static_assert(v(Errc::version_too_new) == 60 && v(Errc::parse_error) == 61 && v(Errc::internal) == 99);
static_assert(std::is_same_v<decltype(&fk::errcName), const char* (*)(Errc)>);
static_assert(std::is_same_v<fk::Status, fk::Result<void>>);

// ---- §3 images
static_assert(v(fk::Photometric::min_is_black) == 0 && v(fk::Photometric::min_is_white) == 1 &&
              v(fk::Photometric::rgb) == 2);
static_assert(v(fk::Compression::none) == 0 && v(fk::Compression::lzw) == 1 &&
              v(fk::Compression::deflate) == 2 && v(fk::Compression::packbits) == 3 &&
              v(fk::Compression::jpeg) == 4 && v(fk::Compression::other) == 99);
static_assert(v(fk::FileFormat::tiff) == 0 && v(fk::FileFormat::png) == 1 && v(fk::FileFormat::jpeg) == 2 &&
              v(fk::FileFormat::dng) == 3);
static_assert(std::is_same_v<decltype(&fk::openTiff), fk::Result<std::unique_ptr<fk::TiffSource>> (*)(
                                                          const path&, const fk::TiffOpenOptions&)>);
static_assert(std::is_base_of_v<fk::ImageSource, fk::TiffSource>);
static_assert(
    std::is_same_v<decltype(&fk::checkTiffIntegrity), fk::Result<fk::IntegrityReport> (*)(const path&)>);
static_assert(std::is_same_v<decltype(&fk::readImageFile),
                             fk::Result<fk::ImageBuffer> (*)(const path&, fk::ImageInfo*, fk::MetadataSet*)>);
static_assert(std::is_same_v<decltype(&fk::createSink), fk::Result<std::unique_ptr<fk::ImageSink>> (*)(
                                                            const path&, const fk::WriteOptions&)>);

// ---- §4 geometry
static_assert(v(fk::ShapeKind::quad) == 0 && v(fk::ShapeKind::ellipse) == 1);
static_assert(v(fk::GeometryMode::pixel_crop) == 0 && v(fk::GeometryMode::straightened) == 1 &&
              v(fk::GeometryMode::standard_size) == 2);
static_assert(v(fk::EditHandle::corner) == 0 && v(fk::EditHandle::edge) == 1 &&
              v(fk::EditHandle::move) == 2 && v(fk::EditHandle::center) == 3 &&
              v(fk::EditHandle::radiusX) == 4 && v(fk::EditHandle::radiusY) == 5);
static_assert(fk::kMinEditSize == 4.0);
static_assert(std::is_same_v<decltype(&fk::computeExportGeometry),
                             fk::ExportGeometry (*)(const fk::Shape&, const fk::Quad&, fk::GeometryMode, int,
                                                    std::optional<fk::SizeI>, fk::SizeI)>);
static_assert(std::is_same_v<decltype(&fk::maskWeight),
                             double (*)(const fk::Shape&, const fk::MaskParams&, double, double)>);
static_assert(
    std::is_same_v<decltype(&fk::boundingRect), fk::RectI (*)(const fk::Shape&, std::optional<fk::SizeI>)>);
static_assert(std::is_same_v<decltype(&fk::expandShape), fk::Shape (*)(const fk::Shape&, double)>);
static_assert(std::is_same_v<decltype(&fk::frameAngle), double (*)(const fk::Quad&)>);
static_assert(std::is_same_v<decltype(&fk::editShape),
                             std::optional<fk::Shape> (*)(const fk::Shape&, const fk::EditInput&)>);
static_assert(std::is_same_v<decltype(&fk::ellipseHandle), fk::Vec2 (*)(const fk::Ellipse&, bool)>);
static_assert(std::is_same_v<decltype(&fk::isCircle), bool (*)(const fk::Ellipse&)>);
static_assert(std::is_same_v<decltype(&fk::quarterTurnMatrix), fk::Mat3 (*)(int, double, double)>);
static_assert(std::is_same_v<decltype(&fk::quarterTurnSize), fk::SizeI (*)(int, fk::SizeI)>);

// ---- §5 detection
static_assert(std::is_same_v<decltype(&fk::builtinFilmTypes), const std::vector<fk::FilmType>& (*)()>);
static_assert(std::is_same_v<decltype(&fk::buildProxy),
                             fk::Result<fk::Proxy> (*)(fk::ImageSource&, int, const fk::CancelToken&,
                                                       const fk::ProgressFn&)>);
static_assert(std::is_same_v<
              decltype(&fk::detectPrints),
              fk::Result<fk::DetectionResult> (*)(fk::ImageSource&, const fk::Proxy&, const fk::DetectParams&,
                                                  const fk::CancelToken&, const fk::ProgressFn&)>);

// ---- §6 export, relink, composite
static_assert(v(fk::ExportTarget::image_area) == 0 && v(fk::ExportTarget::full_print) == 1);
static_assert(fk::ColorPolicy::keep == 0 && fk::ColorPolicy::convert == 1 && fk::ColorPolicy::assign == 2 &&
              fk::ColorPolicy::untagged == 3);
static_assert(v(fk::MaskMode::hard_edge) == 0 && v(fk::MaskMode::feather) == 1 &&
              v(fk::MaskMode::preserve_edge) == 2);
static_assert(
    std::is_same_v<decltype(&fk::exportRegion),
                   fk::Result<fk::ExportReport> (*)(fk::ImageSource&, const fk::ExportRequest&,
                                                    const fk::CancelToken&, const fk::ProgressFn&)>);
static_assert(std::is_same_v<decltype(&fk::nextFreeVersionPath),
                             path (*)(const path&, const std::string&, const std::string&, int*)>);
static_assert(std::is_same_v<decltype(&fk::computeSignature), fk::Signature (*)(const fk::ImageBuffer&)>);
static_assert(
    std::is_same_v<decltype(&fk::compareSignatures), double (*)(const fk::Signature&, const fk::Signature&)>);
static_assert(
    std::is_same_v<decltype(&fk::findMatch), fk::RelinkCandidate (*)(const path&, const fk::Project&)>);
static_assert(
    std::is_same_v<decltype(&fk::checkEdited),
                   fk::RelinkCheck (*)(const fk::Sidecar&, const fk::ImageBuffer&, const fk::ImageInfo&)>);
static_assert(
    std::is_same_v<decltype(&fk::compositeItem),
                   fk::Result<fk::ExportReport> (*)(fk::ImageSource&, const fk::CompositeRequest&,
                                                    const fk::CancelToken&, const fk::ProgressFn&)>);
static_assert(std::is_same_v<decltype(&fk::naturalBlend), fk::MaskChoice (*)(fk::MaskMode, bool)>);
static_assert(std::is_same_v<decltype(&fk::resolveMask), fk::MaskParams (*)(const fk::MaskChoice&, double)>);
static_assert(
    std::is_same_v<decltype(&fk::edgeChange), double (*)(const fk::ImageBuffer&, const fk::ImageBuffer&,
                                                         const fk::Shape&, fk::Vec2, double, double)>);
static_assert(fk::kNaturalFeatherMm == 0.05 && fk::kNaturalInsetMm == 0.02 && fk::kNaturalFeatherPx == 2.4 &&
              fk::kNaturalInsetPx == 0.9 && fk::kStrongEdgeChange == 10.0);

// ---- §7 project, sidecar, template
static_assert(
    std::is_same_v<decltype(&fk::loadProject), fk::Result<fk::Project> (*)(const path&, fk::OpenMode*)>);
static_assert(std::is_same_v<decltype(&fk::saveProject), fk::Status (*)(const fk::Project&, const path&)>);
static_assert(std::is_same_v<decltype(&fk::loadSidecar), fk::Result<fk::Sidecar> (*)(const path&)>);
static_assert(std::is_same_v<decltype(&fk::saveSidecar), fk::Status (*)(const fk::Sidecar&, const path&)>);
static_assert(
    std::is_same_v<decltype(&fk::loadTemplate), fk::Result<fk::Template> (*)(const path&, fk::OpenMode*)>);
static_assert(std::is_same_v<decltype(&fk::saveTemplate), fk::Status (*)(const fk::Template&, const path&)>);
static_assert(
    std::is_same_v<decltype(&fk::makeTemplate), fk::Template (*)(const std::vector<fk::DetectedItem>&,
                                                                 fk::SizeI, double, const std::string&)>);
static_assert(std::is_same_v<decltype(&fk::rotateScanView), void (*)(fk::ScanRecord&, int)>);
static_assert(
    std::is_same_v<decltype(&fk::exportTurns), int (*)(const fk::ScanRecord&, const fk::ItemRecord&, bool)>);
static_assert(std::is_same_v<decltype(&fk::updateCountWarning), void (*)(fk::ScanRecord&)>);
// FK-014 (0.7.0-alpha.15): compatibility fields and the version order
static_assert(std::is_same_v<decltype(fk::Project::savedBy), std::string> &&
              std::is_same_v<decltype(fk::ProjectSettings::unknownJson), std::string> &&
              std::is_same_v<decltype(fk::ScanRecord::unknownJson), std::string> &&
              std::is_same_v<decltype(fk::ItemRecord::unknownJson), std::string> &&
              std::is_same_v<decltype(fk::Sidecar::unknownJson), std::string>);
static_assert(
    std::is_same_v<decltype(&fk::compareVersions), int (*)(const std::string&, const std::string&)>);

// ---- §8 metadata, memory
static_assert(v(fk::PrivacyPreset::keep_all_safe) == 0 && v(fk::PrivacyPreset::remove_location) == 1 &&
              v(fk::PrivacyPreset::remove_identity) == 2);
static_assert(std::is_same_v<decltype(&fk::defaultCacheBudget), uint64_t (*)(uint64_t)>);
static_assert(std::is_same_v<decltype(&fk::systemMemory), fk::SystemMemory (*)()>);
} // namespace

// Names written into project files, sidecars and logs: a rename breaks files written by older versions.
TEST(ApiContract, StableNames) {
    EXPECT_STREQ(fk::errcName(Errc::needs_confirmation), "needs_confirmation");
    EXPECT_STREQ(fk::errcName(Errc::unsupported_format), "unsupported_format");
    EXPECT_STREQ(fk::errcName(Errc::version_too_new), "version_too_new");
    EXPECT_STREQ(fk::geometryModeName(fk::GeometryMode::straightened), "straightened");
    EXPECT_STREQ(fk::maskModeName(fk::MaskMode::preserve_edge), "preserve_edge");
    EXPECT_STREQ(fk::exportTargetName(fk::ExportTarget::image_area), "image_area");
    EXPECT_STREQ(fk::privacyPresetName(fk::PrivacyPreset::remove_identity), "remove_identity");
    EXPECT_STREQ(fk::fileFormatName(fk::FileFormat::dng), "dng");
    EXPECT_STREQ(fk::photometricName(fk::Photometric::min_is_white), "miniswhite");
    EXPECT_STREQ(fk::compressionName(fk::Compression::deflate), "deflate");
}

// API-SPEC header: API version 1.0, FK_API_VERSION_MAJOR / MINOR, apiVersion() = major*1000 + minor.
static_assert(FK_API_VERSION_MAJOR == 1 && FK_API_VERSION_MINOR == 0);
TEST(ApiContract, ApiVersionIsTheSpecs) {
    EXPECT_EQ(fk::apiVersion(), FK_API_VERSION_MAJOR * 1000 + FK_API_VERSION_MINOR);
}
