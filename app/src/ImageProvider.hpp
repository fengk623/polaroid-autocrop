// image://fk/scan/<id>?rev and image://fk/item/<id>?rev — thumbnails held by AppController (memory only).
#pragma once

#include <QQuickImageProvider>

namespace fkapp {

class AppController;

class ThumbProvider : public QQuickImageProvider {
  public:
    explicit ThumbProvider(AppController* app) : QQuickImageProvider(QQuickImageProvider::Image), app_(app) {}
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

  private:
    AppController* app_;
};

} // namespace fkapp
