#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

// DCP-Untertitel (Interop "DCSubtitle"-XML, SMPTE ST 428-7 Timed Text in MXF,
// auch verschlüsselt) -> eine ASS-Datei über alle Rollen, mit Position
// (VAlign/VPosition, HAlign/HPosition), Schriftgröße, Farbe, Kursiv/Fett,
// Rand/Schatten und Ein-/Ausblendung. Eingebettete Schriften werden extrahiert.
namespace Dcp {

struct SubtitleSource
{
    QString file;
    QByteArray key;
    double reelStart = 0;     // Position der Rolle in der Wiedergabe (s)
    double entry = 0;         // Einstiegspunkt der Spur (s)
    double length = 0;        // verwendete Länge (s)
    QString language;
};

struct SubtitleResult
{
    QString assFile;
    QString fontsDir;
    int events = 0;
    int images = 0;           // Bilduntertitel (PNG) – nicht unterstützt
    QString language;
    QString error;
};

SubtitleResult buildSubtitles(const QList<SubtitleSource> &sources, const QString &outDir, const QString &name);

// Familienname aus einer TrueType/OpenType-Datei ("name"-Tabelle)
QString fontFamily(const QByteArray &font);

} // namespace Dcp
