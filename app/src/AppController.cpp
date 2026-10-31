#include "AppController.hpp"

#include "util.hpp"

#include "fk/color.hpp"
#include "fk/export.hpp"
#include "fk/hash.hpp"
#include "fk/image_io.hpp"
#include "fk/metadata.hpp"
#include "fk/product_info.hpp"
#include "fk/relink.hpp"
#include "fk/version.hpp"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>
#include <cmath>

namespace fkapp {

namespace {

constexpr int kThumbLong = 320;   // scan thumbnails (left rail)
constexpr int kItemThumbH = 216;  // filmstrip cells are 108 px tall at 2× density
constexpr size_t kDisplayLru = 4; // display images kept in memory (≈ 26 MB each)

QString confidenceClass(double c) {
    if (c >= fk::kHighConfidence) return QStringLiteral("high");
    if (c >= fk::kLowConfidence) return QStringLiteral("medium");
    return QStringLiteral("low");
}

QString sourceName(fk::BoundarySource s) {
    switch (s) {
    case fk::BoundarySource::automatic:
        return QStringLiteral("auto");
    case fk::BoundarySource::templated:
        return QStringLiteral("template");
    default:
        return QStringLiteral("manual");
    }
}

fk::FileFormat formatOf(const QString& f) {
    if (f == QLatin1String("png")) return fk::FileFormat::png;
    if (f == QLatin1String("jpg")) return fk::FileFormat::jpeg;
    if (f == QLatin1String("dng")) return fk::FileFormat::dng;
    return fk::FileFormat::tiff;
}

fk::Compression compressionOf(const QString& c) {
    if (c == QLatin1String("deflate")) return fk::Compression::deflate;
    if (c == QLatin1String("none")) return fk::Compression::none;
    return fk::Compression::lzw;
}

fk::PrivacyPreset privacyOf(const QString& p) {
    if (p == QLatin1String("remove_location")) return fk::PrivacyPreset::remove_location;
    if (p == QLatin1String("remove_identity")) return fk::PrivacyPreset::remove_identity;
    return fk::PrivacyPreset::keep_all_safe;
}

fk::ColorPolicy colorPolicyOf(const QString& c) {
    fk::ColorPolicy p;
    if (c == QLatin1String("convert_srgb")) {
        p.kind = fk::ColorPolicy::convert;
        p.icc = fk::Profile::sRGB().icc();
    } else if (c == QLatin1String("convert_adobe")) {
        p.kind = fk::ColorPolicy::convert;
        p.icc = fk::Profile::adobeRGB1998().icc();
    } else if (c == QLatin1String("untagged")) {
        p.kind = fk::ColorPolicy::untagged;
    }
    return p;
}

QString errText(const fk::Error& e) {
    QString s = QCoreApplication::translate("Errors", e.message.c_str());
    if (!e.detail.empty()) s += QStringLiteral(" (") + QString::fromStdString(e.detail) + QStringLiteral(")");
    return s;
}

std::string stemOf(const std::string& path) {
    return fk::pathToUtf8(fk::pathFromUtf8(path).stem());
}

/// Compositing adjustment of a relinked graded file (FK-002, RFC-0006 §Confirmed transform): the transform
/// confirmed at relink time, applied as stored; relinks made by earlier versions get the turn the picture
/// decides unambiguously. Returns false with `error` when the file changed since it was confirmed or its
/// orientation is uncertain.
bool relinkAdjust(const fk::EditVersion& ev, const fk::Sidecar& rec, const fk::ImageBuffer& edited,
                  const fk::ImageInfo& ei, bool legacyScale, bool legacyDepth, fk::EditAdjust& adjust,
                  QString& error) {
    if (const fk::RelinkTransform& t = ev.relinkTransform; t.set) {
        if (t.gradedSize.w > 0 && !(t.gradedSize == fk::SizeI{edited.width(), edited.height()})) {
            error =
                AppController::tr("The graded file changed since it was relinked (%1 × %2 px, was %3 × %4). "
                                  "Relink it again.")
                    .arg(edited.width())
                    .arg(edited.height())
                    .arg(t.gradedSize.w)
                    .arg(t.gradedSize.h);
            return false;
        }
        adjust.rotate = t.rotate;
        adjust.allowScale = t.scale;
        adjust.allowDepthReduction = t.lowerDepth;
        return true;
    }
    const fk::RelinkCheck chk = fk::checkEdited(rec, edited, ei);
    if (!chk.rotationReliable && chk.allowedRotations.size() > 1) {
        error = AppController::tr("The orientation of the graded file is uncertain. Relink it and choose the "
                                  "orientation.");
        return false;
    }
    adjust.rotate = chk.suggestedRotate;
    adjust.allowScale = legacyScale;
    adjust.allowDepthReduction = legacyDepth;
    return true;
}

/// FK-007 wording of IdentityCheck::reason for a scan.
QString scanChangeText(const std::string& reason) {
    if (reason == "missing") return AppController::tr("The scan file is missing.");
    if (reason == "cancelled") return AppController::tr("Cancelled");
    if (reason == "size")
        return AppController::tr(
            "The scan file has changed since it was imported (different size); it is not used.");
    return AppController::tr(
        "The scan file has changed since it was imported (different content); it is not used.");
}

/// FK-007: every scan an output is made from is verified before it is read (streamed, inside the job) and its
/// stamp is compared after reading; a scan replaced meanwhile never produces an output.
class ScanGuard {
  public:
    /// Empty = verified (kept for the job while the file's stamp stays the same); else the reason.
    QString verify(const fk::ScanRecord& s, const fk::CancelToken& cancel) {
        const auto path = fk::pathFromUtf8(s.path);
        const fk::FileStamp now = fk::fileStamp(path);
        if (auto f = stamps_.find(s.path); f != stamps_.end() && f->second == now) return {};
        const fk::IdentityCheck c = fk::verifyFileIdentity(path, s.fingerprint, &cancel);
        if (c.state != fk::Identity::same || c.reason == "cancelled") {
            stamps_.erase(s.path);
            return scanChangeText(c.state == fk::Identity::same ? c.reason : c.reason);
        }
        stamps_[s.path] = now;
        return {};
    }
    /// False when the file changed since verify(): whatever was made from it is discarded.
    bool unchanged(const fk::ScanRecord& s) const {
        auto f = stamps_.find(s.path);
        return f != stamps_.end() && f->second == fk::fileStamp(fk::pathFromUtf8(s.path));
    }

  private:
    std::map<std::string, fk::FileStamp> stamps_;
};

QString changedWhileReading() {
    return AppController::tr("The scan file changed while it was read; the output was discarded.");
}

/// FK-007: the relinked graded file is still the one confirmed (same bytes), or was graded again with the
/// same size, picture and profile (`regradedSha` set); otherwise the reason it is refused.
QString verifyGraded(const fk::EditVersion& ev, const fk::Sidecar& rec, const fk::ImageBuffer& px,
                     const fk::ImageInfo& ei, const std::string& sha, std::string& regradedSha) {
    if (ev.relinkedSha256.empty() || sha == ev.relinkedSha256) return {};
    if (!ev.relinkTransform.set)
        return AppController::tr("The graded file changed since it was relinked. Relink it again.");
    const fk::GradedRecheck r = fk::recheckGraded(rec, ev.relinkTransform, px, ei);
    if (r.samePicture) {
        regradedSha = sha;
        return {};
    }
    if (r.reason == "size")
        return AppController::tr("The graded file changed size since it was relinked. Relink it again.");
    if (r.reason == "profile")
        return AppController::tr(
            "The graded file's colour profile changed since it was relinked. Relink it again to "
            "confirm how to read its colours.");
    return AppController::tr(
        "The graded file was replaced by a different picture since it was relinked. Relink "
        "it again.");
}

} // namespace

// ---- construction / settings ---------------------------------------------------------------------------

AppController::AppController(QObject* parent)
    : QObject(parent), jobs_(new JobRunner(this)), scans_(new ScanListModel(this)),
      items_(new ItemListModel(this)) {
    const QByteArray dir = qgetenv("FK_SETTINGS_DIR");
    if (!dir.isEmpty())
        qs_ = new QSettings(QDir(QString::fromLocal8Bit(dir)).filePath(QStringLiteral("settings.ini")),
                            QSettings::IniFormat, this);
    else
        qs_ = new QSettings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("FrameKeeper"),
                            QStringLiteral("FrameKeeper"), this);
    loadSettings();
    connect(jobs_, &JobRunner::busyChanged, this, &AppController::busyChanged);
    newProject();
}

AppController::~AppController() {
    jobs_->cancelAll();
    jobs_->waitAll();
}

void AppController::loadSettings() {
    const QVariantMap defaults{{"theme", "dark"},
                               {"language", "system"},
                               {"exportFolder", ""},
                               {"exportFormat", "tif"},
                               {"exportBits", 0},
                               {"exportCompression", "lzw"},
                               {"geometryMode", "crop"},
                               {"applyUpright", true},
                               {"colorPolicy", "keep"},
                               {"privacy", "keep_all_safe"},
                               {"jpegQuality", 95},
                               {"writeSidecar", false}, // portable .fk.json beside exports (UX-07: optional)
                               {"expectedCount", 4},
                               {"safetyMargin", 1.0},
                               {"finalMode", "real"},
                               {"finalFormat", "tif"},
                               {"finalBits", 0},
                               {"frameMode", "sides"},
                               {"frameUnit", "percent"},
                               {"frameTop", 5.0},
                               {"frameRight", 5.0},
                               {"frameBottom", 18.0},
                               {"frameLeft", 5.0},
                               {"frameRatioW", 4},
                               {"frameRatioH", 5},
                               {"frameMinMargin", 4.0},
                               {"frameColorSource", "scanned"},
                               {"frameColor", "#ffffff"},
                               {"maskMode", "hard_edge"},
                               {"featherPx", 0.0},
                               {"preserveEdgePx", 0.0},
                               {"leftRailWidth", 254},
                               {"rightRailWidth", 300},
                               {"showLeftRail", true},
                               {"showRightRail", true},
                               {"showFilmstrip", true},
                               {"rightTab", 0},
                               {"recentProjects", QStringList{}}};
    settings_.clear();
    for (auto it = defaults.begin(); it != defaults.end(); ++it)
        settings_[it.key()] = qs_->value(QStringLiteral("app/") + it.key(), it.value());
}

QVariantMap AppController::settings() const {
    return settings_;
}

void AppController::setSetting(const QString& key, const QVariant& value) {
    if (settings_.value(key) == value) return;
    settings_[key] = value;
    // composite inputs (RFC-0010 §Task safety): previews computed with the old mask are outdated
    if (key == QLatin1String("maskMode") || key == QLatin1String("featherPx") ||
        key == QLatin1String("preserveEdgePx")) {
        ++compositeGen_;
        compositePreviews_.clear();
        previewGen_.clear();
    }
    qs_->setValue(QStringLiteral("app/") + key, value);
    // project-level settings travel with the project (docs/project-format.md)
    auto& ps = project_.settings;
    bool projectKey = true;
    if (key == QLatin1String("expectedCount"))
        ps.expectedCount = value.toInt();
    else if (key == QLatin1String("safetyMargin"))
        ps.safetyMarginPx = value.toDouble();
    else if (key == QLatin1String("exportFolder"))
        ps.exportFolder = toStd(value.toString());
    else if (key == QLatin1String("exportFormat"))
        ps.exportFormat = toStd(value.toString());
    else if (key == QLatin1String("exportBits"))
        ps.exportBits = value.toInt();
    else if (key == QLatin1String("exportCompression"))
        ps.exportCompression = toStd(value.toString());
    else if (key == QLatin1String("colorPolicy"))
        ps.colorPolicy = toStd(value.toString());
    else if (key == QLatin1String("privacy"))
        ps.privacy = toStd(value.toString());
    else
        projectKey = false;
    if (projectKey && !project_.scans.empty()) {
        dirty_ = true;
        Q_EMIT projectChanged();
    }
    Q_EMIT settingsChanged();
    if (key.startsWith(QLatin1String("export")) || key == QLatin1String("geometryMode"))
        Q_EMIT currentChanged();
}

void AppController::setExportFolder(const QVariant& url) {
    setSetting(QStringLiteral("exportFolder"), QDir::cleanPath(localPath(url)));
}

void AppController::cancelJob(int id) {
    jobs_->cancel(id);
}

QString AppController::version() const {
    return QString::fromStdString(fk::productVersion());
}
QString AppController::buildCommit() const {
    return QString::fromStdString(fk::buildCommit());
}
QString AppController::buildId() const {
    return QString::fromStdString(fk::buildId());
}

QString AppController::platform() const {
#if defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#elif defined(Q_OS_WIN)
    return QStringLiteral("windows");
#else
    return QStringLiteral("linux");
#endif
}

void AppController::notify(const QString& text, const QString& kind) {
    Q_EMIT notification(text, kind);
}

// ---- lookup ----------------------------------------------------------------------------------------------

fk::ScanRecord* AppController::findScan(const std::string& id) {
    for (auto& s : project_.scans)
        if (s.scanId == id) return &s;
    return nullptr;
}
const fk::ScanRecord* AppController::findScan(const std::string& id) const {
    for (const auto& s : project_.scans)
        if (s.scanId == id) return &s;
    return nullptr;
}
fk::ItemRecord* AppController::findItemMut(const std::string& id, fk::ScanRecord** scan) {
    for (auto& s : project_.scans)
        for (auto& it : s.items)
            if (it.itemId == id) {
                if (scan) *scan = &s;
                return &it;
            }
    return nullptr;
}
const fk::ItemRecord* AppController::item(const std::string& itemId) const {
    for (const auto& s : project_.scans)
        for (const auto& it : s.items)
            if (it.itemId == itemId) return &it;
    return nullptr;
}
const fk::ScanRecord* AppController::scanOf(const std::string& itemId) const {
    for (const auto& s : project_.scans)
        for (const auto& it : s.items)
            if (it.itemId == itemId) return &s;
    return nullptr;
}

std::vector<std::string> AppController::resolveItems(const QStringList& ids) const {
    std::vector<std::string> out;
    if (ids.isEmpty()) {
        for (const auto& s : project_.scans)
            for (const auto& it : s.items)
                if (!it.excluded) out.push_back(it.itemId);
        return out;
    }
    for (const QString& q : ids)
        if (const auto* it = item(toStd(q)); it && !it->excluded) out.push_back(it->itemId);
    return out;
}

// ---- project --------------------------------------------------------------------------------------------

QString AppController::projectName() const {
    return toQ(project_.name);
}

