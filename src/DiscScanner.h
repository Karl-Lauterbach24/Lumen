#pragma once

#include <QObject>
#include <QVariant>

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

public:
    explicit DiscScanner(QObject *parent = nullptr);
    ~DiscScanner() override;

    bool available() const;
    bool busy() const { return m_busy; }
    QVariantMap info() const { return m_info; }

    Q_INVOKABLE void scan(const QString &device);
    Q_INVOKABLE void clear();

signals:
    void busyChanged();
    void infoChanged();

private:
    static QVariantMap scanBlocking(const QString &device);

    bool m_busy = false;
    int m_generation = 0;
    QVariantMap m_info;
};
