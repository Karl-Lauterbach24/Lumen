#pragma once

#include <QImage>

struct bd_overlay_s;

// Komprimierte Grafik von libbluray: Menüs im HDMV-Modus (IG) und Untertitel (PG). Ein Ereignis
// beschreibt Lauflängen mit Farbnummern und eine Palette aus Y, Cr, Cb und Deckkraft (BT.709,
// 16–235); apply() zeichnet es in eine ARGB32-Ebene, wie sie die BD-J-Grafik schon mitbringt.
namespace BdOverlay {

// true: die Ebene hat sich fertig geändert und soll gezeigt werden (FLUSH, CLOSE)
bool apply(QImage &plane, const bd_overlay_s &ov);

} // namespace BdOverlay
