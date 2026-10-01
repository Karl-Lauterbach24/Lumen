#include "CastServer.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QHostInfo>
#include <QJsonDocument>
#include <QNetworkInterface>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <QUrl>
#include <QUuid>

namespace {

const int kPollSeconds = 25;      // so lange wartet eine Abfrage höchstens auf einen Befehl
const int kClientTimeout = 60;    // ohne Lebenszeichen gilt eine TV-App als weg
const qint64 kMaxBacklog = 12 * 1024 * 1024; // fortlaufender Empfänger liest zu langsam

const char kDlnaFeatures[] = "DLNA.ORG_OP=00;DLNA.ORG_CI=1;DLNA.ORG_FLAGS=01700000000000000000000000000000";

qint64 nowSeconds()
{
    return QDateTime::currentSecsSinceEpoch();
}

QByteArray statusText(int status)
{
    switch (status) {
    case 200: return "OK";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 503: return "Service Unavailable";
    default: return "Error";
    }
}

QByteArray contentType(const QString &file)
{
    if (file.endsWith(QLatin1String(".html"))) return "text/html; charset=utf-8";
    if (file.endsWith(QLatin1String(".js"))) return "text/javascript; charset=utf-8";
    if (file.endsWith(QLatin1String(".css"))) return "text/css; charset=utf-8";
    if (file.endsWith(QLatin1String(".png"))) return "image/png";
    if (file.endsWith(QLatin1String(".svg"))) return "image/svg+xml";
    if (file.endsWith(QLatin1String(".json"))) return "application/json";
    return "application/octet-stream";
}

} // namespace

CastServer::CastServer(CastStream *stream, QObject *parent)
    : QObject(parent)
    , m_stream(stream)
{
    connect(&m_server, &QTcpServer::newConnection, this, &CastServer::onConnection);
    // fortlaufende Empfänger bedienen, wartende Abfragen beantworten, verschwundene Apps entfernen
    m_timer.setInterval(10);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        pushLive();
        static int tick = 0;
        if (++tick % 100 == 0)
            housekeeping();
    });
}

CastServer::~CastServer()
{
    close();
}

bool CastServer::listen()
{
    if (m_server.isListening())
        return true;
    // fester Port, damit TV-Apps Lumen im Netz finden; belegt -> die nächsten probieren
    for (quint16 port = kDefaultPort; port < kDefaultPort + 10; ++port) {
        if (m_server.listen(QHostAddress::Any, port)) {
            m_timer.start();
            return true;
        }
    }
    return false;
}

void CastServer::close()
{
    m_timer.stop();
    m_server.close();
    const auto sockets = m_connections.keys();
    m_connections.clear();
    for (QTcpSocket *s : sockets) {
        s->disconnect(this);
        s->abort();
        s->deleteLater();
    }
    if (!m_clients.isEmpty()) {
        m_clients.clear();
        emit clientsChanged();
    }
}

void CastServer::newSession()
{
    endSession();
    m_token = QString::number(QRandomGenerator::system()->generate64(), 16).rightJustified(16, QLatin1Char('0'));
    m_streamRequests = 0;
}

void CastServer::endSession()
{
    m_token.clear();
    for (auto it = m_connections.begin(); it != m_connections.end(); ++it) {
        if (it->live)
            it.key()->disconnectFromHost();
    }
}

QStringList CastServer::localAddresses()
{
    QStringList out;
    for (const QNetworkInterface &nif : QNetworkInterface::allInterfaces()) {
        const auto flags = nif.flags();
        if (!(flags & QNetworkInterface::IsUp) || !(flags & QNetworkInterface::IsRunning) || (flags & QNetworkInterface::IsLoopBack))
            continue;
        for (const QNetworkAddressEntry &e : nif.addressEntries()) {
            if (e.ip().protocol() == QAbstractSocket::IPv4Protocol && !e.ip().isLinkLocal())
                out << e.ip().toString();
        }
    }
    return out;
}

