#include "Edid.h"

#include <cmath>

namespace {

const int kBlock = 128;

// Kennungen der Hersteller (OUI), niederwertiges Byte zuerst, wie sie im Block stehen
bool oui(const uchar *p, uchar a, uchar b, uchar c)
{
    return p[0] == a && p[1] == b && p[2] == c;
}

void audioBlock(const uchar *p, int length, EdidInfo &info)
{
    // Kurzbeschreibungen der Tonformate, je drei Bytes
    for (int i = 0; i + 3 <= length; i += 3) {
        const int format = (p[i] >> 3) & 0x0f;
        const int channels = (p[i] & 0x07) + 1;
        const auto add = [&info](const char *name) {
            if (!info.bitstream.contains(QLatin1String(name)))
                info.bitstream << QLatin1String(name);
        };
        switch (format) {
        case 1: info.pcmChannels = qMax(info.pcmChannels, channels); break;
        case 2: add("ac3"); break;
        case 7: add("dts"); break;
        case 10:
            add("eac3");
            if (p[i + 2] & 0x01)
                info.atmos = true;
            break;
        case 11: add("dts-hd"); break;
        case 12: add("truehd"); break;
        default: break;
        }
    }
}

// hdr: Farbmetrik und HDR werden gelesen (nur in der ersten CTA-Erweiterung, siehe parseEdid)
void extendedBlock(const uchar *p, int length, EdidInfo &info, bool hdr)
{
    if (length < 1)
        return;
    if (!hdr && (p[0] == 0x05 || p[0] == 0x06))
        return;
    switch (p[0]) {
    case 0x01: // Videodaten eines Herstellers
        if (length >= 4 && oui(p + 1, 0x46, 0xd0, 0x00))
            info.dolbyVision = true;
        if (length >= 4 && oui(p + 1, 0x8b, 0x84, 0x90))
            info.hdr10plus = true;
        break;
    case 0x05: // Farbmetrik: BT.2020 als RGB – so gibt die Grafik das Bild aus
        if (length >= 2 && (p[1] & 0x80))
            info.bt2020 = true;
        break;
    case 0x06: // statische HDR-Metadaten
        if (length >= 2) {
            info.hdr10 = info.hdr10 || (p[1] & 0x04);
            info.hlg = info.hlg || (p[1] & 0x08);
        }
        if (length >= 4 && p[3] > 0)
            info.maxLuminance = int(std::lround(50.0 * std::pow(2.0, p[3] / 32.0)));
        break;
    default:
        break;
    }
}

void ctaExtension(const uchar *block, EdidInfo &info, bool first)
{
    if (block[1] < 3)
        return; // vor Version 3 gibt es keine Datenblöcke
    int end = block[2]; // dort beginnen die Zeitangaben; davor liegen die Datenblöcke
    if (end == 0)
        end = kBlock - 1;
    end = qMin(end, kBlock - 1);
    // "Grundton": Stereo, unkomprimiert, auch ohne eigene Beschreibung
    if (block[3] & 0x40)
        info.pcmChannels = qMax(info.pcmChannels, 2);
    for (int at = 4; at < end;) {
        const int tag = block[at] >> 5;
        const int length = block[at] & 0x1f;
        if (at + 1 + length > end)
            break;
        const uchar *p = block + at + 1;
        if (tag == 1)
            audioBlock(p, length, info);
        else if (tag == 3 && length >= 3 && oui(p, 0x8b, 0x84, 0x90))
            info.hdr10plus = true;
        else if (tag == 7)
            extendedBlock(p, length, info, first);
        at += 1 + length;
    }
}

} // namespace

EdidInfo parseEdid(const QByteArray &edid)
{
    EdidInfo info;
    static const uchar header[8] = {0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00};
    if (edid.size() < kBlock || memcmp(edid.constData(), header, sizeof header) != 0)
        return info;
    const auto *data = reinterpret_cast<const uchar *>(edid.constData());
    info.valid = true;
    // vier Deskriptoren zu 18 Bytes; einer davon trägt den Namen
    for (int at = 54; at + 18 <= 126; at += 18) {
        if (data[at] == 0 && data[at + 1] == 0 && data[at + 3] == 0xfc) {
            QByteArray name(reinterpret_cast<const char *>(data + at + 5), 13);
            const int newline = name.indexOf('\n');
            if (newline >= 0)
                name.truncate(newline);
            info.name = QString::fromLatin1(name).trimmed();
        }
    }
    // HDR und Farbmetrik zählen nur aus der ersten CTA-Erweiterung: mpv liest sie auch nur dort, und
    // es kündigt dem Bildschirm HDR nur an, wenn es selbst sie findet. Läse Lumen mehr, gäbe es ein
    // HDR-Bild aus, das der Bildschirm für SDR hält.
    bool first = true;
    const int extensions = data[126];
    for (int i = 1; i <= extensions && (i + 1) * kBlock <= edid.size(); ++i) {
        const uchar *block = data + i * kBlock;
        if (block[0] == 0x02) {
            ctaExtension(block, info, first);
            first = false;
        }
    }
    return info;
}
