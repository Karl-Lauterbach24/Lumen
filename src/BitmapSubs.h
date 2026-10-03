#pragma once

#include <QImage>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QRect>
#include <QSize>
#include <QString>
#include <QThread>

#include <atomic>

// Bild-Untertitel (PGS, VobSub, DVB) einer Datei, selbst gelesen und dekodiert. mpv liefert von
// solchen Untertiteln nur das fertige Gesamtbild; für eine 3D-Ausgabe mit einem Bereich je Auge
// (StereoSubs) braucht Lumen die einzelnen Untertitelbilder, um sie einmal je Auge zu zeichnen.
//
// Ein Thread öffnet die Datei ein zweites Mal, liest nur die gewählte Untertitelspur und legt jedes
// Untertitelbild mit Anfang und Ende ab (Zeit wie mpvs time-pos: ab dem Anfang der Datei). Er liest
// eine halbe Minute vor der Wiedergabe voraus; springt die Wiedergabe in einen Bereich, den er
// noch nicht gelesen hat, setzt er dort neu an.
class BitmapSubs : public QObject
{
    Q_OBJECT
public:
    struct Event
    {
        double start = 0;
        double end = -1;   // < 0: bis zum nächsten Untertitel
        QSize canvas;      // Bezugsgröße der Positionen (z. B. 1920x1080)
        QRect rect;        // Lage des Bildes auf dieser Fläche
        QImage image;      // ARGB32, vormultipliziert; leer = Untertitel aus
        bool forced = false;
    };

    explicit BitmapSubs(QObject *parent = nullptr);
    ~BitmapSubs() override;

    // Spur streamIndex (FFmpeg-Zählung) der Datei path lesen; ein anderer Aufruf beendet den vorigen.
    // timeOffset: was mpv von den Zeitstempeln abzieht (Anfangszeit der abgespielten Datei, auch für
    // eine geladene Untertiteldatei); < 0: die Anfangszeit dieser Datei
    void start(const QString &path, int streamIndex, const QString &codec, double timeOffset = -1);
    void stop();
    bool active(const QString &path, int streamIndex) const;
    // Wiedergabeposition (Sekunden): der Leser bleibt in ihrer Nähe
    void setPosition(double seconds);
    // Das Untertitelbild zur Zeit t (leer, wenn keines zu sehen ist)
    Event at(double t, bool forcedOnly) const;

signals:
    void eventsChanged(); // neue Untertitelbilder gelesen (aus dem Lese-Thread)

private:
    void run(QString path, int streamIndex, QString codec, double timeOffset);
    void addEvent(Event e);

    QThread *m_thread = nullptr;
    std::atomic_bool m_stop{false};
    std::atomic<double> m_position{0};
    std::atomic<double> m_seekTo{-1};
    mutable QMutex m_lock;
    QList<Event> m_events; // nach Anfang sortiert
    double m_coveredFrom = 0, m_coveredTo = -1; // gelesener Abschnitt
    QString m_path;
    int m_stream = -1;
};
