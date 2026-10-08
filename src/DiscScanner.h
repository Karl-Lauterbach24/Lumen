#pragma once

#include <QObject>
#include <QTimer>
#include <QVariant>

#include <vector>

// Liest Disc-Informationen über libbluray (Titel/Playlists mit Laufzeit,
// Kapitel, Tonspuren, AACS/BD+/BD-J-Status). Läuft im Hintergrund-Thread.
// Ohne libbluray (Build-Option) liefert available=false und die Titelliste
// kommt stattdessen aus mpv.
class DiscScanner : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QVariantMap info READ info NOTIFY infoChanged)
    // Das Öffnen der Disc ist nicht zurückgekehrt: Laufwerk oder AACS-Bibliothek stehen. Lumen kann
    // danach keine Disc mehr öffnen, bis es neu gestartet ist (der Aufruf der Bibliothek endet nie).
    Q_PROPERTY(bool stuck READ stuck NOTIFY busyChanged)

public:
    explicit DiscScanner(QObject *parent = nullptr);
    ~DiscScanner() override;

    bool available() const;
    bool busy() const { return m_busy; }
    bool stuck() const { return m_stuck; }
    QVariantMap info() const { return m_info; }

    Q_INVOKABLE void scan(const QString &device);
    Q_INVOKABLE void clear();
    // Metadaten eines Plugins (Titel, Interpret, Jahr, Cover, Tracknamen) übernehmen:
    // {"device", "title", "artist", "year", "cover", "source", "tracks": [{"title", "artist"}]}
    void applyMetadata(const QVariantMap &meta);

signals:
    void busyChanged();
    void infoChanged();
    // Ein Scan ist abgeschlossen (infoChanged meldet auch nachträgliche Metadaten)
    void scanned();

private:
    static QVariantMap scanBlocking(const QString &device);

    void giveUp();

    bool m_busy = false;
    bool m_stuck = false;
    int m_generation = 0;
    QTimer m_watch;                 // läuft, solange gelesen wird
    QString m_device;
    std::vector<qint64> m_helpers;  // Hilfsprozesse, die vor dem Lesen schon liefen
    QVariantMap m_info;
};
