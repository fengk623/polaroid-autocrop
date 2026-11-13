# API-SPEC-core — FrameKeeper core library (`fkcore`)

API version: **1.0** (`FK_API_VERSION_MAJOR=1`, `FK_API_VERSION_MINOR=0`) · Normative · RFC-0001

This document and `core/include/fk/*.hpp` must agree: `tests/core/test_api_contract.cpp` checks the enum values,
constants and function signatures listed here when it compiles, and the stable names when it runs;
`tools/check-claims.py` (CI) checks that every name this document declares exists in the core (FK-013, U027).
Names marked *planned* are not built. Any change: same commit updates this file, `CONTRACT-ui-core.md`,
the RFC, compatibility tests and a migration note (§9).

## 1. Conventions
- Namespace `fk`. C++20. No exceptions cross the API; functions return `fk::Result<T>`.
- Paths: `std::filesystem::path`. Strings: UTF-8.
- Coordinates: RFC-0002 (pixel (x,y) covers [x,x+1)×[y,y+1)).
- Long operations: synchronous, take `const CancelToken&` and `const ProgressFn&` (may be empty), poll
  cancellation at least every 64 rows; on cancel return `Errc::cancelled` and leave no output file.
- Thread-safety: free functions are re-entrant; objects (`ImageSource`, `ImageSink`, `Project`) are not
  shared across threads without external locking.

## 2. Errors (`fk/common.hpp`)
```cpp
enum class Errc : int {
  ok = 0, invalid_argument = 1,
  io_open = 10, io_read = 11, io_write = 12, disk_full = 13, exists = 14, not_found = 15,
  unsupported_format = 20, damaged_file = 21,
  color_profile = 30,
  cancelled = 40, memory_budget = 41, capability = 42,
  needs_review = 50, needs_confirmation = 51, conflict = 52,
  version_too_new = 60, parse_error = 61,
  internal = 99 };
struct Error { Errc code; std::string message; std::string detail; };
template <class T> class Result;   // ok() / value() / error(); Result<void> specialisation
const char* errcName(Errc);        // stable snake_case names, used in JSON and logs
```

## 3. Images (`fk/image.hpp`, `fk/image_io.hpp`)
```cpp
enum class Photometric { min_is_black = 0, min_is_white = 1, rgb = 2 };
enum class Compression { none = 0, lzw = 1, deflate = 2, packbits = 3, jpeg = 4, other = 99 };
enum class FileFormat { tiff = 0, png = 1, jpeg = 2, dng = 3 };
struct ImageInfo { int64_t width, height; int channels; int bitsPerSample; bool hasAlpha;
  Photometric photometric; Compression compression; int predictor; bool tiled;
  uint32_t tileWidth, tileHeight, rowsPerStrip; bool bigTiff; bool planarSeparate;
  double dpiX, dpiY; std::vector<uint8_t> icc; std::string iccDescription; int orientation;
  uint64_t fileSize; int pageCount; FileFormat format; };
class ImageBuffer;   // owning, interleaved, 8 or 16 bit, channels 1–4, accounted in BufferAccounting
class ImageSource { public: virtual const ImageInfo& info() const = 0;
  virtual Result<void> readRows(int64_t y0, int64_t rows, int64_t x0, int64_t width,
                                uint8_t* dst, size_t dstStride) = 0; };
class TiffSource : public ImageSource { /* … */ uint64_t cachedBytes() const; };
Result<std::unique_ptr<TiffSource>> openTiff(const path&, const TiffOpenOptions& = {});
Result<IntegrityReport> checkTiffIntegrity(const path&);
Result<ImageBuffer> readImageFile(const path&, ImageInfo* info = nullptr, MetadataSet* meta = nullptr);
struct WriteOptions { FileFormat format; int bits; Compression compression; int jpegQuality = 100;
  std::vector<uint8_t> icc; double dpiX = 0, dpiY = 0; MetadataSet meta; std::string xmp;
  bool allowBigTiff = true; };
class ImageSink { public: virtual Result<void> begin(SizeI, int channels, int bits) = 0;
  virtual Result<void> writeRows(const uint8_t* rows, int64_t n, size_t stride) = 0;
  virtual Result<void> finish() = 0; virtual void abort() = 0; };
Result<std::unique_ptr<ImageSink>> createSink(const path& finalPath, const WriteOptions&);
```
`readRows` returns samples in the file's depth; MinIsWhite is inverted so 0 = black. Requests should be
in increasing `y0` (ADR-0002).

