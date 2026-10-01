#pragma once

#include <QByteArray>
#include <QtGlobal>

#include <deque>

// Ordnet den Ton aus mpv auf der Zeitachse des Sendestroms an.
//
// Lumens libmpv schreibt jeden Block, den das (simulierte) Tongerät annimmt, mit dem Zeitpunkt
// in die Pipe, zu dem dieses Gerät ihn abspielt – dazu Pause, Fortsetzen und Verwerfen (Sprung).
// Damit liegt der Ton im Strom genau dort, wo mpv ihn zum Bild hören will, auch nach Pausen
// und Sprüngen. Satzaufbau: tools/patches/mpv-ao-null-outfile.patch.
//
// Zeiten sind Samples (48 kHz) seit Beginn des Stroms.
class CastAudio
{
public:
    // Abstand der Uhr des Stroms zur Uhr von mpv, in Sekunden (Strom = mpv + offset)
    void setOffset(double offset) { m_offset = offset; }
    // rohe Bytes aus der Pipe
    void feed(const char *data, int size);
    // Ton für [start, start + samples) nach out (Stereo, 16 Bit); wo nichts liegt: Stille.
    // Liefert die Anzahl der Samples mit echtem Ton.
    int take(qint64 start, qint16 *out, int samples);
    // noch nicht abgespielter Ton in Samples (für die Diagnose)
    qint64 queued() const;

    // So viel später als von mpv verlangt erscheinen Bild und Ton im Strom: Spielraum, damit
    // ein Block sicher eingetroffen ist, bevor er an der Reihe ist
    static constexpr double kDelay = 0.08;

private:
    struct Block
    {
        qint64 start;
        QByteArray pcm;
    };
    qint64 toSample(qint64 timeUs) const;
    void record(quint32 type, qint64 timeUs, const char *payload, int samples);
    void cutAt(qint64 sample);

    double m_offset = 0;
    QByteArray m_in;
    std::deque<Block> m_blocks;
    qint64 m_end = -1;        // Ende des zuletzt eingereihten Blocks (-1: kein Anschluss)
    qint64 m_pausedAt = -1;   // Gerät angehalten seit (Sample), -1 = läuft
};