void AppController::newProject() {
    jobs_->cancelAll();
    ++projectGen_; // results of jobs started for the previous project are outdated from here on
    previewGen_.clear();
    previewJob_.clear();
    project_ = fk::newProject(toStd(tr("Untitled")));
    project_.settings.expectedCount = settings_.value("expectedCount").toInt();
    project_.settings.safetyMarginPx = settings_.value("safetyMargin").toDouble();
    projectPath_.clear();
    dirty_ = readOnly_ = false;
    curScan_.clear();
    curItem_.clear();
    selection_.clear();
    rt_.clear();
    undo_.clear();
    detected_.clear();
    compositePreviews_.clear();
    relinkChecks_.clear();
    rebuildModels();
    Q_EMIT projectChanged();
    Q_EMIT selectionChanged();
    Q_EMIT currentChanged();
}

bool AppController::openProject(const QVariant& url) {
    const QString path = localPath(url);
    fk::OpenMode mode = fk::OpenMode::read_write;
    auto p = fk::loadProject(toPath(path), &mode);
    if (!p) {
        notify(tr("Cannot open the project: %1").arg(errText(p.error())), QStringLiteral("error"));
        return false;
    }
    newProject();
    project_ = std::move(*p);
    projectPath_ = path;
    readOnly_ = mode == fk::OpenMode::read_only;
    if (readOnly_)
        notify(tr("This project was saved by a newer version and is opened read-only."),
               QStringLiteral("warning"));
    const auto& ps = project_.settings;
    settings_["expectedCount"] = ps.expectedCount;
    settings_["safetyMargin"] = ps.safetyMarginPx;
    if (!ps.exportFolder.empty()) settings_["exportFolder"] = toQ(ps.exportFolder);
    settings_["exportFormat"] = toQ(ps.exportFormat);
    settings_["exportBits"] = ps.exportBits;
    settings_["exportCompression"] = toQ(ps.exportCompression);
    settings_["colorPolicy"] = toQ(ps.colorPolicy);
    settings_["privacy"] = toQ(ps.privacy);
    for (const auto& s : project_.scans) {
        ScanRuntime& r = rt_[s.scanId];
        r.state = s.missing   ? QStringLiteral("missing")
                  : s.changed ? QStringLiteral("changed")
                              : QStringLiteral("pending");
        if (s.changed) r.error = scanChangeText(s.changeReason);
        for (const auto& it : s.items)
            detected_[it.itemId] = {it.outer, it.inner};
    }
    QStringList recent = settings_.value("recentProjects").toStringList();
    recent.removeAll(path);
    recent.prepend(path);
    while (recent.size() > 8)
        recent.removeLast();
    setSetting(QStringLiteral("recentProjects"), recent);
    rebuildModels();
    Q_EMIT projectChanged();
    Q_EMIT settingsChanged();
    for (const auto& s : project_.scans)
        if (!s.missing && !s.changed) loadPreview(s.scanId);
    if (!project_.scans.empty()) selectScan(toQ(project_.scans.front().scanId));
    return true;
}

bool AppController::saveProject() {
    if (projectPath_.isEmpty()) return false; // QML asks for a path first
    if (readOnly_) {
        notify(tr("Read-only project: use Save As to keep a copy."), QStringLiteral("warning"));
        return false;
    }
    const auto st = fk::saveProject(project_, toPath(projectPath_));
    if (!st.ok()) {
        notify(tr("Saving failed: %1").arg(errText(st.error())), QStringLiteral("error"));
        return false;
    }
    dirty_ = false;
    Q_EMIT projectChanged();
    return true;
}

bool AppController::saveProjectAs(const QVariant& url) {
    QString path = localPath(url);
    const QString ext = QStringLiteral(".") + QString::fromLatin1(fk::product::kProjectExt);
    if (!path.endsWith(ext)) path += ext;
    if (project_.name == toStd(tr("Untitled"))) project_.name = toStd(QFileInfo(path).completeBaseName());
    projectPath_ = path;
    readOnly_ = false;
    return saveProject();
}

AppController::JobSnapshot AppController::snapshotFor(const std::string& scanId,
                                                      const std::string& itemId) const {
    JobSnapshot s;
    s.project = projectGen_;
    s.scanId = scanId;
    s.itemId = itemId;
    if (auto f = scanGen_.find(scanId); f != scanGen_.end()) s.scan = f->second;
    if (auto f = itemGen_.find(itemId); f != itemGen_.end()) s.item = f->second;
    s.composite = compositeGen_;
    return s;
}

bool AppController::sameScan(const JobSnapshot& s) const {
    if (!sameProject(s) || !findScan(s.scanId)) return false;
    auto f = scanGen_.find(s.scanId);
    return (f == scanGen_.end() ? 0 : f->second) == s.scan;
}

bool AppController::sameItem(const JobSnapshot& s) const {
    if (!sameProject(s) || !item(s.itemId)) return false;
    auto f = itemGen_.find(s.itemId);
    return (f == itemGen_.end() ? 0 : f->second) == s.item;
}

void AppController::invalidateItem(const std::string& itemId) {
    ++itemGen_[itemId];
    if (const fk::ScanRecord* s = scanOf(itemId)) ++scanGen_[s->scanId];
    compositePreviews_.erase(itemId);
    previewGen_.erase(itemId);
}

quint64 AppController::itemGeneration(const std::string& itemId) const {
    auto f = itemGen_.find(itemId);
    return f == itemGen_.end() ? 0 : f->second;
}

quint64 AppController::compositePreviewGeneration(const std::string& itemId) const {
    auto f = previewGen_.find(itemId);
    return f == previewGen_.end() ? 0 : f->second;
}

void AppController::touch(bool structural) {
    dirty_ = true;
    if (structural)
        rebuildModels();
    else
        rebuildModels();
    Q_EMIT projectChanged();
    Q_EMIT currentChanged();
}

void AppController::importScans(const QVariantList& urls) {
    if (readOnly_) return;
    std::string first;
    int added = 0;
    for (const QVariant& u : urls) {
        const QString path = localPath(u);
        const QString suffix = QFileInfo(path).suffix().toLower();
        if (suffix != QLatin1String("tif") && suffix != QLatin1String("tiff")) {
            notify(tr("Skipped %1: only TIFF scans can be imported.").arg(QFileInfo(path).fileName()),
                   QStringLiteral("warning"));
            continue;
        }
        const std::string sp = toStd(QFileInfo(path).absoluteFilePath());
        if (std::any_of(project_.scans.begin(), project_.scans.end(),
                        [&](const fk::ScanRecord& s) { return s.path == sp; }))
            continue;
        fk::ScanRecord s;
        s.scanId = fk::newUuid();
        s.path = sp;
        s.relPath = toStd(QFileInfo(path).fileName());
        s.expectedCount = settings_.value("expectedCount").toInt();
        project_.scans.push_back(s);
        rt_[s.scanId].state = QStringLiteral("detecting");
        if (first.empty()) first = s.scanId;
        ++added;
    }
    if (!added) return;
    dirty_ = true;
    rebuildModels();
    Q_EMIT projectChanged();
    for (auto& s : project_.scans)
        if (rt_[s.scanId].state == QLatin1String("detecting") && s.items.empty() && s.detectedAt.empty())
            startDetect(s.scanId, nullptr);
    if (curScan_.empty()) selectScan(toQ(first));
}

void AppController::importFolder(const QVariant& url) {
    const QString dir = localPath(url);
    QVariantList files;
    QDirIterator it(dir,
                    {QStringLiteral("*.tif"), QStringLiteral("*.tiff"), QStringLiteral("*.TIF"),
                     QStringLiteral("*.TIFF")},
                    QDir::Files);
    QStringList list;
    while (it.hasNext())
        list << it.next();
    list.sort();
    for (const auto& f : list)
        files << f;
    if (files.isEmpty()) notify(tr("No TIFF scans in %1.").arg(dir), QStringLiteral("warning"));
    importScans(files);
}

void AppController::removeScan(const QString& scanId) {
    const std::string id = toStd(scanId);
    auto& v = project_.scans;
    if (const fk::ScanRecord* gone = findScan(id))
        for (const auto& it : gone->items)
            invalidateItem(it.itemId);
    ++scanGen_[id];
    v.erase(std::remove_if(v.begin(), v.end(), [&](const fk::ScanRecord& s) { return s.scanId == id; }),
            v.end());
    rt_.erase(id);
    if (curScan_ == id) {
        curScan_.clear();
        curItem_.clear();
        if (!v.empty()) curScan_ = v.front().scanId;
    }
    dirty_ = true;
    rebuildModels();
    Q_EMIT projectChanged();
    Q_EMIT selectionChanged();
    Q_EMIT currentChanged();
    if (!curScan_.empty()) Q_EMIT scanImageChanged(toQ(curScan_));
}

void AppController::relocateMissing(const QVariant& folderUrl) {
    const QString folder = localPath(folderUrl);
    const QString projDir = projectPath_.isEmpty() ? folder : QFileInfo(projectPath_).absolutePath();
    const int n = fk::relocateScans(project_, toPath(projDir), toPath(folder));
    notify(tr("Relocated %n scan(s).", nullptr, n));
    if (n > 0) {
        for (const auto& s : project_.scans)
            if (!s.missing && !s.changed &&
                (rt_[s.scanId].state == QLatin1String("missing") ||
                 rt_[s.scanId].state == QLatin1String("changed"))) {
                rt_[s.scanId].state = QStringLiteral("pending");
                loadPreview(s.scanId);
            }
        touch(true);
    }
}

// ---- detection / preview jobs
// ------------------------------------------------------------------------------

namespace {
struct DetectResult {
    bool ok = false;
    QString error;
    fk::ImageInfo info;
    std::string iccSha;
    fk::Fingerprint fp;
    fk::DetectionResult det;
    QImage display;
    int proxyScale = 1;
    bool changed = false; ///< FK-007: the file at the path is not the imported scan
    std::string changeReason;
    std::string fullSha256; ///< computed while opening a scan whose fingerprint had none (older projects)
};
} // namespace

void AppController::startDetect(const std::string& scanId, std::shared_ptr<fk::Template> tmpl) {
    fk::ScanRecord* s = findScan(scanId);
    if (!s) return;
    rt_[scanId].state = QStringLiteral("detecting");
    rebuildModels();
    const std::string path = s->path;
    const fk::Fingerprint known = s->fingerprint;
    const int expected = s->expectedCount;
    const double safety = settings_.value("safetyMargin").toDouble();
    auto res = std::make_shared<DetectResult>();
    const JobSnapshot snap = snapshotFor(scanId, {});
    const QString title = tr("Detecting prints — %1").arg(QFileInfo(toQ(path)).fileName());
    jobs_->start(
        Lane::detect, title,
        [res, path, expected, safety, tmpl, known](JobContext& ctx) {
            // FK-007: a scan detected before must still be the same file (its exports refer to it)
            if (!known.quickSha256.empty()) {
                const fk::IdentityCheck c =
                    fk::verifyFileIdentity(fk::pathFromUtf8(path), known, &ctx.cancel);
                if (c.state != fk::Identity::same) {
                    res->error = scanChangeText(c.reason);
                    res->changed = c.state == fk::Identity::changed;
                    res->changeReason = c.reason;
                    return;
                }
            }
            auto src = fk::openTiff(fk::pathFromUtf8(path));
            if (!src) {
                res->error = errText(src.error());
                return;
            }
            res->info = (*src)->info();
            res->iccSha =
                res->info.icc.empty() ? "" : fk::sha256Hex(res->info.icc.data(), res->info.icc.size());
            // the whole file is hashed once at import (streamed), so later checks see any change
            if (auto fp = fk::fingerprintFile(fk::pathFromUtf8(path), true, &ctx.cancel)) res->fp = *fp;
            auto proxy = fk::buildProxy(**src, 3000, ctx.cancel, [&](double f, std::string_view) {
                ctx.progress(0.5 * f, tr("Reading"));
            });
            if (!proxy) {
                res->error = errText(proxy.error());
                return;
            }
            fk::DetectParams p;
            p.expectedCount = expected;
            p.safetyMarginPx = safety;
            p.tmpl = tmpl.get();
            auto det = fk::detectPrints(**src, *proxy, p, ctx.cancel, [&](double f, std::string_view) {
                ctx.progress(0.5 + 0.5 * f, tr("Detecting"));
            });
            if (!det) {
                res->error = errText(det.error());
                return;
            }
            res->det = std::move(*det);
            DisplayConverter conv;
            if (conv.init(res->info)) res->display = conv.convertProxy(*proxy);
            res->proxyScale = proxy->scale;
            res->ok = true;
        },
        [this, res, scanId, snap](const JobOutcome& o) -> JobDone {
            fk::ScanRecord* s = findScan(scanId);
            if (!s || !sameProject(snap)) return JobApply::outdated; // scan removed or project replaced
            ScanRuntime& r = rt_[scanId];
            if (o.cancelled) {
                r.state = s->items.empty() ? QStringLiteral("error") : QStringLiteral("ready");
                r.error = tr("Cancelled");
                rebuildModels();
                return JobApply::applied;
            }
            if (!res->ok) {
                r.state = res->changed ? QStringLiteral("changed") : QStringLiteral("error");
                r.error = res->error;
                s->changed = res->changed;
                s->changeReason = res->changeReason;
                notify(tr("%1: %2").arg(QFileInfo(toQ(s->path)).fileName(), res->error),
                       QStringLiteral("error"));
                rebuildModels();
                return {JobApply::failed, res->error};
            }
            if (!sameScan(snap)) {
                // the prints of this scan were edited while detection ran: never replace newer edits
                r.state = QStringLiteral("ready");
                rebuildModels();
                notify(tr("%1: detection finished, but the prints were edited meanwhile. The result was not "
                          "applied; detect again to replace them.")
                           .arg(QFileInfo(toQ(s->path)).fileName()),
                       QStringLiteral("warning"));
                return JobApply::outdated;
            }
            s->info = res->info;
            s->info.icc.clear();
            s->iccSha256 = res->iccSha;
            s->fingerprint = res->fp;
            s->detectedAt = fk::isoDateTimeNowUtc();
            s->dpiUsed = res->det.dpiUsed;
            s->proxyScale = res->proxyScale;
            s->warnings = res->det.warnings;
            // replacing items drops their history (the user confirmed re-detection in the UI)
            for (const auto& it : s->items)
                undo_.erase(it.itemId);
            for (const auto& it : s->items)
                invalidateItem(it.itemId);
            s->items.clear();
            ++scanGen_[scanId];
            int idx = 1;
            for (const auto& d : res->det.items) {
                fk::ItemRecord it = fk::itemFromDetection(d, idx++);
                detected_[it.itemId] = {it.outer, it.inner};
                s->items.push_back(it);
            }
            r.display = res->display;
            r.proxyScale = res->proxyScale;
            r.thumb = boxDownscale(res->display, kThumbLong);
            r.state = QStringLiteral("ready");
            r.error.clear();
            ++r.rev;
            refreshItemThumbs(scanId);
            dirty_ = true;
            if (curScan_ == scanId && !s->items.empty() && item(curItem_) == nullptr)
                curItem_ = s->items.front().itemId;
            rebuildModels();
            Q_EMIT projectChanged();
            Q_EMIT selectionChanged();
            Q_EMIT currentChanged();
            Q_EMIT scanImageChanged(toQ(scanId));
            for (const auto& w : s->warnings)
                if (QString::fromStdString(w).startsWith(QLatin1String("count_mismatch")))
                    notify(tr("%1: found %2 prints, expected %3 — please review.")
                               .arg(QFileInfo(toQ(s->path)).fileName())
                               .arg(s->items.size())
                               .arg(s->expectedCount),
                           QStringLiteral("warning"));
            return JobApply::applied;
        });
}

