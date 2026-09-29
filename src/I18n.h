#pragma once

#include <QHash>
#include <QObject>
#include <QTranslator>
#include <QVariant>

class QQmlEngine;

// Übersetzer über JSON-Wörterbücher (i18n/<sprache>.json: {"Quelltext": "Übersetzung"}).
// Unabhängig vom Kontext – derselbe deutsche Text wird überall gleich übersetzt.
// Fehlt ein Eintrag, gilt Englisch als Rückfall, danach der deutsche Quelltext.
class JsonTranslator : public QTranslator
{
public:
    bool load(const QString &language);
    QString translate(const char *context, const char *sourceText, const char *disambiguation = nullptr, int n = -1) const override;
    bool isEmpty() const override { return m_map.isEmpty(); }

private:
    static QHash<QString, QString> read(const QString &language);
    QHash<QString, QString> m_map;
    QHash<QString, QString> m_fallback;
};

// Sprache der Oberfläche (QML: "I18n")
class I18n : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList languages READ languages CONSTANT)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(QString effective READ effective NOTIFY languageChanged)

public:
    explicit I18n(QObject *parent = nullptr);

    // "" = Systemsprache
    QString language() const { return m_language; }
    void setLanguage(const QString &code);
    QString effective() const { return m_effective; }
    QVariantList languages() const;
    void setEngine(QQmlEngine *engine) { m_engine = engine; }
    static QStringList codes();

signals:
    void languageChanged();

private:
    void apply();

    QString m_language;
    QString m_effective;
    JsonTranslator m_translator;
    bool m_installed = false;
    QQmlEngine *m_engine = nullptr;
};