## 4. Geometry (`fk/geometry.hpp`) — RFC-0002
```cpp
struct Vec2 { double x, y; };  struct SizeI { int64_t w, h; };  struct RectI { int64_t x, y, w, h; };
struct Quad { std::array<Vec2, 4> p; };                       // clockwise from top-left
struct Ellipse { Vec2 c; double rx, ry, angleRad; };
enum class ShapeKind { quad = 0, ellipse = 1 };
struct Shape { ShapeKind kind; Quad quad; Ellipse ellipse; };
struct Mat3 { std::array<double, 9> m; };                     // row-major
enum class GeometryMode { pixel_crop = 0, straightened = 1, standard_size = 2 };
struct ExportGeometry { SizeI outSize; Mat3 outToScan, scanToOut; RectI sourceRect;
  bool resampled; int upright; GeometryMode mode; double angleRad; double scale; };
ExportGeometry computeExportGeometry(const Shape& region, const Quad& frame, GeometryMode, int upright,
                                     std::optional<SizeI> standard, SizeI scan);
double maskWeight(const Shape&, const MaskParams&, double x, double y);
RectI boundingRect(const Shape&, std::optional<SizeI> clampTo = std::nullopt); bool isValid(const Shape&);
Shape expandShape(const Shape&, double px);  double frameAngle(const Quad&);
// boundary editing (UX-03): one geometry for canvas preview, commit and nudges
enum class EditHandle { corner = 0, edge = 1, move = 2, center = 3, radiusX = 4, radiusY = 5 };
struct EditInput { EditHandle handle; int index; Vec2 delta; bool constrain; };
inline constexpr double kMinEditSize = 4.0;
std::optional<Shape> editShape(const Shape& start, const EditInput&);
Vec2 ellipseHandle(const Ellipse&, bool xAxis);  bool isCircle(const Ellipse&);
// quarter turns (export upright, whole-scan view rotation UX-04): exact, integer corners → integer corners
Mat3 quarterTurnMatrix(int k, double w, double h);  SizeI quarterTurnSize(int k, SizeI);
```

## 5. Detection (`fk/detection.hpp`) — RFC-0004
```cpp
struct FilmType { std::string id, name; double widthMm, heightMm, imageWidthMm, imageHeightMm,
  imageLeftMm, imageTopMm, cornerRadiusMm, toleranceMm; ShapeKind innerShape; };
const std::vector<FilmType>& builtinFilmTypes();
struct Proxy { int64_t width, height; double scale; std::vector<float> rgb; };
Result<Proxy> buildProxy(ImageSource&, int longSide, const CancelToken&, const ProgressFn&);
struct DetectParams { int expectedCount = 4; int maxCount = 12; bool refine = true;
  double safetyMarginPx = 1.0; const Template* tmpl = nullptr; };
struct DetectedItem { Shape outer; double cornerRadiusPx; Shape inner; std::string filmTypeId;
  int upright; double confidence, outerConfidence, innerConfidence;
  std::vector<std::string> reasons; std::array<double, 4> refineOffsetPx; };
struct DetectionResult { std::vector<DetectedItem> items; std::vector<std::string> warnings;
  double proxyScale; double secondsProxy, secondsDetect, secondsRefine; };
Result<DetectionResult> detectPrints(ImageSource&, const Proxy&, const DetectParams&,
                                     const CancelToken&, const ProgressFn&);
```
Reason codes: `low_contrast`, `count_mismatch`, `size_implausible`, `touching_split`, `refine_residual`,
`inner_prior_fallback`, `inner_asymmetric`, `template_snap_far`, `dpi_unknown`.

