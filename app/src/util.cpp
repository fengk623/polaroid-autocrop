#include "util.hpp"

namespace fkapp {

QVariantMap shapeToVariant(const fk::Shape& s) {
    QVariantMap m;
    if (s.kind == fk::ShapeKind::quad) {
        m["kind"] = QStringLiteral("quad");
        QVariantList pts;
        for (const auto& p : s.quad.p)
            pts << QVariant::fromValue(QVariantList{p.x, p.y});
        m["points"] = pts;
    } else {
        m["kind"] = QStringLiteral("ellipse");
        m["cx"] = s.ellipse.c.x;
        m["cy"] = s.ellipse.c.y;
        m["rx"] = s.ellipse.rx;
        m["ry"] = s.ellipse.ry;
        m["angle"] = s.ellipse.angleRad;
    }
    return m;
}

bool shapeFromVariant(const QVariantMap& m, fk::Shape& out) {
    if (m.value("kind").toString() == QLatin1String("ellipse")) {
        out = fk::Shape::fromEllipse({{m.value("cx").toDouble(), m.value("cy").toDouble()},
                                      m.value("rx").toDouble(),
                                      m.value("ry").toDouble(),
                                      m.value("angle").toDouble()});
        return out.ellipse.rx > 0 && out.ellipse.ry > 0;
    }
    const QVariantList pts = m.value("points").toList();
    if (pts.size() != 4) return false;
    std::array<fk::Vec2, 4> q;
    for (int i = 0; i < 4; ++i) {
        const QVariantList p = pts[i].toList();
        if (p.size() != 2) return false;
        q[i] = {p[0].toDouble(), p[1].toDouble()};
    }
    out = fk::Shape::fromQuad(fk::normalizeQuad(q));
    return fk::isConvex(out.quad);
}

} // namespace fkapp
