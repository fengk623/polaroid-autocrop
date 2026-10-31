// `FrameKeeper --self-test <dir> --scan <tif>` (RFC-0011): drives the real UI controllers offscreen
// through import → detect → export → relink → final (real frame and synthetic), captures window snapshots
// and writes report.json. It cannot prove monitor colour or drawing in a real window (HV-02/HV-03).
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointF>
#include <QString>

class QQuickItem;
class QQuickWindow;

namespace fkapp {

class AppController;

class SelfTest : public QObject {
    Q_OBJECT
  public:
    SelfTest(AppController* app, QQuickWindow* window, QString outDir, QString scan,
             QObject* parent = nullptr);
    /// Runs synchronously (spinning the event loop); returns the process exit code.
    int run();
    /// `--scenario <name>`: reproduces reported user flows and records what actually happens (observations,
    /// not assertions) in <dir>/scenario-<name>.json plus snapshots. Names: "relink", "count", "upright".
    int runScenario(const QString& name);

  private:
    void check(const QString& name, bool ok, const QString& detail = {});
    /// A check whose precondition this scan does not meet (e.g. an untagged scan for a colour conversion):
    /// recorded as skipped with the reason, never as passed.
    void skip(const QString& name, const QString& why);
    void snapshot(const QString& name);
    QObject* findCanvas() const;
    QQuickItem* findItem(const QString& objectName) const;
    /// A left click through the window (a real input event), at `local` or the item's centre; false when the
    /// item is hidden or outside the window.
    bool click(QQuickItem* item, QPointF local = QPointF(-1, -1));
    void observe(const QString& name, const QJsonObject& data);
    /// Known issue (expected to reproduce until fixed, like ctest WILL_FAIL): reproduced → recorded, run
    /// passes; not reproduced → the run fails so the check is turned into a normal one with the fix.
    void known(const QString& ticket, const QString& name, bool reproduced, const QString& detail = {});
    /// A known-issue check whose precondition this scan does not meet (e.g. an 8-bit scan for a 16-bit
    /// issue): recorded as not applicable, neither reproduced nor a failure.
    void knownNotApplicable(const QString& ticket, const QString& name, const QString& why);
    void runKnownIssues(const QString& exportDir);
    /// FK-001 / FK-008 (U008): the replace guard, failed saves, backup restore, recovery snapshots, output
    /// settings.
    void runDataProtection();
    /// FK-004 (U009): privacy presets on a scan copy carrying identity, outputs read back byte for byte.
    void runPrivacy();
    /// UX-01 (U011): automatic count by default, per-scan override with the left-rail controls (mouse
    /// input), warnings updated without detecting again, single/multi/blank scans in one project, saved.
    void runCountSetting();
    void runTaskSafety(const QString& exportDir, const QString& projectPath);
    int scenarioMinIsWhite();
    QJsonArray known_;
    int scenarioRelink();
    int scenarioCount();
    int scenarioUpright();
    QJsonArray observations_;

    AppController* app_;
    QQuickWindow* window_;
    QString dir_, scan_;
    QJsonArray checks_;
    bool allOk_ = true;
};

} // namespace fkapp
