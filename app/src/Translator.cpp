#include "Translator.hpp"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace fkapp {

bool JsonTranslator::loadJson(const QString& resourcePath) {
    QFile f(resourcePath);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    table_.clear();
    for (auto c = root.begin(); c != root.end(); ++c) {
        const QJsonObject ctx = c.value().toObject();
        for (auto s = ctx.begin(); s != ctx.end(); ++s) {
            const QString t = s.value().toString();
            if (t.isEmpty()) continue;
            table_.insert(c.key() + QChar(0x1f) + s.key(), t);
            table_.insert(QChar(0x1f) + s.key(), t); // context-free fallback
        }
    }
    return !table_.isEmpty();
}

QString JsonTranslator::translate(const char* context, const char* sourceText, const char*, int n) const {
    if (!sourceText) return {};
    const QString src = QString::fromUtf8(sourceText);
    auto it = table_.constFind(QString::fromUtf8(context ? context : "") + QChar(0x1f) + src);
    if (it == table_.constEnd()) it = table_.constFind(QChar(0x1f) + src);
    if (it == table_.constEnd()) return {};
    QString out = *it;
    if (n >= 0) out.replace(QStringLiteral("%n"), QString::number(n));
    return out;
}

} // namespace fkapp