## 6. Export, relink, composite (`fk/export.hpp`, `fk/relink.hpp`, `fk/composite.hpp`)
```cpp
enum class ExportTarget { image_area = 0, full_print = 1 };
struct ColorPolicy { enum Kind { keep = 0, convert = 1, assign = 2, untagged = 3 } kind = keep;
  std::vector<uint8_t> icc; int intent = 1 /*relative*/; bool bpc = true; };
struct ExportRequest { ExportTarget target; Shape region; Quad frame; GeometryMode mode; int upright;
  std::optional<SizeI> standardSize; WriteOptions write; ColorPolicy color; PrivacyPreset privacy;
  path output; bool reviewed = true; bool allowDepthReduction = false; XmpIds ids; };
struct ExportReport { path output; SizeI size; int bits; bool resampled; bool frameBitExact;
  std::vector<std::string> notExactReasons; std::string sha256; ExportGeometry geometry;
  Signature signature; double seconds; uint64_t peakAccountedBytes; };
Result<ExportReport> exportRegion(ImageSource&, const ExportRequest&, const CancelToken&, const ProgressFn&);
path nextFreeVersionPath(const path& dir, const std::string& base, const std::string& ext, int* versionOut = nullptr);

struct Signature { std::array<float, 4096> v; };
Signature computeSignature(const ImageBuffer&);  double compareSignatures(const Signature&, const Signature&);
RelinkCandidate findMatch(const path& edited, const Project&);           // fk/project.hpp
RelinkCheck checkEdited(const Sidecar&, const ImageBuffer& edited, const ImageInfo& editedInfo);

enum class MaskMode { hard_edge = 0, feather = 1, preserve_edge = 2 };
struct CompositeRequest { const Item* item; const Sidecar* edit; const ImageBuffer* edited;
  std::vector<uint8_t> editedIcc; RelinkActions actions; MaskParams mask;
  GeometryMode outputMode; WriteOptions write; ColorPolicy color; path output; XmpIds ids; };
Result<ExportReport> compositeItem(ImageSource&, const CompositeRequest&, const CancelToken&, const ProgressFn&);
// UX-08 natural blend: a print's mask choice (width in mm or px), presets, the one conversion, edge measurement
struct MaskChoice { MaskMode mode; double width; bool mm; };
inline constexpr double kNaturalFeatherMm = 0.05, kNaturalInsetMm = 0.02, kNaturalFeatherPx = 2.4, kNaturalInsetPx = 0.9;
MaskChoice naturalBlend(MaskMode, bool dpiReliable);  MaskParams resolveMask(const MaskChoice&, double dpi);
inline constexpr double kStrongEdgeChange = 10.0;
double edgeChange(const ImageBuffer& composite, const ImageBuffer& scanOnly, const Shape& inner, Vec2 origin,
                  double scale, double pxPerMm);
```

## 7. Project, sidecar, template (`fk/project.hpp`) — RFC-0009, `docs/project-format.md`
`Result<Project> loadProject(const path&, OpenMode*)`, `Status saveProject(const Project&, const path&)`,
`Result<Sidecar> loadSidecar(const path&)`, `Status saveSidecar(const Sidecar&, const path&)`,
`Result<Template> loadTemplate(const path&, OpenMode*)`, `Status saveTemplate(const Template&, const path&)`,
`Template makeTemplate(const std::vector<DetectedItem>&, SizeI scanSize, double dpi, const std::string& name)`,
`Result<Fingerprint> fingerprintFile(const path&, bool full = false, const CancelToken* = nullptr)`
(`Status` = `Result<void>`). JSON schemas: `fk.project/1`, `fk.sidecar/1`, `fk.template/1`.
`ScanRecord::rotation` (UX-04, quarter turns clockwise, display only); `void rotateScanView(ScanRecord&, int delta)`;
`int exportTurns(const ScanRecord&, const ItemRecord&, bool applyUpright)` — a print's upright when uprights are
applied, else the scan's view rotation, never both; `void updateCountWarning(ScanRecord&)` (UX-01).
Compatibility (FK-014, `docs/project-format.md` §Compatibility): `Project::savedBy` (the `app.version` that last
saved the file); `ProjectSettings::unknownJson`, `ScanRecord::unknownJson`, `ItemRecord::unknownJson` and
`Sidecar::unknownJson` hold the keys a later version wrote and this one does not read (a JSON object or empty),
written back unchanged; `Project::unknownJson` does so for top-level keys. In `fk/version.hpp`:
`int compareVersions(const std::string& a, const std::string& b)` — SemVer 2.0 precedence, −1 / 0 / 1, and 0
when either is not a version.

