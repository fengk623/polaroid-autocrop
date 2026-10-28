// Translations from JSON tables (app/i18n/<lang>.json: {"context": {"source": "translation"}}).
// No Qt Linguist toolchain needed at build time; tools/check-i18n.py fails CI on missing strings.
#pragma once

#include <QHash>
#include <QTranslator>

namespace fkapp {

class JsonTranslator : public QTranslator {
  public:
    bool loadJson(const QString& resourcePath);
    QString translate(const char* context, const char* sourceText, const char* disambiguation = nullptr,
                      int n = -1) const override;
    bool isEmpty() const override { return table_.isEmpty(); }

  private:
    QHash<QString, QString> table_; ///< "context\x1fsource" → translation; "\x1fsource" for any context
};

} // namespace fkapp
