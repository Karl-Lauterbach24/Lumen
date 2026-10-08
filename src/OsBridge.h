#pragma once

#include <QJSValue>
#include <QObject>
#include <QTimer>
#include <QVariant>

// LumenOS: Lumen als einziges Programm eines Abspielgeräts ("lumen --os"). Diese Klasse ist, was die
// Oberfläche dafür vom System braucht: die Orte, an denen Filme liegen (interner Speicher, USB,
// Netzlaufwerke), deren Ordner, und ein Weg zu den Dingen, die nur das System darf (Netz, Bluetooth,
// Netzlaufwerke einhängen, aktualisieren, ausschalten) – das erledigt ein Hilfsprogramm mit
// Systemrechten, share/lumen/os/lumenos-admin, das hier nur aufgerufen wird.
class OsBridge : public QObject
{
    Q_OBJECT
    // als Abspielgerät gestartet ("--os" oder LUMEN_OS=1)
    Q_PROPERTY(bool active READ active CONSTANT)
    // bildschirmfüllend, das Player-Fenster nur während der Wiedergabe (LUMEN_OS_WINDOWED=1: im Fenster)
    Q_PROPERTY(bool kiosk READ kiosk CONSTANT)
    // das Hilfsprogramm mit Systemrechten ist da (ein LumenOS-System, nicht nur der Modus)
    Q_PROPERTY(bool system READ system CONSTANT)
    // [{name, path, kind: "internal" | "usb" | "network" | "disc", free, total}]
    Q_PROPERTY(QVariantList places READ places NOTIFY placesChanged)
    // {version, hostname, system, addresses: [..]}
    Q_PROPERTY(QVariantMap info READ info NOTIFY infoChanged)
    // Stand der Aktualisierung, wie ihn der Dienst schreibt: {state, current, latest, message}
    Q_PROPERTY(QVariantMap update READ update NOTIFY updateChanged)

public:
    explicit OsBridge(bool active, QObject *parent = nullptr);

    bool active() const { return m_active; }
    bool kiosk() const { return m_active && !qEnvironmentVariableIsSet("LUMEN_OS_WINDOWED"); }
    bool system() const;
    QVariantList places() const { return m_places; }
    QVariantMap info() const;
    QVariantMap update() const { return m_update; }

    // Hilfsprogramm aufrufen; done(exitCode, stdout, stderr), wenn es fertig ist
    Q_INVOKABLE void admin(const QStringList &arguments, const QJSValue &done = QJSValue());
    // Ordner lesen, ohne die Oberfläche aufzuhalten (ein Netzlaufwerk kann dauern): folderListed
    Q_INVOKABLE void listFolder(const QString &path);
    Q_INVOKABLE QString parentFolder(const QString &path) const;
    Q_INVOKABLE void refresh();
    // Etwas läuft (Wiedergabe, Kopieren): die Aktualisierung des Systems wartet so lange
    void setBusy(bool busy);

    // Dateien, die als Film gelten
    static bool isMedia(const QString &fileName);

signals:
    void placesChanged();
    void infoChanged();
    void updateChanged();
    // entries: [{name, path, dir, kind, size}], Ordner zuerst; kind: "folder", "file" oder eine Disc-Art
    void folderListed(const QString &path, const QVariantList &entries);

private:
    QString helper() const;
    void readUpdate();

    bool m_active = false;
    QVariantList m_places;
    QVariantMap m_update;
    QTimer m_poll;
};
