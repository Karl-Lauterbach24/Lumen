#include "CastTargets.h"

#include "CastServer.h"
#include "Tr.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QUuid>
#include <QtEndian>

namespace {

const char kDlnaProtocolInfo[] = "http-get:*:video/mpeg:DLNA.ORG_OP=00;DLNA.ORG_CI=1;DLNA.ORG_FLAGS=01700000000000000000000000000000";

const char kNsConnection[] = "urn:x-cast:com.google.cast.tp.connection";
const char kNsHeartbeat[] = "urn:x-cast:com.google.cast.tp.heartbeat";
const char kNsReceiver[] = "urn:x-cast:com.google.cast.receiver";
const char kNsMedia[] = "urn:x-cast:com.google.cast.media";
const char kDefaultReceiver[] = "CC1AD845"; // Standard-Medienempfänger von Google

QString xmlEscape(const QString &s)
{
    return s.toHtmlEscaped();
}

// --- Protobuf (CastMessage): nur Varint- und Längenfelder ---
void putVarint(QByteArray &out, quint64 v)
{
    while (v >= 0x80) {
        out += char((v & 0x7F) | 0x80);
        v >>= 7;
    }
    out += char(v);
}

void putString(QByteArray &out, int field, const QByteArray &value)
{
    putVarint(out, quint64(field << 3 | 2));
    putVarint(out, quint64(value.size()));
    out += value;
}

bool getVarint(const QByteArray &d, int &pos, quint64 &v)
{
    v = 0;
    for (int shift = 0; pos < d.size() && shift < 64; shift += 7) {
        const quint8 b = quint8(d[pos++]);
        v |= quint64(b & 0x7F) << shift;
        if (!(b & 0x80))
            return true;
    }
    return false;
}

} // namespace

CastTarget *CastTarget::create(const CastDevice &device, CastServer *server, QObject *parent)
{
    if (device.type == QLatin1String("dlna"))
        return new DlnaTarget(device, parent);
    if (device.type == QLatin1String("chromecast"))
        return new ChromecastTarget(device, parent);
    if (device.type == QLatin1String("airplay"))
        return new AirplayTarget(device, parent);
    if (device.type == QLatin1String("tv"))
        return new TvTarget(device, server, parent);
    return nullptr;
}

// --------------------------------------------------------------------------
// DLNA
// --------------------------------------------------------------------------

DlnaTarget::DlnaTarget(const CastDevice &device, QObject *parent)
    : CastTarget(parent)
    , m_device(device)
{
    m_net.setProxy(QNetworkProxy::NoProxy);
}

void DlnaTarget::soap(const QString &action, const QString &arguments, std::function<void(bool, const QString &)> done)
{
    const QString body = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        "<s:Body><u:%1 xmlns:u=\"%2\"><InstanceID>0</InstanceID>%3</u:%1></s:Body></s:Envelope>")
        .arg(action, m_device.serviceType, arguments);
    QNetworkRequest req(m_device.controlUrl);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("text/xml; charset=\"utf-8\""));
    req.setRawHeader("SOAPACTION", '"' + m_device.serviceType.toUtf8() + '#' + action.toUtf8() + '"');
    req.setTransferTimeout(8000);
    QNetworkReply *reply = m_net.post(req, body.toUtf8());
    connect(reply, &QNetworkReply::finished, this, [reply, done] {
        reply->deleteLater();
        const bool ok = reply->error() == QNetworkReply::NoError;
        QString message;
        if (!ok) {
            // UPnP-Fehler stehen im Rumpf (<errorDescription>)
            const QString text = QString::fromUtf8(reply->readAll());
            const int a = text.indexOf(QLatin1String("<errorDescription>"));
            const int b = text.indexOf(QLatin1String("</errorDescription>"));
            message = a >= 0 && b > a ? text.mid(a + 18, b - a - 18) : reply->errorString();
        }
        if (done)
            done(ok, message);
    });
}

