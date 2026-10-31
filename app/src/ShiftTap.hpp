// UX-03: a lone tap of Shift (pressed and released with nothing else in between) switches the edited boundary
// between Outer and Inner. Holding Shift for a constrained drag, Shift+click, Shift+arrows, Shift+wheel and
// every Shift shortcut never count as a tap. Watches the window's own input events; the QML side decides
// whether the tap may act (not while typing, not in a dialog or menu).
#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QtQml/qqmlregistration.h>

class QWindow;

namespace fkapp {

class ShiftTap : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QObject* window READ window WRITE setWindow NOTIFY windowChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    /// A press held longer than this is not a tap (ms).
    Q_PROPERTY(int maxHoldMs READ maxHoldMs WRITE setMaxHoldMs NOTIFY enabledChanged)

  public:
    explicit ShiftTap(QObject* parent = nullptr);
    QObject* window() const;
    void setWindow(QObject* w);
    bool enabled() const { return enabled_; }
    void setEnabled(bool v);
    int maxHoldMs() const { return maxHoldMs_; }
    void setMaxHoldMs(int ms);

  Q_SIGNALS:
    void windowChanged();
    void enabledChanged();
    void tapped();

  protected:
    bool eventFilter(QObject* watched, QEvent* e) override;

  private:
    QPointer<QWindow> window_;
    bool enabled_ = true;
    bool armed_ = false;
    int maxHoldMs_ = 1000;
    QElapsedTimer held_;
};

} // namespace fkapp