void AppController::loadPreview(const std::string& scanId) {
    const fk::ScanRecord* s = findScan(scanId);
    if (!s) return;
    ScanRuntime& r = rt_[scanId];
    if (r.state == QLatin1String("loading")) return;
    r.state = QStringLiteral("loading");
    const std::string path = s->path;
    const fk::Fingerprint known = s->fingerprint;
    auto res = std::make_shared<DetectResult>();
    const JobSnapshot snap = snapshotFor(scanId, {});
    jobs_->start(
        Lane::detect, tr("Opening %1").arg(QFileInfo(toQ(path)).fileName()),
        [res, path, known](JobContext& ctx) {
            // FK-007: verify the file against its fingerprint (streamed, off the UI thread) before showing it
            if (!known.quickSha256.empty()) {
                const fk::IdentityCheck c =
                    fk::verifyFileIdentity(fk::pathFromUtf8(path), known, &ctx.cancel);
                if (c.state != fk::Identity::same || c.reason == "cancelled") {
                    res->error = scanChangeText(c.reason);
                    res->changed = c.state == fk::Identity::changed;
                    res->changeReason = c.reason;
                    return;
                }
                // projects from before U007 have no whole-file hash: add it now, so later checks see every
                // change
                if (known.fullSha256.empty())
                    if (auto fh = fk::sha256File(fk::pathFromUtf8(path), &ctx.cancel)) res->fullSha256 = *fh;
            }
            auto src = fk::openTiff(fk::pathFromUtf8(path));
            if (!src) {
                res->error = errText(src.error());
                return;
            }
            res->info = (*src)->info();
            auto proxy = fk::buildProxy(**src, 3000, ctx.cancel,
                                        [&](double f, std::string_view) { ctx.progress(f, tr("Reading")); });
            if (!proxy) {
                res->error = errText(proxy.error());
                return;
            }
            DisplayConverter conv;
            if (conv.init(res->info)) res->display = conv.convertProxy(*proxy);
            res->proxyScale = proxy->scale;
            res->ok = true;
        },
        [this, res, scanId, snap](const JobOutcome& o) -> JobDone {
            if (!findScan(scanId) || !sameProject(snap)) return JobApply::outdated;
            ScanRuntime& r = rt_[scanId];
            if (o.cancelled) {
                r.state = QStringLiteral("pending");
                rebuildModels();
                return JobApply::applied;
            }
            if (!res->ok) {
                r.state = res->changed ? QStringLiteral("changed") : QStringLiteral("error");
                r.error = res->error;
                if (fk::ScanRecord* sr = findScan(scanId)) {
                    sr->changed = res->changed;
                    sr->changeReason = res->changeReason;
                }
                rebuildModels();
                return {JobApply::failed, res->error};
            }
            if (fk::ScanRecord* sr = findScan(scanId);
                sr && !res->fullSha256.empty() && sr->fingerprint.fullSha256.empty())
                sr->fingerprint.fullSha256 = res->fullSha256; // saved with the next save
            r.display = res->display;
            r.proxyScale = res->proxyScale;
            r.thumb = boxDownscale(res->display, kThumbLong);
            r.state = QStringLiteral("ready");
            ++r.rev;
            refreshItemThumbs(scanId);
            // keep memory bounded: drop display images of scans that are not current (LRU by rev order)
            std::vector<std::pair<quint64, std::string>> loaded;
            for (auto& [id, rr] : rt_)
                if (!rr.display.isNull() && id != curScan_) loaded.push_back({rr.rev, id});
            std::sort(loaded.begin(), loaded.end());
            while (loaded.size() + 1 > kDisplayLru) {
                rt_[loaded.front().second].display = QImage();
                loaded.erase(loaded.begin());
            }
            rebuildModels();
            Q_EMIT scanImageChanged(toQ(scanId));
            return JobApply::applied;
        });
}

void AppController::detectScan(const QString& scanId, const QString& templateName) {
    if (readOnly_) return;
    std::shared_ptr<fk::Template> tmpl;
    if (!templateName.isEmpty()) {
        auto t = fk::loadTemplate(toPath(
            QDir(templatesDir()).filePath(templateName + QString::fromLatin1(fk::product::kTemplateSuffix))));
        if (!t) {
            notify(tr("Cannot read the template: %1").arg(errText(t.error())), QStringLiteral("error"));
            return;
        }
        tmpl = std::make_shared<fk::Template>(*t);
    }
    startDetect(toStd(scanId), tmpl);
}

void AppController::detectSelectedScans(const QString& templateName) {
    for (const auto& s : project_.scans)
        detectScan(toQ(s.scanId), templateName);
}

// ---- selection
// ---------------------------------------------------------------------------------------------

void AppController::selectScan(const QString& scanId) {
    const std::string id = toStd(scanId);
    const fk::ScanRecord* s = findScan(id);
    if (!s) return;
    curScan_ = id;
    if (!item(curItem_) || scanOf(curItem_) != s)
        curItem_ = s->items.empty() ? std::string() : s->items.front().itemId;
    selection_.clear();
    if (!curItem_.empty()) selection_.push_back(curItem_);
    ScanRuntime& r = rt_[id];
    if (r.display.isNull() && r.state != QLatin1String("detecting") && r.state != QLatin1String("missing") &&
        r.state != QLatin1String("changed"))
        loadPreview(id);
    rebuildModels();
    Q_EMIT selectionChanged();
    Q_EMIT currentChanged();
    Q_EMIT scanImageChanged(scanId);
}

void AppController::selectItem(const QString& itemId, bool extend, bool toggle) {
    const std::string id = toStd(itemId);
    const fk::ScanRecord* s = scanOf(id);
    if (!s) return;
    if (toggle) {
        auto it = std::find(selection_.begin(), selection_.end(), id);
        if (it != selection_.end() && selection_.size() > 1)
            selection_.erase(it);
        else if (it == selection_.end())
            selection_.push_back(id);
    } else if (extend && !curItem_.empty()) {
        // range in filmstrip order
        std::vector<std::string> order;
        for (const auto& sc : project_.scans)
            for (const auto& it : sc.items)
                order.push_back(it.itemId);
        auto a = std::find(order.begin(), order.end(), curItem_),
             b = std::find(order.begin(), order.end(), id);
        if (a != order.end() && b != order.end()) {
            if (a > b) std::swap(a, b);
            selection_.assign(a, b + 1);
        }
    } else {
        selection_.assign(1, id);
    }
    const bool scanChanged = curScan_ != s->scanId;
    curScan_ = s->scanId;
    curItem_ = id;
    ScanRuntime& r = rt_[curScan_];
    if (scanChanged && r.display.isNull() && r.state == QLatin1String("ready")) loadPreview(curScan_);
    rebuildModels();
    Q_EMIT selectionChanged();
    Q_EMIT currentChanged();
    if (scanChanged) Q_EMIT scanImageChanged(toQ(curScan_));
}

void AppController::selectAllItems() {
    selection_.clear();
    for (const auto& s : project_.scans)
        for (const auto& it : s.items)
            selection_.push_back(it.itemId);
    rebuildModels();
    Q_EMIT selectionChanged();
}

QStringList AppController::selectedItemIds() const {
    QStringList out;
    for (const auto& s : selection_)
        out << toQ(s);
    return out;
}

void AppController::stepItem(int delta) {
    std::vector<std::string> order;
    for (const auto& sc : project_.scans)
        for (const auto& it : sc.items)
            order.push_back(it.itemId);
    if (order.empty()) return;
    auto pos = std::find(order.begin(), order.end(), curItem_);
    int i = pos == order.end() ? 0 : static_cast<int>(pos - order.begin()) + delta;
    i = std::clamp(i, 0, static_cast<int>(order.size()) - 1);
    selectItem(toQ(order[static_cast<size_t>(i)]));
}

void AppController::stepScan(int delta) {
    if (project_.scans.empty()) return;
    int i = 0;
    for (size_t k = 0; k < project_.scans.size(); ++k)
        if (project_.scans[k].scanId == curScan_) i = static_cast<int>(k);
    i = std::clamp(i + delta, 0, static_cast<int>(project_.scans.size()) - 1);
    selectScan(toQ(project_.scans[static_cast<size_t>(i)].scanId));
}

// ---- edits / undo --------------------------------------------------------------------------------------

AppController::Snapshot AppController::snap(const fk::ItemRecord& it) const {
    return {it.outer, it.inner, it.outerSource, it.innerSource, it.upright, it.reviewed};
}

void AppController::restore(fk::ItemRecord& it, const Snapshot& s) {
    it.outer = s.outer;
    it.inner = s.inner;
    it.outerSource = s.outerSource;
    it.innerSource = s.innerSource;
    it.upright = s.upright;
    it.reviewed = s.reviewed;
}

void AppController::pushUndo(fk::ItemRecord& it) {
    auto& u = undo_[it.itemId];
    u.undo.push_back(snap(it));
    if (u.undo.size() > 100) u.undo.erase(u.undo.begin());
    u.redo.clear();
}

bool AppController::canUndo() const {
    auto it = undo_.find(curItem_);
    return it != undo_.end() && !it->second.undo.empty();
}
bool AppController::canRedo() const {
    auto it = undo_.find(curItem_);
    return it != undo_.end() && !it->second.redo.empty();
}

void AppController::undo() {
    fk::ItemRecord* it = findItemMut(curItem_);
    if (!it || !canUndo()) return;
    auto& u = undo_[curItem_];
    u.redo.push_back(snap(*it));
    restore(*it, u.undo.back());
    u.undo.pop_back();
    invalidateItem(curItem_);
    refreshItemThumbs(curScan_);
    touch(false);
    Q_EMIT geometryChanged(toQ(curItem_));
}

void AppController::redo() {
    fk::ItemRecord* it = findItemMut(curItem_);
    if (!it || !canRedo()) return;
    auto& u = undo_[curItem_];
    u.undo.push_back(snap(*it));
    restore(*it, u.redo.back());
    u.redo.pop_back();
    invalidateItem(curItem_);
    refreshItemThumbs(curScan_);
    touch(false);
    Q_EMIT geometryChanged(toQ(curItem_));
}

void AppController::commitShape(const std::string& itemId, bool outer, const fk::Shape& s) {
    if (readOnly_) return;
    fk::ScanRecord* scan = nullptr;
    fk::ItemRecord* it = findItemMut(itemId, &scan);
    if (!it || !fk::isValid(s)) return;
    const fk::Shape& cur = outer ? it->outer : it->inner;
    if (cur.kind == s.kind && cur.kind == fk::ShapeKind::quad) {
        bool same = true;
        for (int i = 0; i < 4; ++i)
            same = same && fk::norm(cur.quad.p[i] - s.quad.p[i]) < 1e-6;
        if (same) return; // a drag back to the start is not an undo step
    }
    pushUndo(*it);
    if (outer) {
        it->outer = s;
        it->outerSource = fk::BoundarySource::manual;
    } else {
        it->inner = s;
        it->innerSource = fk::BoundarySource::manual;
    }
    invalidateItem(itemId);
    if (scan) refreshItemThumbs(scan->scanId);
    touch(false);
    Q_EMIT geometryChanged(toQ(itemId));
}

void AppController::setOuter(const QString& itemId, const QVariantMap& shape) {
    fk::Shape s;
    if (shapeFromVariant(shape, s) && s.kind == fk::ShapeKind::quad) commitShape(toStd(itemId), true, s);
}

void AppController::setInner(const QString& itemId, const QVariantMap& shape) {
    fk::Shape s;
    if (shapeFromVariant(shape, s)) commitShape(toStd(itemId), false, s);
}

void AppController::setOuterNumeric(const QString& itemId, int corner, double x, double y) {
    const fk::ItemRecord* it = item(toStd(itemId));
    if (!it || corner < 0 || corner > 3) return;
    fk::Shape s = it->outer;
    s.quad.p[corner] = {x, y};
    if (fk::isConvex(s.quad))
        commitShape(it->itemId, true, s);
    else
        notify(tr("That corner would make the outline non-convex."), QStringLiteral("warning"));
}

void AppController::setInnerKind(const QString& itemId, const QString& kind) {
    const fk::ItemRecord* it = item(toStd(itemId));
    if (!it) return;
    const fk::RectI b = fk::boundingRect(it->inner);
    fk::Shape s;
    if (kind == QLatin1String("ellipse") && it->inner.kind == fk::ShapeKind::quad) {
        const fk::Vec2 c = fk::centroid(it->inner);
        const double r = 0.5 * std::min(b.w, b.h);
        s = fk::Shape::fromEllipse({c, r, r, 0});
    } else if (kind == QLatin1String("quad") && it->inner.kind == fk::ShapeKind::ellipse) {
        s = fk::Shape::fromQuad(fk::rectQuad(static_cast<double>(b.x), static_cast<double>(b.y),
                                             static_cast<double>(b.right()),
                                             static_cast<double>(b.bottom())));
    } else {
        return;
    }
    commitShape(it->itemId, false, s);
}

