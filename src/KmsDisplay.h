#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

// Der Bildschirm, wie der Kern ihn führt (KMS): LumenOS gibt das Bild eines Films ohne Fenstersystem
// direkt dorthin aus (mpv: --gpu-context=drm). Nur so erfährt der Bildschirm, dass HDR kommt, bekommt
// 10 Bit je Farbe und die Bildrate des Films. Hier steht, was dafür vorher bekannt sein muss.
struct KmsMode
{
    int width = 0;
    int height = 0;
    double hz = 0; // Bildwechsel je Sekunde, wie mpv sie rechnet
    bool preferred = false;
};

struct KmsOutput
{
    bool valid = false;
    QString device;           // "/dev/dri/card0"
    QString connector;        // "HDMI-A-1" – der Name, den mpv erwartet (--drm-connector)
    QList<KmsMode> modes;     // ohne Halbbild-Betriebsarten
    bool hdrMetadata = false; // der Treiber kann dem Bildschirm HDR ankündigen
    bool colorspace = false;  // … und den Farbraum
    int maxBpc = 0;           // höchste Bittiefe je Farbe am Anschluss, 0 = keine Angabe
    QByteArray edid;
};

namespace Kms {

// mit libdrm gebaut (Linux)
bool available();
// der erste angeschlossene Bildschirm; valid = false, wenn es keinen gibt oder der Kern keinen führt
KmsOutput probe();
// Betriebsart für einen Film mit dieser Bildrate: die Auflösung der bevorzugten Betriebsart, der
// Bildwechsel ein Vielfaches der Bildrate (23,976 -> 23,976, sonst 47,952 …; 25 -> 25 oder 50).
// Als Text für mpv (--drm-mode: "3840x2160@23.98"); leer, wenn nichts passt oder fps unbekannt ist.
QString modeFor(const KmsOutput &output, double fps);

} // namespace Kms
