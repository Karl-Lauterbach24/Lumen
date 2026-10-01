#pragma once

#include "CastStream.h"

#include <QHash>
#include <QHostAddress>
#include <QJsonObject>
#include <QObject>
#include <QTcpServer>
#include <QTimer>
#include <QVariantList>

class QTcpSocket;

// Kleiner HTTP-Server für Empfänger im lokalen Netz.
//
//   /stream/<token>/live.m3u8   HLS-Wiedergabeliste          (Chromecast, AirPlay, TV-Apps, Browser)
//   /stream/<token>/seg<n>.ts   HLS-Segment
//   /stream/<token>/live.ts     fortlaufender MPEG-TS-Strom  (DLNA)
//   /                           Empfänger-Seite für jeden Browser (Smart-TV, Tablet, zweiter PC)
//   /api/…                      Schnittstelle der Lumen-TV-Apps (siehe docs/tv-protocol.md)
//
// Der Strom ist nur über das zufällige <token> erreichbar, das Lumen dem gewählten Empfänger
// mitteilt. Eine TV-App meldet sich an (hello) und fragt dann laufend nach Befehlen (poll);
// gesendet wird ihr erst etwas, wenn der Nutzer sie in Lumen als Ziel auswählt.
class CastServer : public QObject
{
    Q_OBJECT

public:
    explicit CastServer(CastStream *stream, QObject *parent = nullptr);
    ~CastServer() override;

    static constexpr quint16 kDefaultPort = 47800;
    static constexpr int kApiVersion = 1;

    bool listen();
    void close();
    bool isListening() const { return m_server.isListening(); }
    quint16 port() const { return m_server.serverPort(); }

    // Neuer Sendestrom: neues Token, alte Verbindungen zum Strom enden
    void newSession();
    void endSession();
    QString token() const { return m_token; }
    // Adresse dieses Rechners, wie sie der Empfänger (peer) erreicht
    QString baseUrl(const QHostAddress &peer = {}) const;
    QString hlsUrl(const QHostAddress &peer = {}) const { return baseUrl(peer) + streamPath() + QStringLiteral("live.m3u8"); }
    QString tsUrl(const QHostAddress &peer = {}) const { return baseUrl(peer) + streamPath() + QStringLiteral("live.ts"); }
    // alle eigenen IPv4-Adressen (Anzeige: „Am Fernseher eingeben“)
    static QStringList localAddresses();

    // --- TV-Apps ---
    // [{id, name, platform, address}]
    QVariantList clients() const;
    bool hasClient(const QString &id) const { return m_clients.contains(id); }
    void sendCommand(const QString &clientId, const QJsonObject &command);
    // Zustand, den die App zuletzt gemeldet hat ("idle", "playing", "buffering", "error")
    QString clientState(const QString &clientId) const;
    // nur von dieser App werden Fernbedienungstasten angenommen
    void setActiveClient(const QString &clientId) { m_activeClient = clientId; }

    // Anzahl laufender Abrufe des Stroms (Segmente/fortlaufend) seit newSession()
    int streamRequests() const { return m_streamRequests; }

signals:
    void clientsChanged();
    void clientStateChanged(const QString &clientId, const QString &state);
    // Fernbedienung der aktiven TV-App: up, down, left, right, enter, back, menu, playpause, play,
    // pause, stop, rewind, forward, next, prev
    void keyPressed(const QString &key);
    void streamRequested();

private:
    struct Request
    {
        QByteArray method;
        QByteArray path;
        QHash<QByteArray, QByteArray> query;
        QHash<QByteArray, QByteArray> headers;
        QByteArray body;
    };
    struct Connection
    {
        QByteArray buffer;
        bool live = false;          // fortlaufender Strom
        CastStream::Cursor cursor;
        QString pollClient;         // wartet auf einen Befehl für diese App
        qint64 pollSince = 0;
    };
    struct Client
    {
        QString name;
        QString platform;
        QString address;
        QString state;
        qint64 lastSeen = 0;
        QList<QJsonObject> queue;
    };

    void onConnection();
    void onReadyRead(QTcpSocket *socket);
    void handle(QTcpSocket *socket, const Request &req);
    void handleApi(QTcpSocket *socket, const Request &req);
    void handleStream(QTcpSocket *socket, const Request &req, const QByteArray &name);
    void respond(QTcpSocket *socket, int status, const QByteArray &type, const QByteArray &body,
                 const QByteArray &extraHeaders = {}, bool head = false);
    void respondJson(QTcpSocket *socket, const QJsonObject &object, int status = 200);
    void pushLive();
    void housekeeping();
    void answerPoll(QTcpSocket *socket, Client &client);
    QString streamPath() const { return QStringLiteral("/stream/") + m_token + QLatin1Char('/'); }

    CastStream *m_stream;
    QTcpServer m_server;
    QHash<QTcpSocket *, Connection> m_connections;
    QHash<QString, Client> m_clients;
    QString m_activeClient;
    QString m_token;
    QTimer m_timer;
    int m_streamRequests = 0;
};