void AppController::resetToDetected(const QString& itemId) {
    fk::ScanRecord* scan = nullptr;
    fk::ItemRecord* it = findItemMut(toStd(itemId), &scan);
    auto d = detected_.find(toStd(itemId));
    if (!it || d == detected_.end()) return;
    pushUndo(*it);
    it->outer = d->second.first;
    it->inner = d->second.second;
    it->outerSource = it->innerSource = fk::BoundarySource::automatic;
    invalidateItem(it->itemId);
    if (scan) refreshItemThumbs(scan->scanId);
    touch(false);
    Q_EMIT geometryChanged(itemId);
}

void AppController::snapOuter(const QString& itemId) {
    const fk::ItemRecord* it = item(toStd(itemId));
    const fk::ScanRecord* s = scanOf(toStd(itemId));
    if (!it || !s) return;
    fk::DetectedItem seed;
    seed.outer = it->outer;
    seed.inner = it->inner;
    seed.cornerRadiusPx = it->cornerRadiusPx;
    const std::string path = s->path, id = it->itemId;
    const double dpi = s->dpiUsed > 0 ? s->dpiUsed : 600;
    auto out = std::make_shared<std::optional<fk::DetectedItem>>();
    const JobSnapshot snap = snapshotFor(s->scanId, id);
    jobs_->start(
        Lane::io, tr("Snapping edges"),
        [seed, path, dpi, out](JobContext& ctx) {
            auto src = fk::openTiff(fk::pathFromUtf8(path));
            if (!src) return;
            auto r = fk::snapToEdges(**src, seed, dpi, ctx.cancel);
            if (r) *out = *r;
        },
        [this, out, id, snap](const JobOutcome& o) -> JobDone {
            if (o.cancelled) return JobApply::applied;
            if (!sameItem(snap)) return JobApply::outdated; // the outline was edited meanwhile: keep the edit
            if (!*out) {
                notify(tr("No clear paper edge near the current outline."), QStringLiteral("warning"));
                return {JobApply::failed, tr("No clear paper edge near the current outline.")};
            }
            commitShape(id, true, (*out)->outer);
            return JobApply::applied;
        });
}

void AppController::rotateUpright(const QString& itemId, int delta) {
    fk::ItemRecord* it = findItemMut(toStd(itemId));
    if (!it) return;
    pushUndo(*it);
    it->upright = ((it->upright + delta) % 4 + 4) % 4;
    invalidateItem(it->itemId);
    touch(false);
}

void AppController::setReviewed(const QString& itemId, bool reviewed) {
    fk::ItemRecord* it = findItemMut(toStd(itemId));
    if (!it || it->reviewed == reviewed) return;
    pushUndo(*it);
    it->reviewed = reviewed;
    touch(false);
}

void AppController::setExcluded(const QString& itemId, bool excluded) {
    fk::ItemRecord* it = findItemMut(toStd(itemId));
    if (!it || it->excluded == excluded) return;
    it->excluded = excluded;
    touch(false);
}

void AppController::toggleExcludedSelection() {
    for (const auto& id : selection_)
        if (fk::ItemRecord* it = findItemMut(id)) it->excluded = !it->excluded;
    touch(false);
}

void AppController::reviewAndNext() {
    if (!curItem_.empty()) setReviewed(toQ(curItem_), true);
    stepItem(1);
}

// ---- models
// ------------------------------------------------------------------------------------------------

QString AppController::statusOf(const fk::ItemRecord& it) const {
    return confidenceClass(it.confidence);
}

void AppController::rebuildModels() {
    std::vector<ScanRow> srows;
    std::vector<ItemRow> irows;
    for (const auto& s : project_.scans) {
        const ScanRuntime* r = nullptr;
        if (auto f = rt_.find(s.scanId); f != rt_.end()) r = &f->second;
        ScanRow row;
        row.scanId = toQ(s.scanId);
        row.name = QFileInfo(toQ(s.path)).fileName();
        row.path = toQ(s.path);
        if (s.info.width > 0) {
            row.sizeText = QStringLiteral("%1 × %2").arg(s.info.width).arg(s.info.height);
            row.depthText = tr("%1-bit").arg(s.info.bitsPerSample);
            row.iccText = s.info.iccDescription.empty() ? tr("Untagged") : toQ(s.info.iccDescription);
            row.dpiText = s.info.dpiX > 0 ? tr("%1 dpi").arg(std::lround(s.info.dpiX)) : tr("DPI unknown");
        }
        row.state = r ? r->state : QStringLiteral("pending");
        if (s.missing) row.state = QStringLiteral("missing");
        row.error = r ? r->error : QString();
        row.thumb = QStringLiteral("image://fk/scan/%1?%2").arg(row.scanId).arg(r ? r->rev : 0);
        row.itemCount = static_cast<int>(s.items.size());
        for (const auto& it : s.items)
            if (!it.reviewed && !it.excluded) ++row.needReview;
        row.current = s.scanId == curScan_;
        srows.push_back(row);
        for (const auto& it : s.items) {
            ItemRow ir;
            ir.itemId = toQ(it.itemId);
            ir.scanId = row.scanId;
            ir.label = toQ(it.label);
            ir.scanName = row.name;
            ir.filmType = toQ(it.filmType);
            ir.thumb = QStringLiteral("image://fk/item/%1?%2").arg(ir.itemId).arg(r ? r->rev : 0);
            ir.confidence = confidenceClass(it.confidence);
            ir.detection =
                it.outerSource == fk::BoundarySource::manual || it.innerSource == fk::BoundarySource::manual
                    ? QStringLiteral("manual")
                    : sourceName(it.outerSource);
            ir.reviewed = it.reviewed;
            ir.excluded = it.excluded;
            ir.exported = !it.edits.empty();
            ir.relinked = std::any_of(it.edits.begin(), it.edits.end(),
                                      [](const fk::EditVersion& e) { return !e.relinkedPath.empty(); });
            ir.composited = !it.composite.lastOutput.empty();
            ir.finalExported = !it.finalExport.empty();
            ir.current = it.itemId == curItem_;
            ir.selected = std::find(selection_.begin(), selection_.end(), it.itemId) != selection_.end();
            ir.edits = static_cast<int>(it.edits.size());
            irows.push_back(ir);
        }
    }
    scans_->setRows(std::move(srows));
    items_->setRows(std::move(irows));
}

void AppController::refreshItemThumbs(const std::string& scanId) {
    auto f = rt_.find(scanId);
    const fk::ScanRecord* s = findScan(scanId);
    if (f == rt_.end() || !s || f->second.display.isNull()) return;
    ScanRuntime& r = f->second;
    const double k = 1.0 / std::max(1, r.proxyScale);
    for (const auto& it : s->items) {
        const fk::RectI b = fk::boundingRect(it.outer);
        QRect rect(static_cast<int>(b.x * k), static_cast<int>(b.y * k),
                   std::max(1, static_cast<int>(b.w * k)), std::max(1, static_cast<int>(b.h * k)));
        rect = rect.intersected(r.display.rect());
        if (rect.isEmpty()) continue;
        QImage crop = r.display.copy(rect);
        // lossless quarter turns for the thumbnail so that prints appear upright
        if (it.upright) {
            QTransform t;
            t.rotate(90.0 * it.upright);
            crop = crop.transformed(t);
        }
        r.itemThumbs[it.itemId] =
            crop.scaledToHeight(std::min(kItemThumbH, crop.height()), Qt::SmoothTransformation);
    }
    ++r.rev;
}

QImage AppController::thumbnail(const QString& id) const {
    const QString kind = id.section(QLatin1Char('/'), 0, 0);
    const std::string key = toStd(id.section(QLatin1Char('/'), 1).section(QLatin1Char('?'), 0, 0));
    if (kind == QLatin1String("scan")) {
        auto f = rt_.find(key);
        return f == rt_.end() ? QImage() : f->second.thumb;
    }
    const fk::ScanRecord* s = scanOf(key);
    if (!s) return {};
    auto f = rt_.find(s->scanId);
    if (f == rt_.end()) return {};
    auto t = f->second.itemThumbs.find(key);
    return t == f->second.itemThumbs.end() ? QImage() : t->second;
}

QVariantMap AppController::counts() const {
    int scans = static_cast<int>(project_.scans.size()), prints = 0, excluded = 0, review = 0, exported = 0,
        relinked = 0, finals = 0;
    for (const auto& s : project_.scans)
        for (const auto& it : s.items) {
            ++prints;
            if (it.excluded)
                ++excluded;
            else if (!it.reviewed)
                ++review;
            if (!it.edits.empty()) ++exported;
            if (std::any_of(it.edits.begin(), it.edits.end(),
                            [](const fk::EditVersion& e) { return !e.relinkedPath.empty(); }))
                ++relinked;
            if (!it.finalExport.empty()) ++finals;
        }
    return {{"scans", scans},       {"prints", prints},     {"excluded", excluded}, {"needReview", review},
            {"exported", exported}, {"relinked", relinked}, {"finals", finals}};
}

QVariantMap AppController::currentScan() const {
    const fk::ScanRecord* s = findScan(curScan_);
    if (!s) return {};
    QVariantMap m;
    m["id"] = toQ(s->scanId);
    m["name"] = QFileInfo(toQ(s->path)).fileName();
    m["path"] = toQ(s->path);
    m["width"] = static_cast<qlonglong>(s->info.width);
    m["height"] = static_cast<qlonglong>(s->info.height);
    m["bits"] = s->info.bitsPerSample;
    m["channels"] = s->info.channels;
    m["icc"] = s->info.iccDescription.empty() ? tr("Untagged") : toQ(s->info.iccDescription);
    m["dpi"] = s->info.dpiX;
    m["dpiUsed"] = s->dpiUsed;
    m["expectedCount"] = s->expectedCount;
    QStringList w;
    for (const auto& x : s->warnings)
        w << toQ(x);
    m["warnings"] = w;
    m["itemCount"] = static_cast<int>(s->items.size());
    if (auto f = rt_.find(s->scanId); f != rt_.end()) {
        m["state"] = f->second.state;
        m["error"] = f->second.error;
        m["proxyScale"] = f->second.proxyScale;
    }
    if (s->missing) m["state"] = QStringLiteral("missing");
    return m;
}

QVariantMap AppController::currentItem() const {
    const fk::ItemRecord* it = item(curItem_);
    const fk::ScanRecord* s = scanOf(curItem_);
    if (!it || !s) return {};
    QVariantMap m;
    m["id"] = toQ(it->itemId);
    m["label"] = toQ(it->label);
    m["scanId"] = toQ(s->scanId);
    m["scanName"] = QFileInfo(toQ(s->path)).fileName();
    m["filmType"] = toQ(it->filmType);
    m["confidence"] = it->confidence;
    m["confidenceClass"] = confidenceClass(it->confidence);
    m["outerConfidence"] = it->outerConfidence;
    m["innerConfidence"] = it->innerConfidence;
    QStringList reasons;
    for (const auto& r : it->reasons)
        reasons << toQ(r);
    m["reasons"] = reasons;
    m["reviewed"] = it->reviewed;
    m["excluded"] = it->excluded;
    m["upright"] = it->upright * 90;
    m["outer"] = shapeToVariant(it->outer);
    m["inner"] = shapeToVariant(it->inner);
    m["outerSource"] = sourceName(it->outerSource);
    m["innerSource"] = sourceName(it->innerSource);
    const double dpi = s->dpiUsed > 0 ? s->dpiUsed : (s->info.dpiX > 0 ? s->info.dpiX : 0);
    const fk::Quad& q = it->outer.quad;
    const double wpx = fk::norm(q.p[1] - q.p[0]), hpx = fk::norm(q.p[3] - q.p[0]);
    m["outerWidthPx"] = wpx;
    m["outerHeightPx"] = hpx;
    m["outerWidthMm"] = dpi > 0 ? wpx / dpi * 25.4 : 0.0;
    m["outerHeightMm"] = dpi > 0 ? hpx / dpi * 25.4 : 0.0;
    m["angleDeg"] = fk::frameAngle(q) * 180.0 / 3.14159265358979323846;
    const fk::RectI ib = fk::boundingRect(it->inner);
    m["innerWidthPx"] = static_cast<qlonglong>(ib.w);
    m["innerHeightPx"] = static_cast<qlonglong>(ib.h);
    QVariantList edits;
    bool relinked = false;
    for (const auto& e : it->edits) {
        edits << QVariantMap{{"version", e.version},
                             {"path", toQ(e.path)},
                             {"format", toQ(e.format)},
                             {"exportedAt", toQ(e.exportedAt)},
                             {"relinkedPath", toQ(e.relinkedPath)},
                             {"matchedBy", toQ(e.matchedBy)},
                             {"transformSet", e.relinkTransform.set},
                             {"rotate", e.relinkTransform.rotate * 90},
                             {"rotateSource", toQ(e.relinkTransform.rotateSource)},
                             {"scaled", e.relinkTransform.scale},
                             {"contentScore", e.relinkTransform.contentScore},
                             {"iccSource", toQ(e.relinkTransform.iccSource)}};
        relinked = relinked || !e.relinkedPath.empty();
    }
    m["edits"] = edits;
    m["relinked"] = relinked;
    m["finalExport"] = toQ(it->finalExport);
    m["relinkCheck"] = relinkChecks_.count(it->itemId) ? relinkChecks_.at(it->itemId) : QVariantMap();
    const bool crop = settings_.value("geometryMode").toString() == QLatin1String("crop");
    const QString fmt = settings_.value("exportFormat").toString();
    m["frameBitExact"] = crop && fmt != QLatin1String("jpg");
    m["notExactReason"] = !crop                         ? tr("straightened: rotated and resampled")
                          : fmt == QLatin1String("jpg") ? tr("JPEG is lossy")
                                                        : QString();
    m["hasCompositePreview"] = compositePreviews_.count(it->itemId) > 0;
    return m;
}

// ---- canvas support ------------------------------------------------------------------------------------

AppController::CanvasScan AppController::canvasScan() const {
    CanvasScan c;
    const fk::ScanRecord* s = findScan(curScan_);
    if (!s) return c;
    if (auto f = rt_.find(curScan_); f != rt_.end()) {
        c.display = f->second.display;
        c.proxyScale = f->second.proxyScale;
        c.state = f->second.state;
        c.error = f->second.error;
    }
    if (s->missing) c.state = QStringLiteral("missing");
    c.width = s->info.width;
    c.height = s->info.height;
    c.ready = !c.display.isNull() && c.width > 0;
    for (const auto& it : s->items)
        c.items.push_back(&it);
    return c;
}