QString CastServer::baseUrl(const QHostAddress &peer) const
{
    QString host;
    if (peer.isLoopback()) {
        host = QStringLiteral("127.0.0.1");
    } else {
        // die eigene Adresse im Netz des Empfängers
        for (const QNetworkInterface &nif : QNetworkInterface::allInterfaces()) {
            if (!(nif.flags() & QNetworkInterface::IsUp))
                continue;
            for (const QNetworkAddressEntry &e : nif.addressEntries()) {
                if (e.ip().protocol() == QAbstractSocket::IPv4Protocol && !peer.isNull() && e.prefixLength() > 0
                    && peer.isInSubnet(e.ip(), e.prefixLength()))
                    host = e.ip().toString();
            }
        }
        if (host.isEmpty())
            host = localAddresses().value(0, QStringLiteral("127.0.0.1"));
    }
    return QStringLiteral("http://%1:%2").arg(host).arg(port());
}

// --------------------------------------------------------------------------
// TV-Apps
// --------------------------------------------------------------------------

QVariantList CastServer::clients() const
{
    QVariantList out;
    for (auto it = m_clients.cbegin(); it != m_clients.cend(); ++it)
        out << QVariantMap{{"id", it.key()}, {"name", it->name}, {"platform", it->platform}, {"address", it->address}};
    return out;
}

QString CastServer::clientState(const QString &clientId) const
{
    return m_clients.value(clientId).state;
}

void CastServer::sendCommand(const QString &clientId, const QJsonObject &command)
{
    auto it = m_clients.find(clientId);
    if (it == m_clients.end())
        return;
    it->queue.append(command);
    // wartet gerade eine Abfrage dieser App? Dann sofort antworten
    for (auto c = m_connections.begin(); c != m_connections.end(); ++c) {
        if (c->pollClient == clientId) {
            c->pollClient.clear();
            answerPoll(c.key(), *it);
            return;
        }
    }
}

void CastServer::answerPoll(QTcpSocket *socket, Client &client)
{
    QJsonObject cmd{{"cmd", "none"}};
    if (!client.queue.isEmpty())
        cmd = client.queue.takeFirst();
    respondJson(socket, cmd);
}

