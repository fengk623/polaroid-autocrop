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
#include <QVariant>

#include <map>
#include <memory>
#include <mutex>

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
    QString platform() const;

    // ---- project ----
    Q_INVOKABLE void newProject();
    Q_INVOKABLE bool openProject(const QVariant& url);
    Q_INVOKABLE bool saveProject();
    Q_INVOKABLE bool saveProjectAs(const QVariant& url);
    Q_INVOKABLE void importScans(const QVariantList& urls);
    Q_INVOKABLE void importFolder(const QVariant& url);
    Q_INVOKABLE void removeScan(const QString& scanId);
    Q_INVOKABLE void detectScan(const QString& scanId, const QString& templateName = {});
    Q_INVOKABLE void detectSelectedScans(const QString& templateName = {});
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
    Q_INVOKABLE void exportImageAreas(const QStringList& itemIds);
    Q_INVOKABLE void exportFullPrints(const QStringList& itemIds);
    Q_INVOKABLE void relinkFiles(const QVariantList& urls);
    Q_INVOKABLE void applyRelink(const QVariantList& rows);
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
        bool ready = false;
        QString state, error;
        std::vector<const fk::ItemRecord*> items;
    };
    CanvasScan canvasScan() const;
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
    void exportFinished(const QString& folder, int written, int failed);
    void confirmationNeeded(const QString& title, const QString& text);

  private:
    struct ScanRuntime {
        QImage display;
        int proxyScale = 1;
        QImage thumb;
        std::map<std::string, QImage> itemThumbs;
        QString state = QStringLiteral("pending"); // pending, detecting, ready, error, missing
        QString error;
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
    };
    struct UndoState {
        std::vector<Snapshot> undo, redo;
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
    fk::ExportRequest baseExportRequest(const fk::ItemRecord& it, bool fullPrint) const;
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
    std::map<std::string, std::pair<fk::Shape, fk::Shape>> detected_; ///< itemId → detected outer/inner
    std::map<std::string, QImage> compositePreviews_;
    std::map<std::string, QVariantMap> relinkChecks_; ///< itemId → last content/geometry check
    JobRunner* jobs_;
    ScanListModel* scans_;
    ItemListModel* items_;
    QSettings* qs_;
    QVariantMap settings_;
};

} // namespace fkapp
