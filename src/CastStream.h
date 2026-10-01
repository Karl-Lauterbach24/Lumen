#pragma once

#include <QByteArray>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QString>

// Der kodierte Sendestrom (MPEG-TS, H.264 + AAC) als Folge von Segmenten.
//
// Der Encoder (eigener Thread) hängt Daten an das offene Segment an und schließt es
// an einem Schlüsselbild ab; der HTTP-Server liest daraus
//   - die HLS-Wiedergabeliste und einzelne Segmente (Chromecast, AirPlay, TV-Apps, Browser)
//   - einen fortlaufenden Strom ab dem letzten Schlüsselbild (DLNA-Geräte).
// Jedes Segment beginnt mit PAT/PMT und einem Schlüsselbild.
class CastStream : public QObject
{
    Q_OBJECT

public:
    explicit CastStream(QObject *parent = nullptr);

    // Lesemarke eines fortlaufenden Empfängers
    struct Cursor
    {
        qint64 segment = -1;
        qint64 offset = 0;
    };

    void reset();

    // --- Encoder ---
    void append(const QByteArray &bytes);
    void closeSegment(double duration);

    // --- Server ---
    // Anzahl abgeschlossener Segmente seit dem Start
    qint64 segmentCount() const;
    QByteArray playlist(const QString &segmentPrefix) const;
    // leer, wenn das Segment nicht (mehr) vorhanden ist
    QByteArray segment(qint64 index) const;
    // Fortlaufend: neue Daten hinter der Marke; beim ersten Aufruf ab dem jüngsten
    // abgeschlossenen Segment. false = Marke liegt vor dem ältesten Segment (Empfänger zu langsam)
    bool read(Cursor &cursor, QByteArray &out) const;

    static constexpr int kKeep = 40;        // aufbewahrte Segmente
    static constexpr int kPlaylist = 6;     // Segmente in der Wiedergabeliste

signals:
    void dataAvailable();
    void segmentClosed(qint64 count);

private:
    struct Segment
    {
        qint64 index;
        double duration;
        QByteArray data;
    };
    mutable QMutex m_mutex;
    QList<Segment> m_segments; // abgeschlossen
    QByteArray m_open;
    qint64 m_next = 0;          // Index des offenen Segments
};