void AppController::requestDetail(QRectF r, int outW, int outH, quint64 generation, DetailFn done) {
    const fk::ScanRecord* s = findScan(curScan_);
    if (!s || s->missing || outW <= 0 || outH <= 0) return;
    ScanRuntime& rt = rt_[curScan_];
    const std::string path = s->path;
    auto mtx = rt.sourceMutex;
    auto srcHolder = std::make_shared<std::shared_ptr<fk::TiffSource>>(rt.source);
    auto result = std::make_shared<QImage>();
    const std::string scanId = curScan_;
    jobs_->start(
        Lane::io, QString(),
        [=](JobContext& ctx) {
            std::lock_guard<std::mutex> lk(*mtx);
            if (!*srcHolder) {
                fk::TiffOpenOptions o;
                o.cacheBudgetBytes = 96ull << 20;
                auto src = fk::openTiff(fk::pathFromUtf8(path), o);
                if (!src) return;
                *srcHolder = std::shared_ptr<fk::TiffSource>(std::move(*src));
            }
            fk::TiffSource& src = **srcHolder;
            const fk::ImageInfo& info = src.info();
            DisplayConverter conv;
            if (!conv.init(info)) return;
            const int64_t x0 = std::clamp<int64_t>(static_cast<int64_t>(std::floor(r.left())), 0, info.width);
            const int64_t y0 = std::clamp<int64_t>(static_cast<int64_t>(std::floor(r.top())), 0, info.height);
            const int64_t x1 = std::clamp<int64_t>(static_cast<int64_t>(std::ceil(r.right())), 0, info.width);
            const int64_t y1 =
                std::clamp<int64_t>(static_cast<int64_t>(std::ceil(r.bottom())), 0, info.height);
            if (x1 <= x0 || y1 <= y0) return;
            const int64_t w = x1 - x0, h = y1 - y0;
            // integer box reduction to at most ~2× the output, then a smooth scale
            const int k = std::max<int64_t>(1, std::min(w / std::max(1, outW), h / std::max(1, outH)));
            const int64_t ow = w / k, oh = h / k;
            if (ow <= 0 || oh <= 0) return;
            QImage img(static_cast<int>(ow), static_cast<int>(oh), QImage::Format_RGB32);
            std::vector<uint8_t> band(info.bytesPerPixel() * static_cast<size_t>(w) * 64);
            std::vector<uint32_t> row(static_cast<size_t>(w));
            std::vector<uint32_t> acc(static_cast<size_t>(ow) * 3);
            for (int64_t oy = 0; oy < oh; ++oy) {
                if (ctx.cancel.cancelled()) return;
                std::fill(acc.begin(), acc.end(), 0u);
                for (int dy = 0; dy < k; ++dy) {
                    const int64_t y = y0 + oy * k + dy;
                    if (!src.readRows(y, 1, x0, w, band.data(), info.bytesPerPixel() * static_cast<size_t>(w))
                             .ok())
                        return;
                    conv.convertRow(band.data(), w, row.data());
                    for (int64_t ox = 0; ox < ow; ++ox)
                        for (int dx = 0; dx < k; ++dx) {
                            const uint32_t p = row[static_cast<size_t>(ox * k + dx)];
                            acc[ox * 3] += (p >> 16) & 0xFF;
                            acc[ox * 3 + 1] += (p >> 8) & 0xFF;
                            acc[ox * 3 + 2] += p & 0xFF;
                        }
                }
                auto* dst = reinterpret_cast<uint32_t*>(img.scanLine(static_cast<int>(oy)));
                const uint32_t n = static_cast<uint32_t>(k * k);
                for (int64_t ox = 0; ox < ow; ++ox)
                    dst[ox] = 0xFF000000u | ((acc[ox * 3] / n) << 16) | ((acc[ox * 3 + 1] / n) << 8) |
                              (acc[ox * 3 + 2] / n);
            }
            *result = std::move(img);
        },
        [this, result, srcHolder, r, generation, done, scanId](const JobOutcome& o) -> JobDone {
            if (auto f = rt_.find(scanId); f != rt_.end() && !f->second.source) f->second.source = *srcHolder;
            if (!o.cancelled && !result->isNull())
                done(*result, r, generation); // the canvas checks its generation
            return JobApply::applied;
        });
}

void AppController::requestCompositePreview(const QString& itemId) {
    const fk::ItemRecord* it = item(toStd(itemId));
    const fk::ScanRecord* s = scanOf(toStd(itemId));
    if (!it || !s) return;
    const fk::EditVersion* ev = nullptr;
    for (const auto& e : it->edits)
        if (!e.relinkedPath.empty()) ev = &e;
    if (!ev) return;
    const std::string scanPath = s->path, editPath = ev->relinkedPath, id = it->itemId;
    const fk::ItemRecord itemCopy = *it;
    const fk::EditVersion evCopy = *ev;
    const fk::ScanRecord scanCopy = *s;
    const fk::IccStore iccStore = project_.iccProfiles;
    const fk::Result<fk::Sidecar> record = fk::exportRecordFor(project_, *ev);
    const QVariantMap check = relinkChecks_.count(id) ? relinkChecks_.at(id) : QVariantMap();
    const QVariantMap st = settings_;
    auto out = std::make_shared<QImage>();
    auto err = std::make_shared<QString>();
    const JobSnapshot snap = snapshotFor(s->scanId, id);
    // coalesce: a newer request supersedes the one still in flight for this print
    if (auto f = previewJob_.find(id); f != previewJob_.end()) jobs_->cancel(f->second);
    previewJob_[id] = jobs_->start(
        Lane::exportJob, tr("Composite preview %1").arg(toQ(it->label)),
        [=](JobContext& ctx) {
            if (QString e = ScanGuard().verify(scanCopy, ctx.cancel); !e.isEmpty()) {
                *err = e;
                return;
            }
            auto src = fk::openTiff(fk::pathFromUtf8(scanPath));
            if (!src) {
                *err = errText(src.error());
                return;
            }
            const auto& sc = record;
            if (!sc) {
                *err = errText(sc.error());
                return;
            }
            fk::ImageInfo ei;
            auto gradedSha = fk::sha256File(fk::pathFromUtf8(editPath), &ctx.cancel);
            auto edited = fk::readImageFile(fk::pathFromUtf8(editPath), &ei);
            if (!edited) {
                *err = errText(edited.error());
                return;
            }
            std::string regraded;
            if (!gradedSha) {
                *err = errText(gradedSha.error());
                return;
            }
            if (QString e = verifyGraded(evCopy, *sc, *edited, ei, *gradedSha, regraded); !e.isEmpty()) {
                *err = e;
                return;
            }
            fk::CompositeRequest req;
            req.outerRegion = itemCopy.outer;
            req.frame = itemCopy.outer.quad;
            req.inner = itemCopy.inner;
            req.scanToEdit = sc->geometry.scanToOut;
            req.editSize = sc->exportSize;
            req.editExact = !sc->geometry.resampled;
            req.edited = &*edited;
            auto prof = fk::gradedProfile(ei, evCopy.relinkTransform, *sc, (*src)->info().icc, iccStore);
            if (!prof) {
                *err = errText(prof.error());
                return;
            }
            req.editedIcc = *prof;
            if (!relinkAdjust(evCopy, *sc, *edited, ei, true, true, req.adjust, *err)) return;
            req.mask.mode = st.value("maskMode").toString() == QLatin1String("feather")
                                ? fk::MaskMode::feather
                            : st.value("maskMode").toString() == QLatin1String("preserve_edge")
                                ? fk::MaskMode::preserve_edge
                                : fk::MaskMode::hard_edge;
            req.mask.featherPx = st.value("featherPx").toDouble();
            req.mask.preserveEdgePx = st.value("preserveEdgePx").toDouble();
            req.strokes = itemCopy.composite.strokes;
            const fk::RectI b = fk::boundingRect(itemCopy.outer);
            const double scale = std::min(1.0, 2400.0 / static_cast<double>(std::max(b.w, b.h)));
            auto buf = fk::compositeToBuffer(**src, req, scale, ctx.cancel);
            if (!buf) {
                *err = errText(buf.error());
                return;
            }
            fk::ImageInfo info = (*src)->info();
            info.channels = buf->channels();
            info.bitsPerSample = buf->bits();
            info.photometric = info.channels >= 3 ? fk::Photometric::rgb : fk::Photometric::min_is_black;
            DisplayConverter conv;
            if (!conv.init(info)) return;
            QImage img(static_cast<int>(buf->width()), static_cast<int>(buf->height()), QImage::Format_RGB32);
            for (int64_t y = 0; y < buf->height(); ++y)
                conv.convertRow(buf->row(y), buf->width(),
                                reinterpret_cast<uint32_t*>(img.scanLine(static_cast<int>(y))));
            *out = std::move(img);
        },
        [this, out, err, id, snap](const JobOutcome& o) -> JobDone {
            if (auto f = previewJob_.find(id); f != previewJob_.end() && sameProject(snap))
                previewJob_.erase(f);
            if (o.cancelled) return JobApply::applied;
            // the boundary, orientation, relinked file or mask changed while this ran: drop the result
            if (!sameComposite(snap)) return JobApply::outdated;
            if (!err->isEmpty()) {
                notify(tr("Composite preview failed: %1").arg(*err), QStringLiteral("error"));
                return {JobApply::failed, *err};
            }
            if (out->isNull()) return {JobApply::failed, tr("Composite preview failed")};
            compositePreviews_[id] = *out;
            previewGen_[id] = snap.item;
            Q_EMIT compositePreviewReady(toQ(id));
            Q_EMIT currentChanged();
            return JobApply::applied;
        });
}

QImage AppController::compositePreview(const std::string& itemId) const {
    auto f = compositePreviews_.find(itemId);
    return f == compositePreviews_.end() ? QImage() : f->second;
}

bool AppController::waitIdle(int timeoutMs) {
    QElapsedTimer t;
    t.start();
    while (jobs_->busy()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (t.elapsed() > timeoutMs) return false;
    }
    QCoreApplication::processEvents();
    return true;
}

// ---- export -------------------------------------------------------------------------------------------

QString AppController::defaultExportFolder() const {
    const QString s = settings_.value("exportFolder").toString();
    if (!s.isEmpty()) return s;
    if (const fk::ScanRecord* sc = findScan(curScan_))
        return QDir(QFileInfo(toQ(sc->path)).absolutePath()).filePath(QStringLiteral("FrameKeeper Export"));
    return QDir(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation))
        .filePath(QStringLiteral("FrameKeeper Export"));
}

QString AppController::fileUrl(const QString& path) const {
    return QUrl::fromLocalFile(path).toString();
}

fk::ExportRequest AppController::baseExportRequest(const fk::ItemRecord& it, bool fullPrint) const {
    fk::ExportRequest r;
    r.target = fullPrint ? fk::ExportTarget::full_print : fk::ExportTarget::image_area;
    r.region = fullPrint ? it.outer : it.inner;
    r.frame = it.outer.quad;
    r.mode = settings_.value("geometryMode").toString() == QLatin1String("straighten")
                 ? fk::GeometryMode::straightened
                 : fk::GeometryMode::pixel_crop;
    r.upright = settings_.value("applyUpright").toBool() ? it.upright : 0;
    r.write.format = formatOf(settings_.value("exportFormat").toString());
    r.write.bits = settings_.value("exportBits").toInt();
    r.write.compression = compressionOf(settings_.value("exportCompression").toString());
    r.write.jpegQuality = settings_.value("jpegQuality").toInt();
    r.color = colorPolicyOf(settings_.value("colorPolicy").toString());
    r.privacy = privacyOf(settings_.value("privacy").toString());
    r.reviewed = true; // confirmed in prepareExport
    r.allowDepthReduction = true;
    return r;
}

QVariantMap AppController::prepareExport(const QStringList& itemIds) {
    const auto ids = resolveItems(itemIds);
    QVariantList unreviewed;
    for (const auto& id : ids) {
        const fk::ItemRecord* it = item(id);
        if (it && !it->reviewed && it->confidence < fk::kLowConfidence)
            unreviewed << QVariantMap{{"id", toQ(id)},
                                      {"label", toQ(it->label)},
                                      {"scan", QFileInfo(toQ(scanOf(id)->path)).fileName()}};
    }
    const int bits = settings_.value("exportBits").toInt();
    bool reduces = false;
    for (const auto& id : ids)
        if (const fk::ScanRecord* s = scanOf(id); s && bits == 8 && s->info.bitsPerSample == 16)
            reduces = true;
    return {{"count", static_cast<int>(ids.size())},
            {"unreviewed", unreviewed},
            {"folder", defaultExportFolder()},
            {"reducesDepth", reduces || settings_.value("exportFormat").toString() == QLatin1String("jpg")}};
}

namespace {
struct ExportJobItem {
    fk::ScanRecord scan;
    fk::ItemRecord item;
    fk::ExportRequest req;
};
struct ExportOutcome {
    std::string itemId;
    bool ok = false;
    QString error;
    fk::EditVersion ev;
    std::optional<fk::Sidecar> record; ///< export record kept in the project (UX-07)
    std::string fullPath;
    std::string regradedSha256; ///< FK-007: graded file graded again (same picture) — new hash to record
};

} // namespace

