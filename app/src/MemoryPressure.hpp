// Memory pressure from the operating system (FK-005, PRD F14): macOS dispatch source (warn / critical),
// Windows low-memory resource notification (polled every 2 s), Linux MemAvailable below 5 % of RAM (at
// least 256 MB, polled every 2 s). Emits `pressure()` on the UI thread when the system turns low, and again
// every 30 s while it stays low (Windows, Linux; macOS reports each change of level). The handler only drops
// caches that can be rebuilt; output quality never changes.
#pragma once

#include <QObject>
#include <QTimer>

namespace fkapp {

class MemoryPressure : public QObject {
    Q_OBJECT
  public:
    explicit MemoryPressure(QObject* parent = nullptr);
    ~MemoryPressure() override;

  Q_SIGNALS:
    void pressure();

  private:
    void poll();
    void onLow(bool low);
    QTimer timer_;
    bool low_ = false;
    qint64 lastEmit_ = 0;
    void* native_ = nullptr; ///< macOS dispatch source / Windows notification handle
};

} // namespace fkapp