void DlnaTarget::start(const QString &, const QString &ts, const QString &title)
{
    emit stateChanged(QStringLiteral("connecting"));
    const QString didl = QStringLiteral(
        "<DIDL-Lite xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
        "xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\"><item id=\"lumen\" parentID=\"-1\" restricted=\"1\">"
        "<dc:title>%1</dc:title><upnp:class>object.item.videoItem</upnp:class>"
        "<res protocolInfo=\"%2\">%3</res></item></DIDL-Lite>")
        .arg(xmlEscape(title.isEmpty() ? QStringLiteral("Lumen") : title), QLatin1String(kDlnaProtocolInfo), xmlEscape(ts));
    const QString args = QStringLiteral("<CurrentURI>%1</CurrentURI><CurrentURIMetaData>%2</CurrentURIMetaData>")
                             .arg(xmlEscape(ts), xmlEscape(didl));
    soap(QStringLiteral("SetAVTransportURI"), args, [this](bool ok, const QString &message) {
        if (!ok) {
            emit stateChanged(QStringLiteral("error"), LTR("Das Gerät nimmt den Strom nicht an: %1").arg(message));
            return;
        }
        soap(QStringLiteral("Play"), QStringLiteral("<Speed>1</Speed>"), [this](bool played, const QString &error) {
            if (played)
                emit stateChanged(QStringLiteral("playing"));
            else
                emit stateChanged(QStringLiteral("error"), LTR("Das Gerät startet die Wiedergabe nicht: %1").arg(error));
        });
    });
}

void DlnaTarget::stop()
{
    soap(QStringLiteral("Stop"), QString(), nullptr);
}

// --------------------------------------------------------------------------
// Chromecast
// --------------------------------------------------------------------------

ChromecastTarget::ChromecastTarget(const CastDevice &device, QObject *parent)
    : CastTarget(parent)
    , m_device(device)
{
    m_socket.setProxy(QNetworkProxy::NoProxy);
    // Geräte weisen sich mit einem eigenen (selbst signierten) Zertifikat aus
    m_socket.setPeerVerifyMode(QSslSocket::VerifyNone);
    connect(&m_socket, &QSslSocket::encrypted, this, [this] {
        send(QLatin1String(kNsConnection), {{"type", "CONNECT"}});
        send(QLatin1String(kNsReceiver), {{"type", "LAUNCH"}, {"appId", kDefaultReceiver}, {"requestId", ++m_request}});
        m_heartbeat.start();
    });
    connect(&m_socket, &QSslSocket::readyRead, this, &ChromecastTarget::onReadyRead);
    connect(&m_socket, &QSslSocket::errorOccurred, this, [this] {
        if (!m_stopping)
            emit stateChanged(QStringLiteral("error"), LTR("Verbindung zum Chromecast: %1").arg(m_socket.errorString()));
    });
    connect(&m_socket, &QSslSocket::disconnected, this, [this] {
        m_heartbeat.stop();
        if (!m_stopping && m_loaded)
            emit stateChanged(QStringLiteral("error"), LTR("Der Chromecast hat die Verbindung beendet."));
    });
    m_heartbeat.setInterval(5000);
    connect(&m_heartbeat, &QTimer::timeout, this, [this] { send(QLatin1String(kNsHeartbeat), {{"type", "PING"}}); });
}

void ChromecastTarget::start(const QString &hls, const QString &, const QString &title)
{
    m_url = hls;
    m_title = title;
    m_loaded = false;
    m_stopping = false;
    emit stateChanged(QStringLiteral("connecting"));
    m_socket.connectToHostEncrypted(m_device.address.toString(), m_device.port ? m_device.port : 8009);
}

void ChromecastTarget::stop()
{
    m_stopping = true;
    if (m_socket.state() == QAbstractSocket::ConnectedState) {
        if (!m_session.isEmpty())
            send(QLatin1String(kNsReceiver), {{"type", "STOP"}, {"sessionId", m_session}, {"requestId", ++m_request}});
        m_socket.flush();
        m_socket.disconnectFromHost();
    } else {
        m_socket.abort();
    }
    m_heartbeat.stop();
}