void AppController::exportImageAreas(const QStringList& itemIds) {
    const auto ids = resolveItems(itemIds);
    if (ids.empty()) return;
    const QString folder = defaultExportFolder();
    QDir().mkpath(folder);
    std::vector<ExportJobItem> work;
    for (const auto& id : ids) {
        const fk::ItemRecord* it = item(id);
        const fk::ScanRecord* s = scanOf(id);
        if (!it || !s || s->missing) continue;
        work.push_back({*s, *it, baseExportRequest(*it, false)});
    }
    const fk::Project projCopy = project_;
    auto outcomes = std::make_shared<std::vector<ExportOutcome>>();
    const std::string dir = toStd(folder);
    const bool writeSidecar = settings_.value("writeSidecar").toBool();
    const JobSnapshot snap = snapshotFor({}, {});
    jobs_->start(
        Lane::exportJob, tr("Exporting %n image area(s)", nullptr, static_cast<int>(work.size())),
        [work, projCopy, outcomes, dir, writeSidecar](JobContext& ctx) {
            std::string openPath;
            std::unique_ptr<fk::TiffSource> src;
            ScanGuard guard;
            for (size_t i = 0; i < work.size(); ++i) {
                if (ctx.cancel.cancelled()) return;
                const auto& w = work[i];
                ExportOutcome o;
                o.itemId = w.item.itemId;
                if (QString e = guard.verify(w.scan, ctx.cancel); !e.isEmpty()) {
                    o.error = e;
                    outcomes->push_back(o);
                    openPath.clear();
                    continue;
                }
                if (openPath != w.scan.path) {
                    auto s = fk::openTiff(fk::pathFromUtf8(w.scan.path));
                    if (!s) {
                        o.error = errText(s.error());
                        outcomes->push_back(o);
                        continue;
                    }
                    src = std::move(*s);
                    openPath = w.scan.path;
                }
                fk::ExportRequest req = w.req;
                int version = 1;
                const std::string base = stemOf(w.scan.path) + "_" + w.item.label + "_EDIT";
                req.output = fk::nextFreeVersionPath(fk::pathFromUtf8(dir), base,
                                                     fk::fileExtension(req.write.format), &version);
                fk::EditVersion ev;
                ev.editVersionId = fk::newUuid();
                ev.version = version;
                req.ids = fk::XmpIds{projCopy.projectId, w.scan.scanId, w.item.itemId, ev.editVersionId,
                                     fk::newUuid(),      version,       w.item.label,  "image_area"};
                auto rep = fk::exportRegion(*src, req, ctx.cancel, [&](double f, std::string_view) {
                    ctx.progress((static_cast<double>(i) + f) / static_cast<double>(work.size()),
                                 QString::fromStdString(w.item.label));
                });
                if (!rep) {
                    o.error = errText(rep.error());
                    outcomes->push_back(o);
                    continue;
                }
                if (!guard.unchanged(w.scan)) {
                    std::error_code ec;
                    std::filesystem::remove(rep->output, ec);
                    o.error = changedWhileReading();
                    outcomes->push_back(o);
                    openPath.clear();
                    continue;
                }
                ev.path = fk::pathToUtf8(rep->output);
                ev.exportedAt = fk::isoDateTimeNowUtc();
                ev.fileSha256 = rep->sha256;
                ev.format = fk::fileExtension(req.write.format);
                const fk::Sidecar sc = fk::makeSidecar(projCopy, w.scan, w.item, ev, *rep);
                // the record lives in the project; a portable sidecar file is optional (Settings)
                if (writeSidecar) {
                    ev.sidecar = fk::sidecarPathFor(ev.path);
                    const auto st = fk::saveSidecar(sc, fk::pathFromUtf8(ev.sidecar));
                    if (!st.ok()) {
                        o.error = errText(st.error());
                        outcomes->push_back(o);
                        continue;
                    }
                }
                o.ok = true;
                o.ev = ev;
                o.record = sc;
                outcomes->push_back(o);
            }
        },
        [this, outcomes, folder, snap](const JobOutcome& jo) -> JobDone {
            int ok = 0, failed = 0;
            const bool sameProj = sameProject(snap);
            for (const auto& o : *outcomes) {
                if (!o.ok) {
                    ++failed;
                    if (const fk::ItemRecord* it = sameProj ? item(o.itemId) : nullptr)
                        notify(tr("%1: %2").arg(toQ(it->label), o.error), QStringLiteral("error"));
                    continue;
                }
                // export records describe the geometry that was exported, so they stay valid after later
                // edits; they are never written into a project opened after the export started
                if (fk::ItemRecord* it = sameProj ? findItemMut(o.itemId) : nullptr) {
                    it->edits.push_back(o.ev);
                    if (o.record) project_.exportRecords.push_back(*o.record);
                    ++ok;
                }
            }
            if (jo.cancelled)
                notify(tr("Export cancelled; finished files were kept."), QStringLiteral("warning"));
            if (!sameProj) {
                notify(
                    tr("An export of the previous project finished; its files were kept but not recorded."),
                    QStringLiteral("warning"));
                return JobApply::outdated;
            }
            if (ok) touch(false);
            Q_EMIT exportFinished(folder, ok, failed);
            if (failed) return {JobApply::failed, tr("%1 of %2 exports failed").arg(failed).arg(ok + failed)};
            return JobApply::applied;
        });
}

void AppController::exportFullPrints(const QStringList& itemIds) {
    const auto ids = resolveItems(itemIds);
    if (ids.empty()) return;
    const QString folder = defaultExportFolder();
    QDir().mkpath(folder);
    std::vector<ExportJobItem> work;
    for (const auto& id : ids) {
        const fk::ItemRecord* it = item(id);
        const fk::ScanRecord* s = scanOf(id);
        if (!it || !s || s->missing) continue;
        work.push_back({*s, *it, baseExportRequest(*it, true)});
    }
    auto outcomes = std::make_shared<std::vector<ExportOutcome>>();
    const std::string dir = toStd(folder);
    const std::string projectId = project_.projectId;
    const JobSnapshot snap = snapshotFor({}, {});
    jobs_->start(
        Lane::exportJob, tr("Exporting %n full print(s)", nullptr, static_cast<int>(work.size())),
        [work, outcomes, dir, projectId](JobContext& ctx) {
            ScanGuard guard;
            for (size_t i = 0; i < work.size(); ++i) {
                if (ctx.cancel.cancelled()) return;
                const auto& w = work[i];
                ExportOutcome o;
                o.itemId = w.item.itemId;
                if (QString e = guard.verify(w.scan, ctx.cancel); !e.isEmpty()) {
                    o.error = e;
                    outcomes->push_back(o);
                    continue;
                }
                auto s = fk::openTiff(fk::pathFromUtf8(w.scan.path));
                if (!s) {
                    o.error = errText(s.error());
                    outcomes->push_back(o);
                    continue;
                }
                fk::ExportRequest req = w.req;
                int version = 1;
                req.output = fk::nextFreeVersionPath(fk::pathFromUtf8(dir),
                                                     stemOf(w.scan.path) + "_" + w.item.label + "_FULL",
                                                     fk::fileExtension(req.write.format), &version);
                req.ids = fk::XmpIds{projectId,     w.scan.scanId, w.item.itemId, "",
                                     fk::newUuid(), version,       w.item.label,  "real_scan"};
                auto rep = fk::exportRegion(**s, req, ctx.cancel, [&](double f, std::string_view) {
                    ctx.progress((static_cast<double>(i) + f) / static_cast<double>(work.size()),
                                 QString::fromStdString(w.item.label));
                });
                if (!rep)
                    o.error = errText(rep.error());
                else if (!guard.unchanged(w.scan)) {
                    std::error_code ec;
                    std::filesystem::remove(rep->output, ec);
                    o.error = changedWhileReading();
                } else {
                    o.ok = true;
                    o.fullPath = fk::pathToUtf8(rep->output);
                }
                outcomes->push_back(o);
            }
        },
        [this, outcomes, folder, snap](const JobOutcome&) -> JobDone {
            int ok = 0, failed = 0;
            for (const auto& o : *outcomes) {
                if (o.ok)
                    ++ok;
                else {
                    ++failed;
                    notify(o.error, QStringLiteral("error"));
                }
            }
            if (!sameProject(snap)) return JobApply::outdated; // files kept; nothing to record
            Q_EMIT exportFinished(folder, ok, failed);
            if (failed) return {JobApply::failed, tr("%1 of %2 exports failed").arg(failed).arg(ok + failed)};
            return JobApply::applied;
        });
}

// ---- relink -------------------------------------------------------------------------------------------

namespace {

/// "file_hash+embedded_ids+content": the supporting evidence of an option (stored as EditVersion::matchedBy).
QString supportSummary(const fk::RelinkOption& o) {
    QStringList k;
    for (const auto& e : o.evidence)
        if (e.supports) k << QString::fromLatin1(fk::evidenceKindName(e.kind));
    if (o.newAlignment) k << QStringLiteral("manual_alignment");
    k.removeDuplicates();
    return k.join(QLatin1Char('+'));
}

fk::Result<fk::ImageBuffer> bufferOf(const QImage& image) {
    const QImage im = image.convertToFormat(QImage::Format_RGB888);
    auto buf = fk::ImageBuffer::create(im.width(), im.height(), 3, 8);
    if (!buf) return buf.error();
    for (int y = 0; y < im.height(); ++y)
        std::memcpy(buf->row(y), im.constScanLine(y), static_cast<size_t>(im.width()) * 3);
    return buf;
}

const QStringList kChoiceKeys = {"itemId",
                                 "editVersionId",
                                 "label",
                                 "scanName",
                                 "source",
                                 "actions",
                                 "score",
                                 "signaturePass",
                                 "sizeMatches",
                                 "rotate",
                                 "depthLower",
                                 "ok",
                                 "newAlignment",
                                 "alignmentKey",
                                 "conflict",
                                 "alignmentMode",
                                 "rotationReliable",
                                 "rotateSource",
                                 "allowedRotations",
                                 "rotationScores",
                                 "gradedW",
                                 "gradedH",
                                 "iccChoices",
                                 "iccChoice",
                                 "iccSha256",
                                 "iccInvalid",
                                 "gradedIccSha256"};

/// Copies the chosen option into the row's top-level fields (what applyRelink and the dialog read).
void selectOption(QVariantMap& row, int choice, bool accept) {
    const QVariantList opts = row.value("options").toList();
    row["choice"] = choice;
    row["matched"] = !opts.isEmpty();
    if (choice < 0 || choice >= opts.size()) {
        for (const auto& k : kChoiceKeys)
            row.remove(k);
        row["ok"] = false;
        row["accept"] = false;
        return;
    }
    const QVariantMap o = opts[choice].toMap();
    for (const auto& k : kChoiceKeys)
        row[k] = o.value(k);
    row["accept"] = accept && o.value("ok").toBool();
}

} // namespace

