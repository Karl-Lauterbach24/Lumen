#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

// Was ein Bildschirm – und der Verstärker davor, der seine eigenen Angaben einträgt – über sich
// sagt: EDID, der Grundblock und die Erweiterungen nach CTA-861. Gelesen wird, wonach LumenOS seine
// Ausgabe einrichtet: welche HDR-Kennlinien das Gerät annimmt und welche Tonformate es selbst
// entschlüsselt (die werden ihm dann unverändert durchgereicht).
struct EdidInfo
{
    bool valid = false;
    QString name;            // Name des Geräts (Deskriptor 0xFC)

    // Bild
    bool hdr10 = false;      // Kennlinie SMPTE ST 2084 (PQ): HDR10, und was sich dahin wandeln lässt
    bool hlg = false;        // Kennlinie Hybrid Log-Gamma
    bool bt2020 = false;     // Farbraum BT.2020 (als RGB)
    bool hdr10plus = false;  // HDR10+ (Block des Herstellers)
    bool dolbyVision = false;
    int maxLuminance = 0;    // cd/m², die das Gerät als Spitze nennt; 0 = keine Angabe

    // Ton: Formate, die das Gerät als Datenstrom annimmt, mit mpvs Namen (--audio-spdif)
    QStringList bitstream;   // "ac3", "eac3", "dts", "dts-hd", "truehd"
    bool atmos = false;      // Dolby Atmos in Dolby Digital Plus
    int pcmChannels = 0;     // höchste Kanalzahl für unkomprimierten Ton; 0 = kein Ton genannt
};

EdidInfo parseEdid(const QByteArray &edid);
