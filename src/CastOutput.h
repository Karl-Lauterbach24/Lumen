#pragma once

#include <QString>
#include <QVariantMap>

class CastEncoder;

// Der Player aus Sicht der Übertragung (MpvController; im Test eine einfache mpv-Instanz)
class CastOutput
{
public:
    virtual ~CastOutput() = default;
    // Bild an den Encoder statt in ein Fenster, Ton als PCM in die Pipe pcmPath.
    // nullptr beendet die Übertragung; die laufende Wiedergabe geht an derselben Stelle weiter.
    virtual void setCastOutput(CastEncoder *encoder, const QString &pcmPath = {}) = 0;
    virtual QString castTitle() const = 0;
};

// mpv-Optionen der Übertragung: Bild über die Render-API in einen Framebuffer (SDR, BT.709);
// Ton an ein simuliertes Gerät (hält die Wiedergabe in Echtzeit), das jeden Block mit seiner
// Abspielzeit in die Pipe des Encoders schreibt (48 kHz, Stereo, 16 Bit) – eine Erweiterung
// von Lumens libmpv, siehe tools/patches/mpv-ao-null-outfile.patch
inline QVariantMap castMpvOptions(const QString &pcmPath)
{
    return {
        {"vo", "libmpv"},
        {"force-window", "no"},
        {"icc-profile-auto", "no"},
        {"target-colorspace-hint", "no"},
        {"target-prim", "bt.709"},
        {"target-trc", "bt.1886"},
        {"target-peak", "auto"},
        {"video-sync", "audio"},
        {"interpolation", "no"},
        {"ao", "null"},
        {"ao-null-outfile", pcmPath},
        {"ao-null-buffer", "0.1"},
        {"ao-null-format", "s16"},
        {"ao-null-channel-layouts", "stereo"},
        {"audio-samplerate", "48000"},
        {"audio-channels", "stereo"},
        {"audio-spdif", ""},
        {"audio-exclusive", "no"},
        {"gapless-audio", "yes"}, // Ausgabe über Dateiwechsel hinweg offen halten
    };
}

// Hat diese libmpv den Ton-Abgriff (Option ao-null-outfile)?
bool castTapAvailable();