void AppController::relinkFiles(const QVariantList& urls, const QString& targetItemId, bool update) {
    std::vector<std::string> files;
    for (const auto& u : urls)
        files.push_back(toStd(localPath(u)));
    if (files.empty()) return;
    const fk::Project projCopy = project_;
    const std::string target = toStd(targetItemId);
    // the target's current image area, sampled from the scan preview (content check of a new alignment)
    QImage preview;
    int proxyScale = 1;
    if (const fk::ScanRecord* s = target.empty() ? nullptr : scanOf(target)) {
        if (auto r = rt_.find(s->scanId); r != rt_.end()) {
            preview = r->second.display;
            proxyScale = r->second.proxyScale;
        }
    }
    auto rows = std::make_shared<QVariantList>();
    auto aligned = std::make_shared<std::map<QString, fk::Sidecar>>();
    auto profiles = std::make_shared<std::map<std::string, std::vector<uint8_t>>>();
    const JobSnapshot snap = snapshotFor({}, {});
    jobs_->start(
        Lane::exportJob, tr("Checking %n graded file(s)", nullptr, static_cast<int>(files.size())),
        [files, projCopy, target, preview, proxyScale, rows, aligned, profiles](JobContext& ctx) {
            std::map<std::string, std::vector<uint8_t>> scanIccCache; // scan path → its embedded profile
            auto scanIccOf = [&](const std::string& scanId) -> const std::vector<uint8_t>& {
                static const std::vector<uint8_t> none;
                for (const auto& s : projCopy.scans)
                    if (s.scanId == scanId) {
                        auto f = scanIccCache.find(s.path);
                        if (f == scanIccCache.end()) {
                            auto src = fk::openTiff(fk::pathFromUtf8(s.path));
                            f = scanIccCache
                                    .emplace(s.path, src ? (*src)->info().icc : std::vector<uint8_t>{})
                                    .first;
                        }
                        return f->second;
                    }
                return none;
            };
            const fk::ScanRecord* tScan = nullptr;
            const fk::ItemRecord* tItem = nullptr;
            for (const auto& s : projCopy.scans)
                for (const auto& it : s.items)
                    if (it.itemId == target) {
                        tScan = &s;
                        tItem = &it;
                    }
            std::optional<fk::ImageBuffer> previewBuf;
            if (tItem && !preview.isNull())
                if (auto buf = bufferOf(preview)) previewBuf = std::move(buf.value());
            auto where = [&](const std::string& itemId) {
                QVariantMap m;
                for (const auto& s : projCopy.scans)
                    for (const auto& it : s.items)
                        if (it.itemId == itemId) {
                            m["label"] = toQ(it.label);
                            m["scanName"] = fromPath(fk::pathFromUtf8(s.path).filename());
                        }
                return m;
            };
            for (size_t i = 0; i < files.size(); ++i) {
                if (ctx.cancel.cancelled()) return;
                ctx.progress(static_cast<double>(i) / files.size(), QString());
                const auto path = fk::pathFromUtf8(files[i]);
                QVariantMap row{{"file", toQ(files[i])},
                                {"fileName", fromPath(path.filename())},
                                {"target", toQ(target)},
                                {"options", QVariantList()}};
                fk::ImageInfo info;
                auto img = fk::readImageFile(path, &info);
                if (!img) {
                    row["decision"] = QStringLiteral("none");
                    selectOption(row, -1, false);
                    row["message"] = errText(img.error());
                    rows->push_back(row);
                    continue;
                }
                const fk::RelinkInput in = fk::describeGradedFile(path, *img);
                row["sha256"] = toQ(in.sha256);
                // a new alignment places the file on the print's current image area, shaped like the file
                fk::ExportGeometry ag;
                fk::Signature area;
                if (tItem) {
                    ag = previewBuf ? fk::alignmentGeometry(*tScan, *tItem, in.size, *previewBuf, proxyScale,
                                                            in.signature)
                                    : fk::alignmentGeometry(*tScan, *tItem, in.size);
                    if (previewBuf) area = fk::signatureOfExport(*previewBuf, ag, proxyScale);
                }
                const fk::RelinkAssessment a =
                    fk::assessRelink(in, projCopy, target, area.valid ? &area : nullptr);
                const QString decision = QString::fromLatin1(fk::relinkDecisionName(a.decision));
                row["decision"] = decision;
                QVariantList options;
                if (a.decision != fk::RelinkDecision::none) {
                    for (const fk::RelinkOption& o : a.options) {
                        // list what some evidence speaks for (other prints stay reachable via "Assign to …")
                        const bool supported =
                            o.hashMatch || o.contentPass || o.newAlignment || !target.empty() ||
                            std::any_of(o.evidence.begin(), o.evidence.end(), [](const fk::Evidence& e) {
                                return e.supports && (e.kind == fk::EvidenceKind::embedded_ids ||
                                                      e.kind == fk::EvidenceKind::file_name);
                            });
                        if (!supported) continue;
                        QVariantMap om = where(o.itemId);
                        om["itemId"] = toQ(o.itemId);
                        om["newAlignment"] = o.newAlignment;
                        om["conflict"] = o.conflict;
                        om["contentScore"] = o.contentScore;
                        om["contentPass"] = o.contentPass;
                        om["source"] = supportSummary(o);
                        QVariantList ev;
                        for (const auto& e : o.evidence) {
                            QString text;
                            switch (e.kind) {
                            case fk::EvidenceKind::file_hash:
                                text = tr("identical to the exported file");
                                break;
                            case fk::EvidenceKind::embedded_ids:
                                text = e.supports ? tr("embedded IDs name this print")
                                                  : tr("embedded IDs name another print");
                                break;
                            case fk::EvidenceKind::file_name:
                                text = e.supports ? tr("file name names this print")
                                                  : tr("file name names another print");
                                break;
                            case fk::EvidenceKind::content:
                                text =
                                    (o.contentPass ? tr("picture matches (%1 %)")
                                     : e.supports ? tr("picture is similar, below the match threshold (%1 %)")
                                                  : tr("picture differs (%1 %)"))
                                        .arg(qRound(o.contentScore * 100));
                                break;
                            case fk::EvidenceKind::user_target:
                                text = tr("chosen by you");
                                break;
                            }
                            ev << QVariantMap{{"kind", QString::fromLatin1(fk::evidenceKindName(e.kind))},
                                              {"supports", e.supports},
                                              {"text", text}};
                        }
                        om["evidence"] = ev;
                        fk::Sidecar rec;
                        if (o.newAlignment) {
                            if (!tItem || o.itemId != tItem->itemId) continue;
                            rec = fk::alignmentRecord(projCopy, *tScan, *tItem, ag, area,
                                                      fk::pathToUtf8(path.filename()), in.sha256);
                            const QString key = toQ(files[i]) + QLatin1Char('|') + toQ(o.itemId);
                            (*aligned)[key] = rec;
                            om["alignmentKey"] = key;
                        } else if (const fk::Sidecar* r = fk::findExportRecord(projCopy, o.editVersionId)) {
                            rec = *r;
                        } else {
                            continue;
                        }
                        om["alignmentMode"] = QString::fromLatin1(fk::geometryModeName(rec.geometry.mode));
                        om["editVersionId"] = toQ(rec.editVersionId);
                        om["version"] = rec.version;
                        const fk::RelinkCheck chk = fk::checkEdited(rec, *img, info);
                        QStringList actions;
                        for (const auto& x : chk.actionsNeeded)
                            actions << toQ(x);
                        om["actions"] = actions;
                        om["score"] = chk.signatureScore;
                        om["signaturePass"] = chk.signaturePass;
                        om["sizeMatches"] = chk.sizeMatches;
                        om["rotate"] = chk.suggestedRotate * 90;
                        om["rotationReliable"] = chk.rotationReliable;
                        om["rotateSource"] =
                            chk.rotationReliable
                                ? (chk.suggestedRotate ? QStringLiteral("content") : QStringLiteral("none"))
                                : QString();
                        QVariantList allowed, scores;
                        for (int k : chk.allowedRotations)
                            allowed << k * 90;
                        for (double x : chk.rotationScores)
                            scores << x;
                        om["allowedRotations"] = allowed;
                        om["rotationScores"] = scores;
                        om["gradedW"] = static_cast<qlonglong>(img->width());
                        om["gradedH"] = static_cast<qlonglong>(img->height());
                        om["depthLower"] = chk.depthLower;
                        // FK-003: no usable embedded profile → the user confirms how to read the colours
                        om["iccInvalid"] = chk.iccInvalid;
                        om["gradedIccSha256"] = info.icc.empty()
                                                    ? QString()
                                                    : toQ(fk::sha256Hex(info.icc.data(), info.icc.size()));
                        if (chk.iccMissing || chk.iccInvalid) {
                            QVariantList choices;
                            QString def, defSha;
                            for (const auto& c :
                                 fk::iccChoicesFor(rec, scanIccOf(o.scanId), projCopy.iccProfiles)) {
                                const QString d = toQ(c.description);
                                const QString label = c.id == "export"
                                                          ? tr("Profile of the export: %1").arg(d)
                                                      : c.id == "scan" ? tr("Profile of the scan: %1").arg(d)
                                                                       : d;
                                choices << QVariantMap{
                                    {"id", toQ(c.id)}, {"label", label}, {"sha256", toQ(c.sha256)}};
                                (*profiles)[c.sha256] = c.icc;
                                // pre-selected only for a missing profile whose export profile is identified;
                                // an unreadable one is never guessed
                                if (c.id == "export" && chk.iccMissing && !chk.iccInvalid) {
                                    def = toQ(c.id);
                                    defSha = toQ(c.sha256);
                                }
                            }
                            om["iccChoices"] = choices;
                            om["iccChoice"] = def;
                            om["iccSha256"] = defSha;
                        }
                        const bool placeable = !actions.contains(QStringLiteral("manual_alignment"));
                        om["ok"] = placeable;
                        if (!placeable)
                            om["message"] =
                                o.newAlignment
                                    ? tr("The graded file's aspect ratio differs from this print's image "
                                         "area. "
                                         "Crop or resize it to the image area first; it is never stretched.")
                                    : tr("The graded file's aspect ratio differs from the export; it cannot "
                                         "be "
                                         "placed without stretching.");
                        // pre-accepted only when nothing needs confirming: same size, no turn, clear picture
                        om["acceptDefault"] = a.decision == fk::RelinkDecision::unique && placeable &&
                                              !o.newAlignment && chk.signaturePass && chk.sizeMatches &&
                                              chk.rotationReliable && chk.suggestedRotate == 0 &&
                                              !chk.iccMissing && !chk.iccInvalid;
                        options << om;
                    }
                }
                row["options"] = options;
                const bool unique = a.decision == fk::RelinkDecision::unique && !options.isEmpty();
                selectOption(row, unique ? 0 : -1,
                             unique && options.first().toMap().value("acceptDefault").toBool());
                if (options.isEmpty())
                    row["message"] = target.empty()
                                         ? tr("No matching print. Choose the print this file belongs to.")
                                         : tr("This print cannot take the file.");
                else if (!unique)
                    row["message"] =
                        std::any_of(options.begin(), options.end(),
                                    [](const QVariant& v) { return v.toMap().value("conflict").toBool(); })
                            ? tr("The evidence disagrees. Check which print this file belongs to.")
                        : options.size() > 1
                            ? tr("Several prints or exports fit. Choose one.")
                            : tr("Only the picture matches. Confirm the print or choose another.");
                else if (!row.value("ok").toBool())
                    row["message"] = options.first().toMap().value("message");
                else if (row.value("newAlignment").toBool())
                    row["message"] = row.value("alignmentMode").toString() == QLatin1String("pixel_crop")
                                         ? tr("New alignment: this print has no export in this project. The "
                                              "graded image is "
                                              "placed on its current image area as an unrotated crop.")
                                         : tr("New alignment: this print has no export in this project. The "
                                              "graded image is "
                                              "placed on its current image area, straightened.");
                rows->push_back(row);
            }
        },
        [this, rows, aligned, profiles, snap, update](const JobOutcome& o) -> JobDone {
            if (o.cancelled) return JobApply::applied;
            if (!sameProject(snap)) return JobApply::outdated; // rows refer to the previous project's prints
            for (auto& [sha, bytes] : *profiles)
                knownIcc_[sha] = bytes;
            for (auto& [k, rec] : *aligned)
                pendingAlignments_[k] = rec;
            if (update)
                for (const auto& r : *rows)
                    Q_EMIT relinkRowUpdated(r.toMap());
            else
                Q_EMIT relinkResults(*rows);
            return JobApply::applied;
        });
}

QVariantList AppController::relinkTargets() const {
    QVariantList out;
    for (const auto& s : project_.scans)
        for (const auto& it : s.items)
            out << QVariantMap{{"id", toQ(it.itemId)},
                               {"label", toQ(it.label)},
                               {"scanName", fromPath(fk::pathFromUtf8(s.path).filename())}};
    return out;
}

void AppController::dropFiles(const QVariantList& urls) {
    QVariantList scans, graded;
    for (const auto& u : urls) {
        const QString file = localPath(u);
        const QString lower = file.toLower();
        if (lower.endsWith(QLatin1String(".fkproj"))) {
            openProject(u);
            return;
        }
        bool isGraded = !(lower.endsWith(QLatin1String(".tif")) || lower.endsWith(QLatin1String(".tiff")));
        if (!isGraded) {
            // a TIFF is a graded file when it carries our IDs or an export name; anything else is a scan
            if (fk::parseEditFileName(fk::pathToUtf8(fk::pathFromUtf8(toStd(file)).stem())))
                isGraded = true;
            else if (auto xmp = fk::readXmpPacket(fk::pathFromUtf8(toStd(file))); xmp && !xmp->empty())
                isGraded = !fk::parseXmpIds(*xmp).itemId.empty();
        }
        (isGraded ? graded : scans) << u;
    }
    if (!scans.isEmpty()) importScans(scans);
    if (!graded.isEmpty()) relinkFiles(graded);
}

QVariantMap AppController::loadIccProfile(const QVariant& url) {
    QFile f(localPath(url));
    if (!f.open(QIODevice::ReadOnly)) return {{"ok", false}, {"error", tr("Cannot read the file.")}};
    const QByteArray data = f.readAll();
    const std::vector<uint8_t> bytes(data.begin(), data.end());
    auto prof = fk::Profile::fromIcc(bytes);
    if (!prof)
        return {{"ok", false}, {"error", tr("Not a usable ICC profile: %1").arg(errText(prof.error()))}};
    knownIcc_[prof->sha256()] = bytes;
    return {{"ok", true},
            {"id", QStringLiteral("file")},
            {"label", tr("Profile file: %1").arg(toQ(prof->description()))},
            {"sha256", toQ(prof->sha256())}};
}

void AppController::applyRelink(const QVariantList& rows) {
    int n = 0, changed = 0, unoriented = 0, noProfile = 0;
    for (const QVariant& v : rows) {
        const QVariantMap r = v.toMap();
        if (!r.value("accept").toBool() || !r.value("ok").toBool()) continue;
        fk::ItemRecord* it = findItemMut(toStd(r.value("itemId").toString()));
        if (!it) continue;
        const QStringList actions = r.value("actions").toStringList();
        if (actions.contains(QStringLiteral("choose_rotation")) &&
            r.value("rotateSource").toString() != QLatin1String("user")) {
            ++unoriented; // the picture cannot tell the turn: the user must choose it (FK-002)
            continue;
        }
        const bool needsIcc = actions.contains(QStringLiteral("choose_icc"));
        const std::string iccSha = toStd(r.value("iccSha256").toString());
        const auto iccBytes = knownIcc_.find(iccSha);
        if (needsIcc && (iccSha.empty() || iccBytes == knownIcc_.end())) {
            ++noProfile; // no usable profile: how to read the colours must be confirmed (FK-003)
            continue;
        }
        fk::EditVersion* ev = nullptr;
        if (r.value("newAlignment").toBool()) {
            auto pa = pendingAlignments_.find(r.value("alignmentKey").toString());
            if (pa == pendingAlignments_.end()) continue;
            const fk::Sidecar rec = pa->second;
            pendingAlignments_.erase(pa);
            if (!(rec.inner == it->inner) || !(rec.outer == it->outer)) {
                ++changed; // the print was edited after the check: its image area is not what was compared
                continue;
            }
            project_.exportRecords.push_back(rec);
            fk::EditVersion nv;
            nv.editVersionId = rec.editVersionId;
            nv.version = rec.version;
            nv.format = toStd(QFileInfo(r.value("file").toString()).suffix().toLower());
            it->edits.push_back(nv);
            ev = &it->edits.back();
        } else {
            for (auto& e : it->edits)
                if (toQ(e.editVersionId) == r.value("editVersionId").toString()) ev = &e;
            if (!ev) continue;
        }
        ev->relinkedPath = toStd(r.value("file").toString());
        ev->relinkedSha256 = toStd(r.value("sha256").toString());
        ev->matchedBy = toStd(r.value("source").toString());
        ev->relinkedAt = fk::isoDateTimeNowUtc();
        fk::RelinkTransform& t = ev->relinkTransform;
        t.set = true;
        t.rotate = ((r.value("rotate").toInt() / 90) % 4 + 4) % 4;
        t.rotateSource = toStd(r.value("rotateSource").toString());
        if (t.rotateSource.empty()) t.rotateSource = t.rotate ? "content" : "none";
        t.scale = actions.contains(QStringLiteral("scale"));
        t.lowerDepth = actions.contains(QStringLiteral("accept_lower_depth"));
        const QVariantList scores = r.value("rotationScores").toList();
        t.contentScore = scores.size() == 4 ? scores[t.rotate].toDouble() : r.value("score").toDouble();
        t.gradedSize = {r.value("gradedW").toLongLong(), r.value("gradedH").toLongLong()};
        t.iccSource.clear();
        t.iccSha256.clear();
        t.gradedIccSha256 = toStd(r.value("gradedIccSha256").toString());
        t.gradedIccRecorded = true;
        if (needsIcc) {
            t.iccSource = toStd(r.value("iccChoice").toString());
            t.iccSha256 = iccSha;
            project_.iccProfiles[iccSha] = iccBytes->second;
        }
        relinkChecks_[it->itemId] = r;
        invalidateItem(it->itemId);
        ++n;
    }
    if (changed)
        notify(tr("%n print(s) changed after the check; check the graded file again.", nullptr, changed),
               QStringLiteral("warning"));
    if (unoriented)
        notify(tr("%n file(s) not relinked: choose the orientation first.", nullptr, unoriented),
               QStringLiteral("warning"));
    if (noProfile)
        notify(tr("%n file(s) not relinked: choose how to read their colours first.", nullptr, noProfile),
               QStringLiteral("warning"));
    if (n) {
        touch(false);
        notify(tr("Relinked %n graded file(s).", nullptr, n));
    }
}

// ---- final output -------------------------------------------------------------------------------------

QVariantMap AppController::prepareFinal(const QStringList& itemIds) {
    const auto ids = resolveItems(itemIds);
    QVariantList ready, missing;
    QStringList needs;
    for (const auto& id : ids) {
        const fk::ItemRecord* it = item(id);
        const bool rel = std::any_of(it->edits.begin(), it->edits.end(),
                                     [](const fk::EditVersion& e) { return !e.relinkedPath.empty(); });
        const QVariantMap m{{"id", toQ(id)}, {"label", toQ(it->label)}};
        if (rel) {
            ready << m;
            const fk::EditVersion* ev = nullptr;
            for (const auto& e : it->edits)
                if (!e.relinkedPath.empty()) ev = &e;
            if (ev && ev->relinkTransform.set) continue; // confirmed when relinked
            if (auto c = relinkChecks_.find(id); c != relinkChecks_.end())
                for (const auto& a : c->second.value("actions").toStringList())
                    if (!needs.contains(a)) needs << a;
        } else {
            missing << m;
        }
    }
    return {{"ready", ready}, {"missing", missing}, {"actions", needs}, {"folder", defaultExportFolder()}};
}

