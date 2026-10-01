#pragma once

#include "CastDiscovery.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QSslSocket>
#include <QTimer>

class CastServer;
class QJsonObject;

// Ein Empfänger, dem Lumen die Adresse des Sendestroms gibt. Die Wiedergabe selbst steuert
// weiter Lumen: Pause, Springen, Menüs und Lautstärke wirken auf den Strom, der Empfänger
// spielt ihn nur ab.
class CastTarget : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;
    static CastTarget *create(const CastDevice &device, CastServer *server, QObject *parent);

    // hls: Wiedergabeliste, ts: fortlaufender MPEG-TS-Strom
    virtual void start(const QString &hls, const QString &ts, const QString &title) = 0;
    virtual void stop() = 0;

signals:
    // state: "connecting", "playing", "error"
    void stateChanged(const QString &state, const QString &message = {});
};

// DLNA/UPnP-Renderer: AVTransport SetAVTransportURI + Play
class DlnaTarget : public CastTarget
{
    Q_OBJECT

public:
    DlnaTarget(const CastDevice &device, QObject *parent);
    void start(const QString &hls, const QString &ts, const QString &title) override;
    void stop() override;

private:
    void soap(const QString &action, const QString &arguments, std::function<void(bool, const QString &)> done);

    CastDevice m_device;
    QNetworkAccessManager m_net;
};

// Chromecast / Google Cast: CASTV2 über TLS (Port 8009), Standard-Medienempfänger
class ChromecastTarget : public CastTarget
{
    Q_OBJECT

public:
    ChromecastTarget(const CastDevice &device, QObject *parent);
    void start(const QString &hls, const QString &ts, const QString &title) override;
    void stop() override;

private:
    void send(const QString &ns, const QJsonObject &payload, const QString &destination = QStringLiteral("receiver-0"));
    void onReadyRead();
    void onMessage(const QString &source, const QString &ns, const QJsonObject &payload);

    CastDevice m_device;
    QSslSocket m_socket;
    QTimer m_heartbeat;
    QByteArray m_buffer;
    QString m_url;
    QString m_title;
    QString m_transport;   // Sitzung des Medienempfängers
    QString m_session;
    int m_request = 0;
    bool m_loaded = false;
    bool m_stopping = false;
};

// AirPlay (Video per Adresse, ohne Kopplung): POST /play an Port 7000. Die Verbindung bleibt
// offen und wird regelmäßig abgefragt – beim Schließen beendet das Gerät die Wiedergabe.
class AirplayTarget : public CastTarget
{
    Q_OBJECT

public:
    AirplayTarget(const CastDevice &device, QObject *parent);
    void start(const QString &hls, const QString &ts, const QString &title) override;
    void stop() override;

private:
    void request(const QByteArray &method, const QByteArray &path, const QByteArray &body = {});
    void onReadyRead();

    CastDevice m_device;
    QTcpSocket m_socket;
    QTimer m_keepAlive;
    QByteArray m_buffer;
    QByteArray m_sessionId;
    QString m_url;
    QList<QByteArray> m_pending; // Pfade der offenen Anfragen, in Sendereihenfolge
    bool m_playing = false;
};

// Lumen-TV-App oder Browser-Empfänger: Befehl über die wartende Abfrage der App
class TvTarget : public CastTarget
{
    Q_OBJECT

public:
    TvTarget(const CastDevice &device, CastServer *server, QObject *parent);
    void start(const QString &hls, const QString &ts, const QString &title) override;
    void stop() override;

private:
    QString m_client;
    CastServer *m_server;
};
