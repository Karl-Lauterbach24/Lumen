#include "I18n.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QQmlEngine>
#include <QSettings>

namespace {

struct Language
{
    const char *code;
    const char *name; // Eigenname der Sprache
};

const Language kLanguages[] = {
    {"de", "Deutsch"},  {"en", "English"},   {"fr", "Français"}, {"es", "Español"},
    {"it", "Italiano"}, {"pt", "Português"}, {"nl", "Nederlands"}, {"pl", "Polski"},
    {"sv", "Svenska"},  {"cs", "Čeština"},   {"tr", "Türkçe"},     {"uk", "Українська"},
    {"ru", "Русский"},  {"ja", "日本語"},     {"zh", "简体中文"},    {"ko", "한국어"},
};

} // namespace

QHash<QString, QString> JsonTranslator::read(const QString &language)
{
    QHash<QString, QString> map;
    QFile f(QStringLiteral(":/qt/qml/Lumen/i18n/%1.json").arg(language));
    if (!f.open(QIODevice::ReadOnly))
        return map;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = o.begin(); it != o.end(); ++it) {
        const QString v = it.value().toString();
        if (!v.isEmpty())
            map.insert(it.key(), v);
    }
    return map;
}

bool JsonTranslator::load(const QString &language)
{
    m_map = language == QLatin1String("de") ? QHash<QString, QString>() : read(language);
    m_fallback = (language == QLatin1String("de") || language == QLatin1String("en")) ? QHash<QString, QString>() : read(QStringLiteral("en"));
    return true;
}

QString JsonTranslator::translate(const char *, const char *sourceText, const char *, int) const
{
    if (!sourceText)
        return {};
    const QString key = QString::fromUtf8(sourceText);
    const auto it = m_map.constFind(key);
    if (it != m_map.constEnd())
        return *it;
    const auto fb = m_fallback.constFind(key);
    return fb != m_fallback.constEnd() ? *fb : QString();
}

I18n::I18n(QObject *parent)
    : QObject(parent)
{
    // Standard: Englisch; leer = ausdrücklich Systemsprache
    m_language = QSettings().value(QStringLiteral("ui/language"), QStringLiteral("en")).toString();
    apply();
}

QStringList I18n::codes()
{
    QStringList c;
    for (const Language &l : kLanguages)
        c << QLatin1String(l.code);
    return c;
}

QVariantList I18n::languages() const
{
    QVariantList out;
    for (const Language &l : kLanguages)
        out.append(QVariantMap{{"code", QLatin1String(l.code)}, {"name", QString::fromUtf8(l.name)}});
    return out;
}

void I18n::setLanguage(const QString &code)
{
    if (code == m_language)
        return;
    m_language = code;
    QSettings().setValue(QStringLiteral("ui/language"), code);
    apply();
}

void I18n::apply()
{
    QString code = m_language;
    if (code.isEmpty() || !codes().contains(code)) {
        // Systemsprache, sonst Englisch
        const QString sys = QLocale::system().name().left(2);
        code = codes().contains(sys) ? sys : QStringLiteral("en");
    }
    m_effective = code;
    m_translator.load(code);
    if (!m_installed) {
        QCoreApplication::installTranslator(&m_translator);
        m_installed = true;
    } else {
        // Neu installieren -> LanguageChange-Ereignis
        QCoreApplication::removeTranslator(&m_translator);
        QCoreApplication::installTranslator(&m_translator);
    }
    if (m_engine)
        m_engine->retranslate();
    emit languageChanged();
}
