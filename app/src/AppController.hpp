// AppController (RFC-0011, CONTRACT-ui-core §2): owns the fk::Project (source of truth), publishes read-only
// models, and turns every user action into a validated core command. QML never computes geometry.
#pragma once

#include "Display.hpp"
#include "Jobs.hpp"
#include "Models.hpp"

#include "fk/composite.hpp"
#include "fk/project.hpp"

#include <QImage>
#include <QObject>
#include <QRectF>
#include <QSettings>
#include <QTimer>
#include <QVariant>

#include <map>
#include <memory>
#include <mutex>
#include <set>

namespace fkapp {

class AppController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString projectName READ projectName NOTIFY projectChanged)
    Q_PROPERTY(QString projectPath READ projectPath NOTIFY projectChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY projectChanged)
    Q_PROPERTY(bool readOnly READ readOnly NOTIFY projectChanged)
    Q_PROPERTY(QObject* scans READ scansObj CONSTANT)
    Q_PROPERTY(QObject* items READ itemsObj CONSTANT)
    Q_PROPERTY(QObject* jobs READ jobsObj CONSTANT)
    Q_PROPERTY(QString currentScanId READ currentScanId NOTIFY selectionChanged)
    Q_PROPERTY(QString currentItemId READ currentItemId NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap currentScan READ currentScan NOTIFY currentChanged)
    Q_PROPERTY(QVariantMap currentItem READ currentItem NOTIFY currentChanged)
    Q_PROPERTY(QStringList selectedItemIds READ selectedItemIds NOTIFY selectionChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY settingsChanged)
    Q_PROPERTY(QStringList templates READ templates NOTIFY templatesChanged)
    Q_PROPERTY(QVariantMap counts READ counts NOTIFY projectChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY currentChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY currentChanged)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString buildCommit READ buildCommit CONSTANT)
    Q_PROPERTY(QString buildId READ buildId CONSTANT)
    Q_PROPERTY(QString platform READ platform CONSTANT)

  public:
    explicit AppController(QObject* parent = nullptr);
    ~AppController() override;

    // ---- properties ----
    QString projectName() const;
    QString projectPath() const { return projectPath_; }
    bool dirty() const { return dirty_; }
    bool readOnly() const { return readOnly_; }
    QObject* scansObj() { return scans_; }
    QObject* itemsObj() { return items_; }
    QObject* jobsObj() { return jobs_->model(); }
    QString currentScanId() const { return QString::fromStdString(curScan_); }
    QString currentItemId() const { return QString::fromStdString(curItem_); }
    QVariantMap currentScan() const;
    QVariantMap currentItem() const;
    QStringList selectedItemIds() const;
    bool busy() const { return jobs_->busy(); }
    QVariantMap settings() const;
    QStringList templates() const;
    QVariantMap counts() const;
    bool canUndo() const;
    bool canRedo() const;
    QString version() const;
    QString buildCommit() const;
    QString buildId() const;
    QString platform() const;

    // ---- project ----
    Q_INVOKABLE void newProject();
    Q_INVOKABLE bool openProject(const QVariant& url);
    Q_INVOKABLE bool saveProject();
    Q_INVOKABLE bool saveProjectAs(const QVariant& url);
    // ---- data protection (FK-001, FK-008, RFC-0017) ----
    /// What replacing the open project would lose: {unsaved (dirty with content), jobs (running/queued
    /// count)}.
    Q_INVOKABLE QVariantMap replaceInfo() const;
    /// FK-008: the damaged project at `url` is moved aside (kept) and its backup opened at the same path,
    /// unsaved.
    Q_INVOKABLE bool restoreBackup(const QVariant& url);
    /// Recovery snapshots of unsaved work (crash or forced quit): {file, name, projectPath, savedAt}.
    Q_INVOKABLE QVariantList recoverySnapshots() const;
    Q_INVOKABLE bool recoverSnapshot(const QString& file);
    Q_INVOKABLE void discardSnapshot(const QString& file);
    /// The user chose "Don't save": the snapshot of the current project is no longer wanted.
    Q_INVOKABLE void discardCurrentRecovery();
    /// Writes the recovery snapshot now (normally a few seconds after the last change).
    Q_INVOKABLE void writeRecovery();
    Q_INVOKABLE void importScans(const QVariantList& urls);
    Q_INVOKABLE void importFolder(const QVariant& url);
    Q_INVOKABLE void removeScan(const QString& scanId);
    Q_INVOKABLE void detectScan(const QString& scanId, const QString& templateName = {});
    Q_INVOKABLE void detectSelectedScans(const QString& templateName = {});
    /// UX-01: the scan's count — automatic (no count warning) or the expected number of prints (1–12, kept
    /// while automatic). Updates the count warning at once; does not detect again.
    Q_INVOKABLE void setScanCount(const QString& scanId, bool autoCount, int expectedCount);
    Q_INVOKABLE void relocateMissing(const QVariant& folderUrl);

    // ---- selection ----
    Q_INVOKABLE void selectScan(const QString& scanId);
    Q_INVOKABLE void selectItem(const QString& itemId, bool extend = false, bool toggle = false);
    Q_INVOKABLE void selectAllItems();
    Q_INVOKABLE void stepItem(int delta);
    Q_INVOKABLE void stepScan(int delta);

    // ---- item edits (one undo step each) ----
    Q_INVOKABLE void setOuter(const QString& itemId, const QVariantMap& shape);
    Q_INVOKABLE void setInner(const QString& itemId, const QVariantMap& shape);
    Q_INVOKABLE void setOuterNumeric(const QString& itemId, int corner, double x, double y);
    Q_INVOKABLE void setInnerKind(const QString& itemId, const QString& kind);
    Q_INVOKABLE void resetToDetected(const QString& itemId);
    Q_INVOKABLE void rotateUpright(const QString& itemId, int delta);
    /// UX-04: turns the whole scan's view by `delta` quarter turns (clockwise positive). Every print follows;
    /// no boundary, file or export record changes; one undo step for the whole scan.
    Q_INVOKABLE void rotateScan(const QString& scanId, int delta);
    /// UX-08: the print's mask. A mode comes with its natural-blend preset (mm when the DPI is reliable); the
    /// width keeps the choice's unit. Undoable, saved per print; the preview and the final output use it.
    Q_INVOKABLE void setItemMaskMode(const QString& itemId, const QString& mode);
    Q_INVOKABLE void setItemMaskWidth(const QString& itemId, double width);
    Q_INVOKABLE void resetItemMask(const QString& itemId);
    /// Explicit batch: the listed prints take `fromItemId`'s mask. Returns how many changed.
    Q_INVOKABLE int applyMaskToItems(const QString& fromItemId, const QStringList& itemIds);
    /// UX-09: a drag on a value (scrubbing). begin remembers the mask; live changes update the print (and its
    /// coalesced preview) without undo steps; end commits one undo step for the whole drag, or restores
    /// (Esc).
    Q_INVOKABLE void beginMaskEdit(const QString& itemId);
    Q_INVOKABLE void setItemMaskWidthLive(const QString& itemId, double width);
    Q_INVOKABLE void endMaskEdit(const QString& itemId, bool commit);
    Q_INVOKABLE void setReviewed(const QString& itemId, bool reviewed);
    Q_INVOKABLE void setExcluded(const QString& itemId, bool excluded);
    Q_INVOKABLE void reviewAndNext();
    Q_INVOKABLE void toggleExcludedSelection();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void snapOuter(const QString& itemId);

    // ---- settings ----
    Q_INVOKABLE void setSetting(const QString& key, const QVariant& value);
    Q_INVOKABLE void setExportFolder(const QVariant& url);
    Q_INVOKABLE void cancelJob(int id);

    // ---- export / relink / final ----
    /// Items an export would include, with the unreviewed low-confidence ones listed for confirmation.
    Q_INVOKABLE QVariantMap prepareExport(const QStringList& itemIds);
    /// `allowDepthReduction`: the user's explicit consent to write 8 bits from a 16-bit scan (FK-010).
    Q_INVOKABLE void exportImageAreas(const QStringList& itemIds, bool allowDepthReduction = false);
    Q_INVOKABLE void exportFullPrints(const QStringList& itemIds, bool allowDepthReduction = false);
    /// Assesses graded files by weighing all evidence (RFC-0006 §Relink v2). With `targetItemId` only that
    /// print is considered and, if none of its exports matches, a new alignment is offered. `update`
    /// re-assesses rows already shown (result: relinkRowUpdated instead of relinkResults).
    Q_INVOKABLE void relinkFiles(const QVariantList& urls, const QString& targetItemId = QString(),
                                 bool update = false);
    Q_INVOKABLE void applyRelink(const QVariantList& rows);
    /// Every print of the project, for choosing a relink target: {id, label, scanName}.
    Q_INVOKABLE QVariantList relinkTargets() const;
    /// Window drop: projects open, scans import, graded files (our metadata, an export name, or not a TIFF)
    /// relink.
    Q_INVOKABLE void dropFiles(const QVariantList& urls);
    /// Reads an ICC profile chosen to interpret a graded file (FK-003): {ok, id "file", label, sha256} or
    /// {ok false, error}. Unreadable profiles are refused.
    Q_INVOKABLE QVariantMap loadIccProfile(const QVariant& url);
    /// FK-004: the descriptive fields an export of the current scan would carry with the current preset and
    /// format
    /// ({name, value, identity}); empty when the scan's metadata is not known yet.
    Q_INVOKABLE QVariantList privacyPreview() const;
    Q_INVOKABLE QVariantMap prepareFinal(const QStringList& itemIds);
    Q_INVOKABLE void exportFinal(const QStringList& itemIds, const QVariantMap& confirmations);
    Q_INVOKABLE void requestCompositePreview(const QString& itemId);

    // ---- templates ----
    Q_INVOKABLE bool saveTemplate(const QString& name);
    Q_INVOKABLE void deleteTemplate(const QString& name);

    // ---- misc ----
    Q_INVOKABLE void revealInFileManager(const QString& path);
    Q_INVOKABLE QString defaultExportFolder() const;
    Q_INVOKABLE QString fileUrl(const QString& path) const;
    Q_INVOKABLE void notify(const QString& text, const QString& kind = QStringLiteral("info"));

    // ---- canvas support (C++ only) ----
    struct CanvasScan {
        QImage display;     ///< proxy in display space
        int proxyScale = 1; ///< scan px per proxy px
        int64_t width = 0, height = 0;
        int rotation = 0; ///< UX-04: view quarter turns clockwise
        bool ready = false;
        QString state, error;
        std::vector<const fk::ItemRecord*> items;
    };
    CanvasScan canvasScan() const;
    /// UX-08 (self-test): {preview, final} masks last used for the print (mode -1 = not made yet).
    std::pair<fk::MaskParams, fk::MaskParams> masksUsed(const std::string& itemId) const;
    // test access (self-test): generations and the job runner
    quint64 itemGeneration(const std::string& itemId) const;
    /// Item generation the cached composite preview was computed for (0 = no preview).
    quint64 compositePreviewGeneration(const std::string& itemId) const;
    JobRunner* jobRunner() const { return jobs_; }
    int exportRecordCount() const { return static_cast<int>(project_.exportRecords.size()); }
    const fk::ItemRecord* item(const std::string& itemId) const;
    const fk::ScanRecord* scanOf(const std::string& itemId) const;
    /// Live (uncommitted) geometry while dragging; committed with setOuter/setInner on release.
    void commitShape(const std::string& itemId, bool outer, const fk::Shape& s);
    /// Full-resolution view: renders scan rect `r` (scan px) at `outW`×`outH` on the io lane.
    using DetailFn = std::function<void(QImage, QRectF, quint64)>;
    void requestDetail(QRectF r, int outW, int outH, quint64 generation, DetailFn done);
    QImage compositePreview(const std::string& itemId) const;
    QImage thumbnail(const QString& id) const;
    JobRunner* jobRunner() { return jobs_; }
    bool waitIdle(int timeoutMs);
    fk::Project& project() { return project_; }

  Q_SIGNALS:
    void projectChanged();
    void selectionChanged();
    void currentChanged();
    void busyChanged();
    void settingsChanged();
    void templatesChanged();
    void scanImageChanged(const QString& scanId);
    void geometryChanged(const QString& itemId);
    void compositePreviewReady(const QString& itemId);
    void notification(const QString& text, const QString& kind);
    void relinkResults(const QVariantList& rows);
    /// FK-008: opening failed because the file is damaged; a backup from the last save can be restored.
    void backupAvailable(const QString& path, const QString& backupName, const QString& backupTime);
    /// A project file was dropped: QML asks about unsaved changes before opening it (FK-001).
    void openRequested(const QString& url);
    void relinkRowUpdated(const QVariantMap& row);
    void exportFinished(const QString& folder, int written, int failed);
    void confirmationNeeded(const QString& title, const QString& text);

  private:
    struct ScanRuntime {
        QImage display;
        int proxyScale = 1;
        QImage thumb;
        std::map<std::string, QImage> itemThumbs;
        QString state = QStringLiteral("pending"); // pending, detecting, ready, error, missing, changed
        QString error;
        fk::MetadataSet meta; ///< descriptive metadata of the scan (privacy panel, FK-004)
        bool metaKnown = false;
        quint64 rev = 0;
        std::shared_ptr<std::mutex> sourceMutex = std::make_shared<std::mutex>();
        std::shared_ptr<fk::TiffSource> source; ///< for full-resolution view tiles
        DisplayConverter display2;
        bool converterReady = false;
    };
    struct Snapshot {
        fk::Shape outer, inner;
        fk::BoundarySource outerSource, innerSource;
        int upright = 0;
        bool reviewed = false;
        quint64 seq = 0; ///< order across the print's and the scan's steps (one undo history)
        fk::MaskChoice mask{};
        bool maskSet = false;
    };
    struct UndoState {
        std::vector<Snapshot> undo, redo;
    };
    /// UX-04: whole-scan steps (the view rotation), ordered with the prints' steps by `seq`.
    struct ScanStep {
        int rotation = 0;
        quint64 seq = 0;
    };
    struct ScanUndo {
        std::vector<ScanStep> undo, redo;
    };

    fk::ScanRecord* findScan(const std::string& id);
    const fk::ScanRecord* findScan(const std::string& id) const;
    fk::ItemRecord* findItemMut(const std::string& id, fk::ScanRecord** scan = nullptr);
    void touch(bool structural);
    void pushUndo(fk::ItemRecord& it);
    Snapshot snap(const fk::ItemRecord& it) const;
    void restore(fk::ItemRecord& it, const Snapshot& s);
    void rebuildModels();
    void refreshItemThumbs(const std::string& scanId);
    void startDetect(const std::string& scanId, std::shared_ptr<fk::Template> tmpl);
    void loadPreview(const std::string& scanId);
    fk::ExportRequest baseExportRequest(const fk::ItemRecord& it, bool fullPrint,
                                        bool allowDepthReduction = false) const;
    /// FK-010: the core's plan of the real output of `it` with the current settings, worded for the UI.
    QVariantMap outputPlan(const fk::ItemRecord& it, bool fullPrint) const;
    QString statusOf(const fk::ItemRecord& it) const;
    void loadSettings();
    QString templatesDir() const;
    std::vector<std::string> resolveItems(const QStringList& ids) const;

    fk::Project project_;
    QString projectPath_;
    bool dirty_ = false, readOnly_ = false;
    std::string curScan_, curItem_;
    std::vector<std::string> selection_;
    std::map<std::string, ScanRuntime> rt_;
    std::map<std::string, UndoState> undo_;
    std::map<std::string, ScanUndo> scanUndo_;
    std::map<std::string, double> edgeChange_; ///< UX-08: last composite preview's edge change (ΔE00)
    /// UX-08 (self-test): the mask the last composite preview and the last final output of a print were made
    /// with
    std::map<std::string, std::pair<fk::MaskParams, fk::MaskParams>> masksUsed_;
    void setMask(const std::string& itemId, const fk::MaskChoice& m, bool undoStep = true);
    std::map<std::string, std::pair<fk::MaskChoice, bool>> maskEditStart_; ///< UX-09: mask before a drag
    quint64 undoSeq_ = 0;
    void afterScanRotation(const std::string& scanId);
    std::map<std::string, std::pair<fk::Shape, fk::Shape>> detected_; ///< itemId → detected outer/inner
    std::map<std::string, QImage> compositePreviews_;
    // ---- task safety (RFC-0010 §Task safety, FK-006) ----
    /// Generations: a background result is applied only if what it was computed from is still current.
    struct JobSnapshot {
        quint64 project = 0, scan = 0, item = 0, composite = 0;
        std::string scanId, itemId;
    };
    JobSnapshot snapshotFor(const std::string& scanId, const std::string& itemId) const;
    bool sameProject(const JobSnapshot& s) const { return s.project == projectGen_; }
    bool sameScan(const JobSnapshot& s) const;
    bool sameItem(const JobSnapshot& s) const;
    bool sameComposite(const JobSnapshot& s) const { return sameItem(s) && s.composite == compositeGen_; }
    /// An item's geometry, orientation or relinked file changed: newer generation, cached preview dropped.
    void invalidateItem(const std::string& itemId);
    quint64 projectGen_ = 1, compositeGen_ = 1;
    std::map<std::string, quint64> scanGen_, itemGen_;
    std::map<std::string, quint64> previewGen_; ///< item → item generation its cached composite preview shows
    std::map<std::string, int> previewJob_;     ///< item → in-flight composite preview job (coalescing)
    std::set<std::string> previewPending_;      ///< UX-09: asked again while one was in flight
    std::map<std::string, QVariantMap> relinkChecks_; ///< itemId → last content/geometry check
    /// New-alignment records offered by the last assessments, applied only when the user confirms them.
    std::map<QString, fk::Sidecar> pendingAlignments_;
    /// Profiles offered for interpreting graded files (FK-003), by SHA-256; the chosen one moves into the
    /// project.
    std::map<std::string, std::vector<uint8_t>> knownIcc_;
    QTimer* recoveryTimer_ = nullptr;
    QString recoveryDir() const;
    QString recoveryFileFor(const std::string& projectId) const;
    /// Takes over a loaded project (open, restore, recover): runtime state, settings, previews.
    void adoptProject(fk::Project p, const QString& path, bool readOnly, bool dirty);
    void syncOutputSettings();
    JobRunner* jobs_;
    ScanListModel* scans_;
    ItemListModel* items_;
    QSettings* qs_;
    QVariantMap settings_;
};

} // namespace fkapp
