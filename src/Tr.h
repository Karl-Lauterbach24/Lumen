#pragma once

#include <QCoreApplication>
#include <QString>

// Übersetzbarer Text. Quellsprache ist Deutsch; die Wörterbücher liegen in
// i18n/<sprache>.json und werden von I18n geladen (tools/i18n.py hält sie aktuell).
#define LTR(s) QCoreApplication::translate("Lumen", s)

// Laufzeit-Text (z. B. Name einer Vorlage aus presets.json) nachschlagen
inline QString ltrDynamic(const QString &s)
{
    return QCoreApplication::translate("Lumen", s.toUtf8().constData());
}