namespace {
struct FinalJobItem {
    fk::ScanRecord scan;
    fk::ItemRecord item;
    fk::EditVersion ev;
    fk::Result<fk::Sidecar> record; ///< export record (project, else the portable sidecar file)
};
QColor parseColor(const QString& s) {
    QColor c(s);
    return c.isValid() ? c : QColor(Qt::white);
}
} // namespace

void AppController::exportFinal(const QStringList& itemIds, const QVariantMap& confirmations) {
    const auto ids = resolveItems(itemIds);
    std::vector<FinalJobItem> work;
    for (const auto& id : ids) {
        const fk::ItemRecord* it = item(id);
        const fk::ScanRecord* s = scanOf(id);
        if (!it || !s) continue;
        for (auto e = it->edits.rbegin(); e != it->edits.rend(); ++e)
            if (!e->relinkedPath.empty()) {
                work.push_back({*s, *it, *e, fk::exportRecordFor(project_, *e)});
                break;
            }
    }
    if (work.empty()) {
        notify(tr("No relinked graded files to export."), QStringLiteral("warning"));
        return;
    }
    const QString folder = defaultExportFolder();
    QDir().mkpath(folder);
    const QVariantMap st = settings_;
    const bool allowScale = confirmations.value("scale").toBool();
    const bool allowDepth = confirmations.value("accept_lower_depth").toBool();
    const std::string dir = toStd(folder), projectId = project_.projectId;
    const fk::IccStore iccStore = project_.iccProfiles;
    auto outcomes = std::make_shared<std::vector<ExportOutcome>>();
    const JobSnapshot snap = snapshotFor({}, {});
    jobs_->start(
        Lane::exportJob, tr("Final export of %n print(s)", nullptr, static_cast<int>(work.size())),
        [work, st, allowScale, allowDepth, dir, projectId, outcomes, iccStore](JobContext& ctx) {
            const bool synthetic = st.value("finalMode").toString() == QLatin1String("synthetic");
            ScanGuard guard;
            for (size_t i = 0; i < work.size(); ++i) {
                if (ctx.cancel.cancelled()) return;
                const auto& w = work[i];
                ExportOutcome o;
                o.itemId = w.item.itemId;
                auto fail = [&](const QString& e) {
                    o.error = toQ(w.item.label) + QStringLiteral(": ") + e;
                    outcomes->push_back(o);
                };
                if (QString e = guard.verify(w.scan, ctx.cancel); !e.isEmpty()) {
                    fail(e);
                    continue;
                }
                auto src = fk::openTiff(fk::pathFromUtf8(w.scan.path));
                if (!src) {
                    fail(errText(src.error()));
                    continue;
                }
                const auto& sc = w.record;
                if (!sc) {
                    fail(errText(sc.error()));
                    continue;
                }
                // FK-007: the graded file is the confirmed one, or graded again with the same picture
                const auto gradedPath = fk::pathFromUtf8(w.ev.relinkedPath);
                const fk::FileStamp gradedStamp = fk::fileStamp(gradedPath);
                auto gradedSha = fk::sha256File(gradedPath, &ctx.cancel);
                fk::ImageInfo ei;
                auto edited = fk::readImageFile(gradedPath, &ei);
                if (!edited) {
                    fail(errText(edited.error()));
                    continue;
                }
                if (!gradedSha || !(fk::fileStamp(gradedPath) == gradedStamp)) {
                    fail(tr("The graded file changed while it was read."));
                    continue;
                }
                if (QString e = verifyGraded(w.ev, *sc, *edited, ei, *gradedSha, o.regradedSha256);
                    !e.isEmpty()) {
                    fail(e);
                    continue;
                }
                fk::WriteOptions wo;
                wo.format = formatOf(st.value("finalFormat").toString());
                wo.bits = st.value("finalBits").toInt();
                wo.compression = fk::Compression::lzw;
                const std::string ext = fk::fileExtension(wo.format);
                int version = 1;
                if (!synthetic) {
                    fk::CompositeRequest req;
                    req.outerRegion = w.item.outer;
                    req.frame = w.item.outer.quad;
                    req.inner = w.item.inner;
                    req.scanToEdit = sc->geometry.scanToOut;
                    req.editSize = sc->exportSize;
                    req.editExact = !sc->geometry.resampled;
                    req.edited = &*edited;
                    auto prof =
                        fk::gradedProfile(ei, w.ev.relinkTransform, *sc, (*src)->info().icc, iccStore);
                    if (!prof) {
                        fail(errText(prof.error()));
                        continue;
                    }
                    req.editedIcc = *prof;
                    QString why;
                    if (!relinkAdjust(w.ev, *sc, *edited, ei, allowScale, allowDepth, req.adjust, why)) {
                        fail(why);
                        continue;
                    }
                    const QString mm = st.value("maskMode").toString();
                    req.mask.mode = mm == QLatin1String("feather")         ? fk::MaskMode::feather
                                    : mm == QLatin1String("preserve_edge") ? fk::MaskMode::preserve_edge
                                                                           : fk::MaskMode::hard_edge;
                    req.mask.featherPx = st.value("featherPx").toDouble();
                    req.mask.preserveEdgePx = st.value("preserveEdgePx").toDouble();
                    req.strokes = w.item.composite.strokes;
                    req.outputMode = st.value("geometryMode").toString() == QLatin1String("straighten")
                                         ? fk::GeometryMode::straightened
                                         : fk::GeometryMode::pixel_crop;
                    req.upright = st.value("applyUpright").toBool() ? w.item.upright : 0;
                    req.write = wo;
                    req.privacy = privacyOf(st.value("privacy").toString());
                    req.output = fk::nextFreeVersionPath(fk::pathFromUtf8(dir),
                                                         stemOf(w.scan.path) + "_" + w.item.label + "_FINAL",
                                                         ext, &version);
                    req.ids = fk::XmpIds{projectId,     w.scan.scanId, w.item.itemId, w.ev.editVersionId,
                                         fk::newUuid(), version,       w.item.label,  "real_scan"};
                    auto rep = fk::compositeItem(**src, req, ctx.cancel, [&](double f, std::string_view) {
                        ctx.progress((static_cast<double>(i) + f) / static_cast<double>(work.size()),
                                     QString::fromStdString(w.item.label));
                    });
                    if (!rep) {
                        fail(errText(rep.error()));
                        continue;
                    }
                    if (!guard.unchanged(w.scan)) {
                        std::error_code ec;
                        std::filesystem::remove(rep->output, ec);
                        fail(changedWhileReading());
                        continue;
                    }
                    o.fullPath = fk::pathToUtf8(rep->output);
                } else {
                    // Synthetic frame (PhotoFrame style, RFC-0008): graded picture + generated border.
                    fk::FrameSpec spec;
                    const QString mode = st.value("frameMode").toString();
                    spec.mode = mode == QLatin1String("equal")          ? fk::FrameSpecMode::equal
                                : mode == QLatin1String("canvas_ratio") ? fk::FrameSpecMode::canvas_ratio
                                                                        : fk::FrameSpecMode::sides;
                    spec.unit = st.value("frameUnit").toString() == QLatin1String("px")
                                    ? fk::FrameUnit::pixels
                                    : fk::FrameUnit::percent_long_edge;
                    spec.top = st.value("frameTop").toDouble();
                    spec.right = st.value("frameRight").toDouble();
                    spec.bottom = st.value("frameBottom").toDouble();
                    spec.left = st.value("frameLeft").toDouble();
                    spec.ratioW = st.value("frameRatioW").toDouble();
                    spec.ratioH = st.value("frameRatioH").toDouble();
                    spec.minMargin = st.value("frameMinMargin").toDouble();
                    if (st.value("frameColorSource").toString() == QLatin1String("scanned")) {
                        if (auto c = fk::sampleFrameColor(**src, w.item.outer, w.item.inner)) spec.color = *c;
                    } else {
                        const QColor c = parseColor(st.value("frameColor").toString());
                        spec.color = {static_cast<uint16_t>(c.red() * 257),
                                      static_cast<uint16_t>(c.green() * 257),
                                      static_cast<uint16_t>(c.blue() * 257)};
                    }
                    auto framed = fk::renderSyntheticFrame(*edited, spec);
                    if (!framed) {
                        fail(errText(framed.error()));
                        continue;
                    }
                    const auto out = fk::nextFreeVersionPath(
                        fk::pathFromUtf8(dir), stemOf(w.scan.path) + "_" + w.item.label + "_FRAMED", ext,
                        &version);
                    wo.icc = ei.icc;
                    wo.dpiX = ei.dpiX;
                    wo.dpiY = ei.dpiY;
                    if (wo.bits == 0 || wo.format == fk::FileFormat::jpeg)
                        wo.bits = wo.format == fk::FileFormat::jpeg ? 8 : framed->bits();
                    wo.xmp =
                        fk::buildXmp(fk::XmpIds{projectId, w.scan.scanId, w.item.itemId, w.ev.editVersionId,
                                                fk::newUuid(), version, w.item.label, "synthetic"});
                    auto sink = fk::createSink(out, wo);
                    if (!sink) {
                        fail(errText(sink.error()));
                        continue;
                    }
                    fk::ImageBuffer conv = std::move(*framed);
                    if (conv.bits() != wo.bits) {
                        auto c2 =
                            fk::ImageBuffer::create(conv.width(), conv.height(), conv.channels(), wo.bits);
                        if (!c2) {
                            fail(errText(c2.error()));
                            continue;
                        }
                        for (int64_t y = 0; y < conv.height(); ++y)
                            fk::convertDepth(conv.row(y), conv.bits(), c2->row(y), wo.bits,
                                             static_cast<size_t>(conv.width() * conv.channels()));
                        conv = std::move(*c2);
                    }
                    auto s1 = (*sink)->begin(conv.size(), conv.channels(), conv.bits());
                    if (s1.ok()) s1 = (*sink)->writeRows(conv.data(), conv.height(), conv.rowBytes());
                    if (s1.ok()) s1 = (*sink)->finish();
                    if (!s1.ok()) {
                        fail(errText(s1.error()));
                        continue;
                    }
                    o.fullPath = fk::pathToUtf8(out);
                }
                o.ok = true;
                outcomes->push_back(o);
            }
        },
        [this, outcomes, folder, snap](const JobOutcome&) -> JobDone {
            int ok = 0, failed = 0;
            const bool sameProj = sameProject(snap);
            for (const auto& o : *outcomes) {
                if (!o.ok) {
                    ++failed;
                    notify(o.error, QStringLiteral("error"));
                    continue;
                }
                if (fk::ItemRecord* it = sameProj ? findItemMut(o.itemId) : nullptr) {
                    it->finalExport = o.fullPath;
                    it->composite.lastOutput = o.fullPath;
                    it->composite.lastOutputAt = fk::isoDateTimeNowUtc();
                    if (!o.regradedSha256.empty()) {
                        // graded again with the same picture, size and profile: the new bytes are the
                        // relinked file
                        for (auto e = it->edits.rbegin(); e != it->edits.rend(); ++e)
                            if (!e->relinkedPath.empty()) {
                                e->relinkedSha256 = o.regradedSha256;
                                break;
                            }
                        notify(tr("%1: the graded file was graded again since it was relinked; same picture, "
                                  "size "
                                  "and profile — used.")
                                   .arg(toQ(it->label)));
                    }
                    ++ok;
                }
            }
            if (!sameProj) return JobApply::outdated; // files kept; not recorded in a newer project
            if (ok) touch(false);
            Q_EMIT exportFinished(folder, ok, failed);
            if (failed) return {JobApply::failed, tr("%1 of %2 exports failed").arg(failed).arg(ok + failed)};
            return JobApply::applied;
        });
}

// ---- templates ------------------------------------------------------------------------------------------

QString AppController::templatesDir() const {
    const QByteArray dir = qgetenv("FK_SETTINGS_DIR");
    const QString base = dir.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                                       : QString::fromLocal8Bit(dir);
    return QDir(base).filePath(QStringLiteral("templates"));
}

QStringList AppController::templates() const {
    QStringList out;
    const QString suffix = QString::fromLatin1(fk::product::kTemplateSuffix);
    for (const QFileInfo& fi :
         QDir(templatesDir()).entryInfoList({QStringLiteral("*") + suffix}, QDir::Files, QDir::Name))
        out << fi.fileName().chopped(suffix.size());
    return out;
}

bool AppController::saveTemplate(const QString& name) {
    const fk::ScanRecord* s = findScan(curScan_);
    if (!s || s->items.empty() || name.trimmed().isEmpty()) return false;
    std::vector<fk::DetectedItem> items;
    for (const auto& it : s->items) {
        fk::DetectedItem d;
        d.outer = it.outer;
        d.inner = it.inner;
        d.filmTypeId = it.filmType;
        items.push_back(d);
    }
    const fk::Template t =
        fk::makeTemplate(items, {s->info.width, s->info.height}, s->dpiUsed, toStd(name.trimmed()));
    QDir().mkpath(templatesDir());
    const auto st = fk::saveTemplate(
        t, toPath(QDir(templatesDir())
                      .filePath(name.trimmed() + QString::fromLatin1(fk::product::kTemplateSuffix))));
    if (!st.ok()) {
        notify(tr("Saving the template failed: %1").arg(errText(st.error())), QStringLiteral("error"));
        return false;
    }
    Q_EMIT templatesChanged();
    notify(tr("Template “%1” saved.").arg(name.trimmed()));
    return true;
}

void AppController::deleteTemplate(const QString& name) {
    QFile::remove(QDir(templatesDir()).filePath(name + QString::fromLatin1(fk::product::kTemplateSuffix)));
    Q_EMIT templatesChanged();
}

// ---- misc ---------------------------------------------------------------------------------------------

void AppController::revealInFileManager(const QString& path) {
    const QFileInfo fi(path);
#if defined(Q_OS_MACOS)
    QProcess::startDetached(QStringLiteral("open"), {QStringLiteral("-R"), fi.absoluteFilePath()});
#elif defined(Q_OS_WIN)
    QProcess::startDetached(QStringLiteral("explorer.exe"),
                            {QStringLiteral("/select,"), QDir::toNativeSeparators(fi.absoluteFilePath())});
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath()));
#endif
}

} // namespace fkapp
