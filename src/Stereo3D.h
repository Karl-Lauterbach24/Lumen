#pragma once

#include <QString>
#include <QVariantMap>

// Filterketten (FFmpeg/lavfi, als mpv-Option "vf"), die das 3D-Format der Quelle in das
// Format bringen, das das Ausgabegerät erwartet.
namespace Stereo3D {

// Quellformat -> Geräteformat (stereo3d-Kürzel: sbsl, sbs2l, abl, ab2l, irl, arcd, ml …;
// "fp" = HDMI Frame Packing 1920x2205, "seq" = Bildfolge, siehe sequential()).
// in == "none": keine Kette. out == "none": nur das linke Auge (2D).
QString filter(const QString &in, const QVariantMap &profile);

// Bildfolge für Shutterbrillen (experimentell): je Bildwechsel des Bildschirms genau ein Bild,
// abwechselnd linkes und rechtes Auge nach einem frei wählbaren Muster.
//
// Einstellungen im Profil:
//   seqRate       Bildwechsel pro Sekunde = Bildwiederholrate des Geräts (z. B. 120, 144, 240)
//   seqPattern    Folge aus L (links), R (rechts), S (Synchronbild), B (Schwarzbild), z. B.
//                 "LR"   einfacher Wechsel
//                 "LSRS" nach jedem Auge ein Synchronbild (Versuch, DLP-Link nachzubilden)
//                 "LBRB" Schwarzbild nach jedem Auge (weniger Übersprechen)
//   seqSyncColor  Farbe der Synchronbilder ("#ff0000"), seqSyncLevel deren Helligkeit in Prozent
//   seqBox        Messfeld für einen Lichtsensor am Bildschirmrand: "none", "tl", "tr", "bl", "br";
//                 weiß bei L, sonst schwarz. seqBoxSize: Kantenlänge in Prozent der Bildhöhe
//   seqSwap       Augen tauschen
//   seqPhase      Muster um so viele Bilder verschieben
QString sequential(const QString &in, const QVariantMap &profile);

// Muster auf gültige Zeichen bringen (mindestens ein L und ein R, höchstens 12 Zeichen)
QString cleanPattern(const QString &pattern);

} // namespace Stereo3D
