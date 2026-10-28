// App helpers: string/path/shape conversions between Qt and the core (CONTRACT-ui-core §1).
#pragma once

#include "fk/geometry.hpp"
#include "fk/util.hpp"

#include <QPointF>
#include <QString>
#include <QUrl>
#include <QVariant>

#include <filesystem>
#include <string>

namespace fkapp {

inline std::string toStd(const QString& s) {
    return s.toStdString();
}
inline QString toQ(const std::string& s) {
    return QString::fromStdString(s);
}
inline std::filesystem::path toPath(const QString& s) {
    return fk::pathFromUtf8(s.toStdString());
}
inline QString fromPath(const std::filesystem::path& p) {
    return QString::fromStdString(fk::pathToUtf8(p));
}
/// Accepts file URLs and plain paths.
inline QString localPath(const QVariant& v) {
    const QString s = v.toString();
    const QUrl u(s);
    if (u.isLocalFile()) return u.toLocalFile();
    return s;
}
inline QPointF toQ(fk::Vec2 v) {
    return {v.x, v.y};
}
inline fk::Vec2 toFk(QPointF p) {
    return {p.x(), p.y()};
}

/// {kind: "quad", points: [[x,y]…]} or {kind: "ellipse", cx, cy, rx, ry, angle}.
QVariantMap shapeToVariant(const fk::Shape& s);
bool shapeFromVariant(const QVariantMap& m, fk::Shape& out);

} // namespace fkapp
