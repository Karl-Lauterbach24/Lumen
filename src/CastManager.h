#pragma once

#include "AudioTap.h"
#include "CastDiscovery.h"
#include "CastEncoder.h"
#include "CastServer.h"
#include "CastStream.h"

#include <QObject>
#include <QTimer>
#include <QVariantList>

class CastTarget;
class CastOutput;

// Übertragung der Player-Ausgabe an Empfänger im lokalen Netz (QML: "Cast").
//
//   DLNA/UPnP, Chromecast, AirPlay (ohne Kopplung), Lumen-TV-Apps und jeder Browser.
//   Miracast und AirPlay-Bildschirmsynchronisierung laufen über das Betriebssystem: der
//   Empfänger erscheint dort als Bildschirm, den Lumen wie jeden anderen bespielt.
//
// Ablauf: Encoder + HTTP-Server starten, den Player auf „Ausgabe in den Encoder“ umschalten,
// auf die ersten Segmente warten, dann dem Empfänger die Adresse geben.
class CastManager : public QObject
{
    Q_OBJECT
    // H.264/AAC-Encoder vorhanden
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    // idle | starting | connecting | playing | error
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool active READ active NOTIFY stateChanged)
    Q_PROPERTY(QString message READ message NOTIFY stateChanged)
    Q_PROPERTY(QString deviceId READ deviceId NOTIFY stateChanged)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY stateChanged)
    // Adressen für TV-Apps und Browser („http://192.168.1.20:47800“), leer = Server läuft nicht
    Q_PROPERTY(QStringList addresses READ addresses NOTIFY listeningChanged)
    Q_PROPERTY(bool listening READ listening NOTIFY listeningChanged)
    // Einstellungen (bleiben gespeichert)
    Q_PROPERTY(int height READ height WRITE setHeight NOTIFY settingsChanged)
    Q_PROPERTY(int fps READ fps WRITE setFps NOTIFY settingsChanged)
    Q_PROPERTY(int bitrate READ bitrate WRITE setBitrate NOTIFY settingsChanged)
    // Server für TV-Apps schon beim Programmstart öffnen
    Q_PROPERTY(bool alwaysListen READ alwaysListen WRITE setAlwaysListen NOTIFY settingsChanged)

public:
    explicit CastManager(CastOutput *player, QObject *parent = nullptr);
    ~CastManager() override;

    bool available() const;
    QVariantList devices() const;
    QString state() const { return m_state; }
    bool active() const { return m_state != QLatin1String("idle") && m_state != QLatin1String("error"); }
    QString message() const { return m_message; }
    QString deviceId() const { return m_deviceId; }
    QString deviceName() const { return m_deviceName; }
    QStringList addresses() const;
    bool listening() const { return m_server.isListening(); }
    int height() const { return m_height; }
    int fps() const { return m_fps; }
    int bitrate() const { return m_bitrate; }
    bool alwaysListen() const { return m_alwaysListen; }
    void setHeight(int h);
    void setFps(int f);
    void setBitrate(int kbps);
    void setAlwaysListen(bool on);

    // Dialog geöffnet/geschlossen: Server öffnen, Geräte suchen (wiederholt, solange offen)
    Q_INVOKABLE void openDialog();
    Q_INVOKABLE void closeDialog();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void start(const QString &deviceId);
    Q_INVOKABLE void stop();
    // type: dlna | chromecast | airplay; address: Adresse bzw. bei DLNA die der Gerätebeschreibung
    Q_INVOKABLE bool addDevice(const QString &type, const QString &address);
    Q_INVOKABLE void removeDevice(const QString &deviceId);
    // Einstellungen des Systems für drahtlose Bildschirme (Miracast, AirPlay-Synchronisierung)
    Q_INVOKABLE bool openSystemDisplays();
    Q_INVOKABLE bool hasSystemDisplays() const;

    // Programmende: Übertragung beenden, ohne den Player neu zu starten
    void shutdown();

    CastServer *server() { return &m_server; }
    CastStream *stream() { return &m_stream; }
    CastDiscovery *discovery() { return &m_discovery; }

signals:
    void devicesChanged();
    void stateChanged();
    void listeningChanged();
    void settingsChanged();
    // Taste der Fernbedienung einer TV-App
    void remoteKey(const QString &key);

private:
    void setState(const QString &state, const QString &message = {});
    void teardown(bool restorePlayer);
    void ensureListening();

    CastOutput *m_player;
    CastStream m_stream;
    CastServer m_server;
    CastDiscovery m_discovery;
    AudioTap m_tap;
    CastEncoder *m_encoder = nullptr;
    CastTarget *m_target = nullptr;
    QTimer m_scanTimer;
    QTimer m_startTimeout;
    QString m_state = QStringLiteral("idle");
    QString m_message;
    QString m_deviceId;
    QString m_deviceName;
    CastDevice m_device;
    bool m_targetStarted = false;
    bool m_dialogOpen = false;
    int m_height = 1080;
    int m_fps = 30;
    int m_bitrate = 8000;
    bool m_alwaysListen = false;
};
