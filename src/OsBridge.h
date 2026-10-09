#pragma once

#include <QJSValue>
#include <QObject>
#include <QQuickImageProvider>
#include <QTimer>
#include <QVariant>

#include <memory>

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
    // vom Stick oder von der Disc gestartet, nicht von einer Platte des Geräts
    Q_PROPERTY(bool live READ live CONSTANT)
    // kein Grafiktreiber: OpenGL rechnet der Prozessor (eine virtuelle Maschine, ein Gerät ohne Treiber)
    Q_PROPERTY(bool softwareGraphics READ softwareGraphics CONSTANT)
    // die Oberfläche kommt aus einem Zip des Quelltexts (Aktualisierung ohne Netz), nicht aus dem Programm
    Q_PROPERTY(QString overlayVersion READ overlayVersion CONSTANT)
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
    bool live() const;
    bool softwareGraphics() const;
    QString overlayVersion() const;

    // Was die Oberfläche von diesem Programm verlangen darf. Jede Erweiterung, die die QML-Seiten
    // brauchen (neue Eigenschaft, neue Funktion), zählt hier eins weiter – und in os/system/API.
    // Ein Zip des Quelltexts, das mehr verlangt, wird nicht übernommen (lumenos-offline).
    static constexpr int kApi = 1;
    // Ordner mit einer Oberfläche aus dem Quelltext (qml/, i18n/, icons/), leer = keiner gültig
    static QString overlayDir();
    // Bild eines Symbols: aus dem Zip, wenn es dort eines gibt, sonst aus dem Programm
    Q_INVOKABLE QString icon(const QString &name) const;
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
    struct Scan;
    QString helper() const;
    void readUpdate();
    void scanPlaces();
    void tellBusy();
    void useDiscLibraries();

    bool m_active = false;
    bool m_playing = false; // Wiedergabe oder Kopie läuft
    int m_working = 0;      // Aufträge an das System, die dauern (MakeMKV bauen, auf die Platte installieren)
    // Umgebungsvariablen für die Disc-Bibliotheken, die diese Klasse setzt (von außen gesetzte bleiben)
    bool m_ownAacs = false, m_ownDvdcss = false;
    QByteArray m_aacs, m_dvdcss;
    // Zustand der Suche nach Datenträgern, geteilt mit ihrem Faden (der die Oberfläche überleben kann)
    std::shared_ptr<Scan> m_scan;
    QVariantList m_places;
    QVariantMap m_info;
    QVariantMap m_update;
    QTimer m_poll;
};

// Der Hintergrund der Oberfläche ("image://lumenos/backdrop"): ein dunkler Verlauf mit zwei weichen
// Lichtflecken, je Bildpunkt gerechnet und mit Rauschen auf 8 Bit gerundet, damit er keine Ringe zeigt.
class OsBackdrop : public QQuickImageProvider
{
public:
    OsBackdrop() : QQuickImageProvider(QQuickImageProvider::Image) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};
