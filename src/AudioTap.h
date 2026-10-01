#pragma once

#include <QByteArray>
#include <QString>

#include <atomic>

// Die Pipe, in die mpv den Ton der Übertragung schreibt (ao-null-outfile), von Lumen gelesen.
// Was darin steht, ordnet CastAudio.
//
// Alle Aufrufe blockieren nicht.
class AudioTap
{
public:
    AudioTap();
    ~AudioTap();

    bool open();
    void close();
    // Pfad für mpv (--ao-null-outfile)
    QString path() const { return m_path; }
    // bis zu maxBytes lesen; liefert die gelesene Anzahl (0 = nichts da)
    int read(char *data, int maxBytes);
    // Bytes, die geschrieben, aber noch nicht gelesen sind
    int pending() const;

    static constexpr int kRate = 48000;
    static constexpr int kChannels = 2;
    static constexpr int kBytesPerSecond = kRate * kChannels * 2; // s16

private:
    QString m_path;
#ifdef _WIN32
    // zwei Instanzen: mpv kann die Ausgabe neu öffnen, bevor die alte als geschlossen erkannt ist
    void *m_pipe[2] = {nullptr, nullptr};
    std::atomic<bool> m_connected[2] = {false, false};
    void poll();
#else
    int m_fd = -1;
    int m_keep = -1; // eigener Schreiber: hält die Pipe offen, wenn mpv sie schließt
#endif
};
