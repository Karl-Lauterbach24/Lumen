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

// Bilduntertitel (PNG): Lumen blendet sie als Overlay ein
struct ImageSub
{
    double start = 0, end = 0, fadeIn = 0, fadeOut = 0;
    QByteArray png;
    QString valign, halign;   // bottom/top/center, left/center/right
    double vpos = 0, hpos = 0; // Anteil der Bildhöhe/-breite (0..1)
};

struct SubtitleResult
{
    QString assFile;
    QString fontsDir;
    int events = 0;
    int images = 0;           // Bilduntertitel (PNG)
    QList<ImageSub> imageEvents;
    QString language;
    QString error;
};

SubtitleResult buildSubtitles(const QList<SubtitleSource> &sources, const QString &outDir, const QString &name);

// Familienname aus einer TrueType/OpenType-Datei ("name"-Tabelle)
QString fontFamily(const QByteArray &font);

} // namespace Dcp