void ChromecastTarget::send(const QString &ns, const QJsonObject &payload, const QString &destination)
{
    if (!m_socket.isEncrypted())
        return;
    QByteArray msg;
    putVarint(msg, 1 << 3 | 0); // protocol_version = CASTV2_1_0
    putVarint(msg, 0);
    putString(msg, 2, "sender-0");
    putString(msg, 3, destination.toUtf8());
    putString(msg, 4, ns.toUtf8());
    putVarint(msg, 5 << 3 | 0); // payload_type = STRING
    putVarint(msg, 0);
    putString(msg, 6, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    QByteArray frame(4, '\0');
    qToBigEndian<quint32>(quint32(msg.size()), frame.data());
    m_socket.write(frame + msg);
}

void ChromecastTarget::onReadyRead()
{
    m_buffer += m_socket.readAll();
    while (m_buffer.size() >= 4) {
        const quint32 length = qFromBigEndian<quint32>(m_buffer.constData());
        if (length > 1024 * 1024) {
            m_socket.abort();
            return;
        }
        if (quint32(m_buffer.size()) < 4 + length)
            return;
        const QByteArray msg = m_buffer.mid(4, int(length));
        m_buffer.remove(0, 4 + int(length));

        QString source, ns;
        QByteArray payload;
        int pos = 0;
        while (pos < msg.size()) {
            quint64 key = 0, value = 0;
            if (!getVarint(msg, pos, key))
                break;
            const int field = int(key >> 3);
            if ((key & 7) == 0) {
                if (!getVarint(msg, pos, value))
                    break;
            } else if ((key & 7) == 2) {
                if (!getVarint(msg, pos, value) || pos + qint64(value) > msg.size())
                    break;
                const QByteArray bytes = msg.mid(pos, int(value));
                pos += int(value);
                if (field == 2) source = QString::fromUtf8(bytes);
                else if (field == 4) ns = QString::fromUtf8(bytes);
                else if (field == 6) payload = bytes;
            } else {
                break;
            }
        }
        onMessage(source, ns, QJsonDocument::fromJson(payload).object());
    }
}

void ChromecastTarget::onMessage(const QString &source, const QString &ns, const QJsonObject &payload)
{
    const QString type = payload.value("type").toString();
    if (ns == QLatin1String(kNsHeartbeat)) {
        if (type == QLatin1String("PING"))
            send(ns, {{"type", "PONG"}}, source);
        return;
    }
    if (ns == QLatin1String(kNsReceiver)) {
        if (type == QLatin1String("LAUNCH_ERROR")) {
            emit stateChanged(QStringLiteral("error"), LTR("Der Chromecast kann den Medienempfänger nicht starten."));
            return;
        }
        if (type != QLatin1String("RECEIVER_STATUS") || m_loaded)
            return;
        for (const QJsonValue &v : payload.value("status").toObject().value("applications").toArray()) {
            const QJsonObject app = v.toObject();
            if (app.value("appId").toString() != QLatin1String(kDefaultReceiver))
                continue;
            m_transport = app.value("transportId").toString();
            m_session = app.value("sessionId").toString();
            if (m_transport.isEmpty())
                continue;
            m_loaded = true;
            send(QLatin1String(kNsConnection), {{"type", "CONNECT"}}, m_transport);
            const QJsonObject media{
                {"contentId", m_url},
                {"contentType", "application/x-mpegURL"},
                {"streamType", "LIVE"},
                {"hlsSegmentFormat", "ts"},
                {"hlsVideoSegmentFormat", "mpeg2_ts"},
                {"metadata", QJsonObject{{"metadataType", 0}, {"title", m_title.isEmpty() ? QStringLiteral("Lumen") : m_title}}},
            };
            send(QLatin1String(kNsMedia), {{"type", "LOAD"}, {"requestId", ++m_request}, {"autoplay", true}, {"media", media}}, m_transport);
        }
        return;
    }
    if (ns == QLatin1String(kNsMedia)) {
        if (type == QLatin1String("LOAD_FAILED") || type == QLatin1String("LOAD_CANCELLED") || type == QLatin1String("INVALID_REQUEST")) {
            emit stateChanged(QStringLiteral("error"), LTR("Der Chromecast kann den Strom nicht abspielen (%1).").arg(type));
        } else if (type == QLatin1String("MEDIA_STATUS")) {
            const QJsonObject st = payload.value("status").toArray().at(0).toObject();
            const QString player = st.value("playerState").toString();
            if (player == QLatin1String("PLAYING") || player == QLatin1String("BUFFERING"))
                emit stateChanged(QStringLiteral("playing"));
        }
        return;
    }
    if (ns == QLatin1String(kNsConnection) && type == QLatin1String("CLOSE") && source == m_transport && !m_stopping)
        emit stateChanged(QStringLiteral("error"), LTR("Der Chromecast hat die Wiedergabe beendet."));
}

// --------------------------------------------------------------------------
// AirPlay
// --------------------------------------------------------------------------

AirplayTarget::AirplayTarget(const CastDevice &device, QObject *parent)
    : CastTarget(parent)
    , m_device(device)
{
    m_socket.setProxy(QNetworkProxy::NoProxy);
    m_sessionId = QUuid::createUuid().toByteArray(QUuid::WithoutBraces);
    connect(&m_socket, &QTcpSocket::connected, this, [this] {
        request("POST", "/play", "Content-Location: " + m_url.toUtf8() + "\nStart-Position: 0.000000\n");
        m_keepAlive.start();
    });
    connect(&m_socket, &QTcpSocket::readyRead, this, &AirplayTarget::onReadyRead);
    connect(&m_socket, &QTcpSocket::errorOccurred, this, [this] {
        if (m_keepAlive.isActive() || !m_playing)
            emit stateChanged(QStringLiteral("error"), LTR("Verbindung zum AirPlay-Gerät: %1").arg(m_socket.errorString()));
        m_keepAlive.stop();
    });
    m_keepAlive.setInterval(2000);
    connect(&m_keepAlive, &QTimer::timeout, this, [this] { request("GET", "/playback-info"); });
}

void AirplayTarget::start(const QString &hls, const QString &, const QString &)
{
    m_url = hls;
    m_playing = false;
    m_buffer.clear();
    m_pending.clear();
    emit stateChanged(QStringLiteral("connecting"));
    m_socket.connectToHost(m_device.address, m_device.port ? m_device.port : 7000);
}

void AirplayTarget::stop()
{
    m_keepAlive.stop();
    if (m_socket.state() == QAbstractSocket::ConnectedState) {
        request("POST", "/stop");
        m_socket.flush();
        m_socket.disconnectFromHost();
    } else {
        m_socket.abort();
    }
}

void AirplayTarget::request(const QByteArray &method, const QByteArray &path, const QByteArray &body)
{
    if (m_socket.state() != QAbstractSocket::ConnectedState)
        return;
    QByteArray out = method + ' ' + path + " HTTP/1.1\r\nUser-Agent: MediaControl/1.0\r\nX-Apple-Session-ID: " + m_sessionId + "\r\n";
    if (!body.isEmpty())
        out += "Content-Type: text/parameters\r\n";
    out += "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
    m_pending.append(path);
    m_socket.write(out);
}

void AirplayTarget::onReadyRead()
{
    m_buffer += m_socket.readAll();
    for (;;) {
        const int headEnd = m_buffer.indexOf("\r\n\r\n");
        if (headEnd < 0)
            return;
        const QByteArray head = m_buffer.left(headEnd);
        int length = 0;
        for (const QByteArray &line : head.split('\n')) {
            if (line.toLower().startsWith("content-length:"))
                length = line.mid(15).trimmed().toInt();
        }
        if (m_buffer.size() < headEnd + 4 + length)
            return;
        m_buffer.remove(0, headEnd + 4 + length);
        const int status = head.split(' ').value(1).toInt();
        const QByteArray path = m_pending.isEmpty() ? QByteArray() : m_pending.takeFirst();
        if (path != "/play")
            continue;
        if (status == 200) {
            m_playing = true;
            emit stateChanged(QStringLiteral("playing"));
        } else {
            m_keepAlive.stop();
            // neuere Geräte (AirPlay 2) verlangen eine Kopplung, die Lumen nicht beherrscht
            emit stateChanged(QStringLiteral("error"),
                              LTR("Das AirPlay-Gerät lehnt ab (HTTP %1). Geräte, die eine Kopplung oder einen Code verlangen, lassen sich nur über die Bildschirmsynchronisierung des Systems nutzen.").arg(status));
        }
    }
}

// --------------------------------------------------------------------------
// Lumen-TV-App / Browser
// --------------------------------------------------------------------------

TvTarget::TvTarget(const CastDevice &device, CastServer *server, QObject *parent)
    : CastTarget(parent)
    , m_client(device.id.mid(3))
    , m_server(server)
{
    connect(m_server, &CastServer::clientStateChanged, this, [this](const QString &id, const QString &state) {
        if (id != m_client)
            return;
        if (state == QLatin1String("playing") || state == QLatin1String("buffering"))
            emit stateChanged(QStringLiteral("playing"));
        else if (state == QLatin1String("error"))
            emit stateChanged(QStringLiteral("error"), LTR("Die TV-App kann den Strom nicht abspielen."));
    });
    connect(m_server, &CastServer::clientsChanged, this, [this] {
        if (!m_server->hasClient(m_client))
            emit stateChanged(QStringLiteral("error"), LTR("Die TV-App ist nicht mehr verbunden."));
    });
}

void TvTarget::start(const QString &hls, const QString &ts, const QString &title)
{
    emit stateChanged(QStringLiteral("connecting"));
    m_server->setActiveClient(m_client);
    m_server->sendCommand(m_client, {{"cmd", "play"}, {"hls", hls}, {"ts", ts}, {"title", title}});
}

void TvTarget::stop()
{
    m_server->sendCommand(m_client, {{"cmd", "stop"}});
    m_server->setActiveClient(QString());
}