void CastServer::housekeeping()
{
    const qint64 now = nowSeconds();
    for (auto c = m_connections.begin(); c != m_connections.end(); ++c) {
        if (!c->pollClient.isEmpty() && now - c->pollSince >= kPollSeconds) {
            const QString id = std::exchange(c->pollClient, QString());
            auto it = m_clients.find(id);
            if (it != m_clients.end()) {
                it->lastSeen = now;
                answerPoll(c.key(), *it);
            }
        }
    }
    bool changed = false;
    for (auto it = m_clients.begin(); it != m_clients.end();) {
        bool waiting = false;
        for (const Connection &c : std::as_const(m_connections))
            waiting |= c.pollClient == it.key();
        if (!waiting && now - it->lastSeen > kClientTimeout) {
            it = m_clients.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    if (changed)
        emit clientsChanged();
}

// --------------------------------------------------------------------------
// HTTP
// --------------------------------------------------------------------------

void CastServer::onConnection()
{
    while (QTcpSocket *socket = m_server.nextPendingConnection()) {
        m_connections.insert(socket, Connection{});
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { onReadyRead(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            m_connections.remove(socket);
            socket->deleteLater();
        });
    }
}

void CastServer::onReadyRead(QTcpSocket *socket)
{
    auto it = m_connections.find(socket);
    if (it == m_connections.end())
        return;
    it->buffer += socket->readAll();
    for (;;) {
        it = m_connections.find(socket);
        if (it == m_connections.end() || it->live)
            return;
        const int headEnd = it->buffer.indexOf("\r\n\r\n");
        if (headEnd < 0) {
            if (it->buffer.size() > 64 * 1024)
                socket->abort();
            return;
        }
        const QList<QByteArray> lines = it->buffer.left(headEnd).split('\n');
        const QList<QByteArray> first = lines.value(0).trimmed().split(' ');
        Request req;
        req.method = first.value(0).toUpper();
        const QByteArray target = first.value(1);
        for (int i = 1; i < lines.size(); ++i) {
            const int colon = lines[i].indexOf(':');
            if (colon > 0)
                req.headers.insert(lines[i].left(colon).trimmed().toLower(), lines[i].mid(colon + 1).trimmed());
        }
        const int length = qBound(0, req.headers.value("content-length").toInt(), 1024 * 1024);
        if (it->buffer.size() < headEnd + 4 + length)
            return;
        req.body = it->buffer.mid(headEnd + 4, length);
        it->buffer.remove(0, headEnd + 4 + length);

        const int q = target.indexOf('?');
        req.path = QByteArray::fromPercentEncoding(q < 0 ? target : target.left(q));
        if (q >= 0) {
            for (const QByteArray &pair : target.mid(q + 1).split('&')) {
                const int eq = pair.indexOf('=');
                const QByteArray key = eq < 0 ? pair : pair.left(eq);
                QByteArray value = eq < 0 ? QByteArray() : pair.mid(eq + 1);
                req.query.insert(QByteArray::fromPercentEncoding(key), QByteArray::fromPercentEncoding(value.replace('+', ' ')));
            }
        }
        handle(socket, req);
    }
}

void CastServer::respond(QTcpSocket *socket, int status, const QByteArray &type, const QByteArray &body,
                         const QByteArray &extraHeaders, bool head)
{
    QByteArray out = "HTTP/1.1 " + QByteArray::number(status) + ' ' + statusText(status) + "\r\n";
    out += "Server: Lumen/" LUMEN_VERSION "\r\n";
    out += "Access-Control-Allow-Origin: *\r\n";
    out += "Access-Control-Allow-Headers: Content-Type, Range\r\n";
    out += "Access-Control-Allow-Methods: GET, POST, HEAD, OPTIONS\r\n";
    if (!type.isEmpty())
        out += "Content-Type: " + type + "\r\n";
    out += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    out += extraHeaders;
    out += "\r\n";
    if (!head)
        out += body;
    socket->write(out);
}

void CastServer::respondJson(QTcpSocket *socket, const QJsonObject &object, int status)
{
    respond(socket, status, "application/json", QJsonDocument(object).toJson(QJsonDocument::Compact), "Cache-Control: no-store\r\n");
}

void CastServer::handle(QTcpSocket *socket, const Request &req)
{
    const bool head = req.method == "HEAD";
    if (req.method == "OPTIONS") {
        respond(socket, 204, {}, {});
        return;
    }
    if (req.path.startsWith("/api/")) {
        handleApi(socket, req);
        return;
    }
    if (req.method != "GET" && !head) {
        respond(socket, 405, "text/plain", "method not allowed");
        return;
    }
    if (req.path.startsWith("/stream/")) {
        const QList<QByteArray> parts = req.path.split('/'); // "", "stream", token, name
        if (m_token.isEmpty() || parts.size() != 4 || parts[2] != m_token.toLatin1()) {
            respond(socket, 404, "text/plain", "no such stream");
            return;
        }
        handleStream(socket, req, parts[3]);
        return;
    }
    // Empfänger-Seite (Ressourcen unter :/receiver); nur einfache Dateinamen
    QString name = QString::fromUtf8(req.path.mid(1));
    if (name.isEmpty())
        name = QStringLiteral("index.html");
    if (name == QLatin1String("platform.js")) {
        // gehört nur zu den verpackten TV-Apps; im Browser leer
        respond(socket, 200, contentType(name), "", {}, head);
        return;
    }
    QFile file(QStringLiteral(":/receiver/") + name);
    if (name.contains(QLatin1Char('/')) || name.contains(QLatin1String("..")) || !file.open(QIODevice::ReadOnly)) {
        respond(socket, 404, "text/plain", "not found");
        return;
    }
    respond(socket, 200, contentType(name), file.readAll(), "Cache-Control: no-cache\r\n", head);
}

void CastServer::handleStream(QTcpSocket *socket, const Request &req, const QByteArray &name)
{
    const bool head = req.method == "HEAD";
    if (name == "live.m3u8") {
        if (!head)
            ++m_streamRequests;
        respond(socket, 200, "application/vnd.apple.mpegurl", m_stream->playlist(QStringLiteral("seg")),
                "Cache-Control: no-cache, no-store\r\n", head);
        emit streamRequested();
        return;
    }
    if (name.startsWith("seg") && name.endsWith(".ts")) {
        const QByteArray data = m_stream->segment(name.mid(3, name.size() - 6).toLongLong());
        if (data.isEmpty()) {
            respond(socket, 404, "text/plain", "segment gone");
            return;
        }
        respond(socket, 200, "video/mp2t", data, "Cache-Control: max-age=30\r\n", head);
        return;
    }
    if (name == "live.ts") {
        // Fortlaufend bis zum Ende der Verbindung (ohne Längenangabe); DLNA-Geräte erwarten ihre Kopfzeilen
        QByteArray out = "HTTP/1.1 200 OK\r\nServer: Lumen/" LUMEN_VERSION "\r\n"
                         "Content-Type: video/mpeg\r\nConnection: close\r\nAccept-Ranges: none\r\n"
                         "Access-Control-Allow-Origin: *\r\nCache-Control: no-cache, no-store\r\n"
                         "transferMode.dlna.org: Streaming\r\ncontentFeatures.dlna.org: ";
        out += kDlnaFeatures;
        out += "\r\n\r\n";
        socket->write(out);
        if (head) {
            socket->disconnectFromHost();
            return;
        }
        ++m_streamRequests;
        auto it = m_connections.find(socket);
        if (it != m_connections.end()) {
            it->live = true;
            it->cursor = CastStream::Cursor{};
        }
        emit streamRequested();
        return;
    }
    respond(socket, 404, "text/plain", "not found");
}

void CastServer::pushLive()
{
    for (auto it = m_connections.begin(); it != m_connections.end(); ++it) {
        if (!it->live)
            continue;
        QTcpSocket *socket = it.key();
        if (socket->state() != QAbstractSocket::ConnectedState)
            continue;
        QByteArray data;
        if (m_token.isEmpty() || !m_stream->read(it->cursor, data) || socket->bytesToWrite() > kMaxBacklog) {
            socket->disconnectFromHost();
            continue;
        }
        if (!data.isEmpty())
            socket->write(data);
    }
}

void CastServer::handleApi(QTcpSocket *socket, const Request &req)
{
    const QByteArray name = req.path.mid(5);
    // Angaben stehen in der Adresse oder als JSON im Rumpf
    const QJsonObject body = QJsonDocument::fromJson(req.body).object();
    auto param = [&](const char *key) {
        const QString fromBody = body.value(QLatin1String(key)).toString();
        return fromBody.isEmpty() ? QString::fromUtf8(req.query.value(key)) : fromBody;
    };

    if (name == "info") {
        respondJson(socket, {{"name", "Lumen"}, {"host", QHostInfo::localHostName()}, {"version", LUMEN_VERSION},
                             {"api", kApiVersion}, {"casting", !m_token.isEmpty()}});
        return;
    }
    if (name == "hello") {
        QString id = param("id").left(64);
        if (id.isEmpty())
            id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        Client &c = m_clients[id];
        c.name = param("name").left(80);
        if (c.name.isEmpty())
            c.name = socket->peerAddress().toString();
        c.platform = param("platform").left(40);
        QHostAddress peer = socket->peerAddress();
        bool isV4 = false;
        const quint32 v4 = peer.toIPv4Address(&isV4);
        c.address = isV4 ? QHostAddress(v4).toString() : peer.toString();
        c.lastSeen = nowSeconds();
        c.state = QStringLiteral("idle");
        emit clientsChanged();
        respondJson(socket, {{"id", id}, {"server", QHostInfo::localHostName()}, {"version", LUMEN_VERSION}, {"api", kApiVersion}});
        return;
    }

    const QString id = param("id");
    auto it = m_clients.find(id);
    if (it == m_clients.end()) {
        respondJson(socket, {{"error", "unknown client"}}, 404);
        return;
    }
    it->lastSeen = nowSeconds();
    if (name == "poll") {
        const QString state = param("state");
        if (!state.isEmpty() && state != it->state) {
            it->state = state.left(20);
            emit clientStateChanged(id, it->state);
        }
        if (!it->queue.isEmpty()) {
            answerPoll(socket, *it);
            return;
        }
        // eine ältere, noch wartende Abfrage derselben App beenden
        for (auto c = m_connections.begin(); c != m_connections.end(); ++c) {
            if (c->pollClient == id && c.key() != socket) {
                c->pollClient.clear();
                respondJson(c.key(), {{"cmd", "none"}});
            }
        }
        auto c = m_connections.find(socket);
        if (c != m_connections.end()) {
            c->pollClient = id;
            c->pollSince = nowSeconds();
        }
        return;
    }
    if (name == "key") {
        const QString key = param("key");
        const bool active = id == m_activeClient;
        if (active && !key.isEmpty())
            emit keyPressed(key.left(20));
        respondJson(socket, {{"ok", active}});
        return;
    }
    if (name == "bye") {
        m_clients.erase(it);
        emit clientsChanged();
        respondJson(socket, {{"ok", true}});
        return;
    }
    respondJson(socket, {{"error", "unknown request"}}, 404);
}
