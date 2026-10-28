#include "ImageProvider.hpp"

#include "AppController.hpp"

#include <QMetaObject>
#include <QThread>

namespace fkapp {

QImage ThumbProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize) {
    // Image providers may be called from the scene-graph loader thread: read the thumbnail on the UI thread.
    QImage img;
    if (app_->thread() == QThread::currentThread())
        img = app_->thumbnail(id);
    else
        QMetaObject::invokeMethod(app_, [&] { img = app_->thumbnail(id); }, Qt::BlockingQueuedConnection);
    if (img.isNull()) {
        img = QImage(4, 4, QImage::Format_RGB32);
        img.fill(QColor(0x1B, 0x1C, 0x1F));
    }
    if (requestedSize.isValid() && requestedSize.width() > 0 && requestedSize.height() > 0)
        img = img.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (size) *size = img.size();
    return img;
}

} // namespace fkapp
