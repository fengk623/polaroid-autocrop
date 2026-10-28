// `FrameKeeper --self-test <dir> --scan <tif>` (RFC-0011): drives the real UI controllers offscreen
// through import → detect → export → relink → final (real frame and synthetic), captures window snapshots
// and writes report.json. It cannot prove monitor colour or drawing in a real window (HV-02/HV-03).
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

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

  private:
    void check(const QString& name, bool ok, const QString& detail = {});
    void snapshot(const QString& name);
    QObject* findCanvas() const;

    AppController* app_;
    QQuickWindow* window_;
    QString dir_, scan_;
    QJsonArray checks_;
    bool allOk_ = true;
};

} // namespace fkapp
