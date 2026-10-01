#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QMutex>
#include <QQueue>
#include <QThread>

#include <atomic>

class AudioTap;
class CastStream;

// Kodiert die Ausgabe des Players für Empfänger im Netz: H.264 + AAC in MPEG-TS.
//
// Bild:  fertige RGBA-Bilder des Renderers (GUI-Thread, pushFrame) – also genau das, was sonst
//        im Player-Fenster stünde (Disc-Menüs, Untertitel, Tonemapping, 3D-Umsetzung inklusive).
// Ton:   PCM von mpv über AudioTap, jeder Block mit dem Zeitpunkt, zu dem mpv ihn hören will
//        (CastAudio); fehlt Ton (Pause, Menü ohne Ton, Leerlauf), läuft Stille, damit der
//        Strom nie abreißt.
// Zeit:  eine gemeinsame Uhr ab Start, abgeglichen mit der Uhr von mpv. Bilder tragen den
//        Zeitpunkt, zu dem mpv sie zeigen will, Ton den, zu dem mpv ihn abspielt.
class CastEncoder : public QThread
{
    Q_OBJECT

public:
    struct Settings
    {
        int width = 1920;
        int height = 1080;
        int fpsLimit = 30;       // 30: läuft auf jedem Empfänger; 60: nur neuere Geräte
        int videoKbps = 8000;
        int audioKbps = 192;
        double segmentSeconds = 1.0;
    };

    CastEncoder(CastStream *stream, AudioTap *tap, const Settings &settings, QObject *parent = nullptr);
    ~CastEncoder() override;

    // Name des H.264-Encoders in der geladenen FFmpeg-Bibliothek, leer = keiner
    static QString videoEncoderName();

    void begin();
    void end();

    const Settings &settings() const { return m_settings; }
    // Uhr von mpv (mpv_get_time_us) mit der Uhr des Stroms abgleichen: einmal je mpv-Instanz
    void setMpvTime(qint64 mpvTimeUs);
    // Zeitstempel im Strom für einen Zeitpunkt auf der Uhr von mpv (Sekunden seit Start)
    double streamTime(qint64 mpvTimeUs) const;
    // rgba: width*height*4 Bytes, oberste Zeile zuerst
    void pushFrame(QByteArray &&rgba, double pts);

signals:
    void failed(const QString &message);

protected:
    void run() override;

private:
    struct Frame
    {
        QByteArray rgba;
        double pts;
    };
    double clock() const { return m_clock.nsecsElapsed() / 1e9; }

    CastStream *m_stream;
    AudioTap *m_tap;
    Settings m_settings;
    QElapsedTimer m_clock;
    std::atomic<bool> m_stop{false};
    std::atomic<double> m_offset{0.0};   // Uhr des Stroms minus Uhr von mpv
    QMutex m_mutex;
    QQueue<Frame> m_frames;
    std::atomic<int> m_pushed{0};
    std::atomic<int> m_overflow{0};
};
