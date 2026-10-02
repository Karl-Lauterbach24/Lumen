#pragma once

#include <QImage>
#include <QList>
#include <QRectF>
#include <QString>
#include <QVariantMap>

#include <functional>

// Untertitel einer 3D-Datei je Auge. mpv zeichnet Untertitel einmal über das ganze Ausgabebild;
// bei Side-by-Side, Top-Bottom und Frame Packing landet der Text damit zur Hälfte in jedem Auge
// und ist nicht zu lesen. Für Text-Untertitel zeichnet Lumen ihn deshalb selbst, einmal je Auge
// (als Overlay), und kann ihn dabei vor die Leinwand holen.
namespace StereoSubs {

// Geräteformate, bei denen jedes Auge einen eigenen Bereich des Ausgabebilds hat
bool splitsEyes(const QString &stereoOut);

// Untertitelformate, die Bilder statt Text liefern (dort bleibt es bei mpvs Darstellung)
bool isBitmapCodec(const QString &codec);

struct Eye
{
    QRectF rect;   // Bereich des Auges im Player-Fenster (OSD-Koordinaten)
    QSizeF logical; // Größe, in der das Auge am Ende gezeigt wird (bei halben Formaten gestreckt)
    bool left = true;
};

// Bereiche der Augen aus mpvs "osd-dimensions" (w, h, ml, mt, mr, mb) und dem Geräteformat
QList<Eye> eyes(const QString &stereoOut, const QVariantMap &osd);

// Text in ein Bild der Breite des Auges setzen: mittig, umbrochen, weiß mit schwarzem Rand.
// Leeres Bild, wenn es nichts zu zeigen gibt.
QImage render(const QString &text, const QSizeF &logical);

// Zeilen so umbrechen, dass keine breiter ist als maxWidth (measure: Breite eines Textes)
QStringList wrap(const QString &text, double maxWidth, const std::function<double(const QString &)> &measure);

} // namespace StereoSubs
