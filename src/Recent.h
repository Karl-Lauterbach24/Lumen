#pragma once

#include <QObject>
#include <QVariantList>

// Zuletzt gespielte lokale Quellen (Dateien, Abbilder, Disc-/DCP-Ordner) für die
// Startseite, mit der letzten Position zum Fortsetzen. Netzwerk-Adressen werden
// nicht aufgenommen (sie können Zugangsdaten enthalten; Streams haben ihren
// eigenen Verlauf im Tab „Streaming“).
class Recent : public QObject
{
    Q_OBJECT
    // Neueste zuerst, nur noch vorhandene Quellen:
    // {path, kind, title, position, duration}
    Q_PROPERTY(QVariantList items READ items NOTIFY changed)

public:
    explicit Recent(QObject *parent = nullptr);

    QVariantList items() const;
    // Quelle wurde geöffnet (rückt an den Anfang); leerer Titel lässt den alten stehen
    void note(const QString &path, const QString &kind, const QString &title);
    // Position merken; kurz vor dem Ende gilt die Quelle als fertig gespielt (Position 0)
    void notePosition(const QString &path, double position, double duration);
    Q_INVOKABLE void remove(const QString &path);
    Q_INVOKABLE void clear();

    static constexpr int kMax = 12;

signals:
    void changed();

private:
    void save();
    int indexOf(const QString &path) const;

    QVariantList m_items;
};