## 8. Colour, metadata, cache, jobs, diagnostics, capabilities
See RFC-0012/13/14/10/15 and ADR-0007. Key types: `Profile`, `Transform`, `MetadataSet`, `PrivacyPreset {keep_all_safe=0, remove_location=1, remove_identity=2}`,
`MemoryStats`, `BufferAccounting`, `CacheLedger` (below).
Planned, not built: a disk cache (`DiskCache`, ADR-0006), a core job queue (`JobQueue`; jobs run in the app,
`app/src/Jobs.cpp`, RFC-0010), capability profiles (`Capabilities {desktop_full, mobile_lite}`, ADR-0007; planned).

In-memory cache budget (`fk/cache.hpp`, RFC-0014 §Memory budget, FK-005):
```cpp
class CacheLedger { public:
  void setBudget(uint64_t); void put(const std::string& key, uint64_t bytes, bool kept = false);
  void touch(const std::string&); void erase(const std::string&); void setPinned(std::set<std::string>);
  uint64_t reserve(uint64_t bytes); void release(uint64_t token); uint64_t reserved() const;
  std::vector<std::string> enforce();               // LRU keys to drop: total ≤ max(budget − reserved, fixed)
  std::vector<std::string> shrinkTo(uint64_t bytes); // memory pressure
  uint64_t bytes() const; uint64_t bytes(const std::string& prefix) const; uint64_t fixedBytes() const;
  size_t count(const std::string& prefix = {}) const; uint64_t evictions() const; };
uint64_t defaultCacheBudget(uint64_t physicalBytes); // RAM/16, 256 MB … 2 GB
struct SystemMemory { uint64_t totalBytes, availableBytes; }; SystemMemory systemMemory(); // fk/memory.hpp
uint64_t TiffSource::cachedBytes() const;            // decoded strip/tile cache + line buffers
```

## 9. C ABI (`fk/fk_c.h`) — planned, not built
For future Swift/Kotlin bindings (ADR-0007). No header, library or test exists yet; nothing ships it.
```c
int fk_api_version(void);                                   /* major*1000 + minor */
const char* fk_last_error(void);                             /* thread-local, UTF-8 */
typedef struct fk_scan fk_scan;
fk_scan* fk_scan_open(const char* utf8_path);  void fk_scan_close(fk_scan*);
char* fk_scan_info_json(fk_scan*);                            /* free with fk_free */
char* fk_detect_json(fk_scan*, int expected_count);           /* items as JSON */
int fk_export_item_json(fk_scan*, const char* request_json, char** report_json);
void fk_free(void*);
```

## 10. Change log
| Version | Change |
| --- | --- |
| 1.0 | Initial API. |
| 0.7.0-alpha.3 | `editShape`, `EditInput`, `EditHandle`, `ellipseHandle`, `isCircle` (UX-03). |
| 0.7.0-alpha.4 | `quarterTurnMatrix`, `quarterTurnSize`, `ScanRecord::rotation`, `rotateScanView`, `exportTurns` (UX-04). |
| 0.7.0-alpha.5 | `MaskChoice`, `naturalBlend`, `resolveMask`, `edgeChange`, `CompositeSettings::mask`/`maskSet`, `deltaE2000` (UX-08). |
| 0.7.0-alpha.7 | `CacheLedger`, `defaultCacheBudget`, `systemMemory`, `TiffSource::cachedBytes` (FK-005). |
| 0.7.0-alpha.15 | `compareVersions`, `Project::savedBy`, `unknownJson` on `ProjectSettings`, `ScanRecord`, `ItemRecord`, `Sidecar` (FK-014). Additive. |
| 0.7.0-alpha.14 | Spec corrected to the headers (FK-013): `openTiff` returns `TiffSource`, `findMatch` / `checkEdited` signatures, `makeTemplate`, `fingerprintFile`, `boundingRect` clamp, `nextFreeVersionPath` version out; disk cache, job queue, capabilities and the C ABI marked planned. No code change. |
