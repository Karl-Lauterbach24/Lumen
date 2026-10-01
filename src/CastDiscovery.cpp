#include "CastDiscovery.h"

#include <QDateTime>
#include <QNetworkInterface>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QSettings>
#include <QXmlStreamReader>
#include <QtEndian>

namespace {

const QHostAddress kSsdpGroup(QStringLiteral("239.255.255.250"));
const quint16 kSsdpPort = 1900;
const QHostAddress kMdnsGroup(QStringLiteral("224.0.0.251"));
const quint16 kMdnsPort = 5353;
const char kRenderer[] = "urn:schemas-upnp-org:device:MediaRenderer:1";
const char kGooglecast[] = "_googlecast._tcp.local";
const char kAirplay[] = "_airplay._tcp.local";
const int kExpireSeconds = 75;

QList<QNetworkInterface> multicastInterfaces()
{
    QList<QNetworkInterface> out;
    for (const QNetworkInterface &nif : QNetworkInterface::allInterfaces()) {
        const auto f = nif.flags();
        if (!(f & QNetworkInterface::IsUp) || !(f & QNetworkInterface::IsRunning) || !(f & QNetworkInterface::CanMulticast))
            continue;
        for (const QNetworkAddressEntry &e : nif.addressEntries()) {
            if (e.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                out << nif;
                break;
            }
        }
    }
    return out;
}

QByteArray dnsName(const char *name)
{
    QByteArray out;
    for (const QByteArray &label : QByteArray(name).split('.')) {
        out += char(label.size());
        out += label;
    }
    out += '\0';
    return out;
}

// DNS-Name ab pos lesen (mit Kompressionszeigern); pos steht danach hinter dem Namen
QString readName(const QByteArray &d, int &pos)
{
    QStringList labels;
    int p = pos;
    bool jumped = false;
    for (int guard = 0; guard < 64 && p < d.size(); ++guard) {
        const quint8 len = quint8(d[p]);
        if (len == 0) {
            ++p;
            break;
        }
        if ((len & 0xC0) == 0xC0) {
            if (p + 1 >= d.size())
                break;
            const int target = ((len & 0x3F) << 8) | quint8(d[p + 1]);
            if (!jumped)
                pos = p + 2;
            jumped = true;
            p = target;
            continue;
        }
        if (p + 1 + len > d.size())
            break;
        labels << QString::fromUtf8(d.constData() + p + 1, len);
        p += 1 + len;
    }
    if (!jumped)
        pos = p;
    return labels.join(QLatin1Char('.'));
}

qint64 nowSeconds()
{
    return QDateTime::currentSecsSinceEpoch();
}

} // namespace

CastDiscovery::CastDiscovery(QObject *parent)
    : QObject(parent)
{
    m_ssdp.bind(QHostAddress::AnyIPv4, 0);
    m_mdns.bind(QHostAddress::AnyIPv4, 0);
    connect(&m_ssdp, &QUdpSocket::readyRead, this, &CastDiscovery::onSsdp);
    connect(&m_mdns, &QUdpSocket::readyRead, this, &CastDiscovery::onMdns);
    m_net.setProxy(QNetworkProxy::NoProxy); // Geräte im lokalen Netz
    m_expire.setSingleShot(true);
    m_expire.setInterval(6000);
    connect(&m_expire, &QTimer::timeout, this, &CastDiscovery::expire);

    QStringList manual = QSettings().value(QStringLiteral("cast/manual")).toStringList();
    // Entwickler/Tests: LUMEN_CAST_DEVICES="dlna:http://…,chromecast:host:port"
    for (const QString &spec : qEnvironmentVariable("LUMEN_CAST_DEVICES").split(QLatin1Char(','), Qt::SkipEmptyParts))
        addManual(spec.trimmed(), false);
    for (const QString &spec : std::as_const(manual))
        addManual(spec, false);
}

void CastDiscovery::scan()
{
    sendSsdp();
    sendMdns();
    // zweiter Ruf kurz danach: UDP darf verloren gehen
    QTimer::singleShot(900, this, [this] {
        sendSsdp();
        sendMdns();
    });
    m_expire.start();
}

void CastDiscovery::sendSsdp()
{
    const QByteArray msg = QByteArray("M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\nST: ")
                           + kRenderer + "\r\nUSER-AGENT: Lumen/" LUMEN_VERSION " UPnP/1.1\r\n\r\n";
    for (const QNetworkInterface &nif : multicastInterfaces()) {
        m_ssdp.setMulticastInterface(nif);
        m_ssdp.writeDatagram(msg, kSsdpGroup, kSsdpPort);
    }
}

void CastDiscovery::sendMdns()
{
    // Kopf: ID 0, Standardanfrage, 2 Fragen; Klasse IN mit gesetztem Bit „Antwort direkt an den Absender“
    QByteArray q(12, '\0');
    q[5] = 2;
    for (const char *service : {kGooglecast, kAirplay}) {
        q += dnsName(service);
        q += QByteArray::fromHex("000c8001"); // PTR, IN + QU
    }
    for (const QNetworkInterface &nif : multicastInterfaces()) {
        m_mdns.setMulticastInterface(nif);
        m_mdns.writeDatagram(q, kMdnsGroup, kMdnsPort);
    }
}

void CastDiscovery::onSsdp()
{
    while (m_ssdp.hasPendingDatagrams()) {
        QByteArray data(int(m_ssdp.pendingDatagramSize()), Qt::Uninitialized);
        m_ssdp.readDatagram(data.data(), data.size());
        handleSsdp(data);
    }
}

void CastDiscovery::handleSsdp(const QByteArray &data)
{
    QUrl location;
    bool renderer = false;
    for (const QByteArray &line : data.split('\n')) {
        const int colon = line.indexOf(':');
        if (colon < 0)
            continue;
        const QByteArray key = line.left(colon).trimmed().toLower();
        const QByteArray value = line.mid(colon + 1).trimmed();
        if (key == "location")
            location = QUrl(QString::fromUtf8(value));
        else if (key == "st")
            renderer = value.contains("MediaRenderer");
    }
    if (renderer && location.isValid())
        fetchDescription(location, false);
}

void CastDiscovery::fetchDescription(const QUrl &location, bool manual)
{
    // schon bekannt: nur das Lebenszeichen auffrischen
    for (auto it = m_devices.begin(); it != m_devices.end(); ++it) {
        if (it->type == QLatin1String("dlna") && it->controlUrl.host() == location.host() && it->controlUrl.port() == location.port()) {
            it->lastSeen = nowSeconds();
            return;
        }
    }
    if (m_pendingDescriptions.contains(location))
        return;
    m_pendingDescriptions.insert(location);
    QNetworkRequest req(location);
    req.setTransferTimeout(5000);
    QNetworkReply *reply = m_net.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, location, manual] {
        reply->deleteLater();
        m_pendingDescriptions.remove(location);
        if (reply->error() != QNetworkReply::NoError)
            return;
        QXmlStreamReader xml(reply->readAll());
        CastDevice dev;
        QString base, serviceType, controlUrl, currentType;
        bool isRenderer = false;
        while (!xml.atEnd()) {
            if (xml.readNext() != QXmlStreamReader::StartElement)
                continue;
            const QStringView n = xml.name();
            if (n == QLatin1String("URLBase")) {
                base = xml.readElementText();
            } else if (n == QLatin1String("deviceType")) {
                isRenderer |= xml.readElementText().contains(QLatin1String("MediaRenderer"));
            } else if (n == QLatin1String("friendlyName") && dev.name.isEmpty()) {
                dev.name = xml.readElementText();
            } else if (n == QLatin1String("modelName") && dev.model.isEmpty()) {
                dev.model = xml.readElementText();
            } else if (n == QLatin1String("UDN") && dev.id.isEmpty()) {
                dev.id = QStringLiteral("dlna:") + xml.readElementText();
            } else if (n == QLatin1String("serviceType")) {
                currentType = xml.readElementText();
            } else if (n == QLatin1String("controlURL")) {
                const QString url = xml.readElementText();
                if (currentType.contains(QLatin1String(":AVTransport:")) && controlUrl.isEmpty()) {
                    serviceType = currentType;
                    controlUrl = url;
                }
            }
        }
        if (controlUrl.isEmpty() || (!isRenderer && !manual))
            return;
        const QUrl root = base.isEmpty() ? location : QUrl(base);
        dev.type = QStringLiteral("dlna");
        dev.controlUrl = root.resolved(QUrl(controlUrl));
        dev.serviceType = serviceType;
        dev.address = QHostAddress(location.host());
        dev.port = quint16(location.port(80));
        dev.manual = manual;
        if (dev.id.isEmpty())
            dev.id = QStringLiteral("dlna:") + location.toString();
        if (dev.name.isEmpty())
            dev.name = location.host();
        put(dev);
    });
}

void CastDiscovery::onMdns()
{
    while (m_mdns.hasPendingDatagrams()) {
        QByteArray d(int(m_mdns.pendingDatagramSize()), Qt::Uninitialized);
        m_mdns.readDatagram(d.data(), d.size());
        handleMdns(d);
    }
}

void CastDiscovery::handleMdns(const QByteArray &d)
{
    if (d.size() < 12)
        return;
    const int questions = qFromBigEndian<quint16>(d.constData() + 4);
    const int records = qFromBigEndian<quint16>(d.constData() + 6) + qFromBigEndian<quint16>(d.constData() + 8)
                        + qFromBigEndian<quint16>(d.constData() + 10);
    int pos = 12;
    for (int i = 0; i < questions && pos < d.size(); ++i) {
        readName(d, pos);
        pos += 4;
    }
    for (int i = 0; i < records && pos + 10 <= d.size(); ++i) {
        const QString name = readName(d, pos).toLower();
        if (pos + 10 > d.size())
            break;
        const quint16 type = qFromBigEndian<quint16>(d.constData() + pos);
        const quint16 length = qFromBigEndian<quint16>(d.constData() + pos + 8);
        pos += 10;
        if (pos + length > d.size())
            break;
        int r = pos;
        if (type == 12) { // PTR: Dienst -> Instanz
            const QString instance = readName(d, r);
            if (name == QLatin1String(kGooglecast) || name == QLatin1String(kAirplay)) {
                m_ptr.insert(instance.toLower(), name);
                m_label.insert(instance.toLower(), instance); // Anzeigename in Originalschreibweise
            }
        } else if (type == 33 && length >= 7) { // SRV
            Srv srv;
            srv.port = qFromBigEndian<quint16>(d.constData() + pos + 4);
            r = pos + 6;
            srv.target = readName(d, r).toLower();
            m_srv.insert(name, srv);
        } else if (type == 16) { // TXT
            QHash<QString, QString> kv;
            while (r < pos + length) {
                const int n = quint8(d[r]);
                const QString entry = QString::fromUtf8(d.constData() + r + 1, qMin(n, pos + length - r - 1));
                const int eq = entry.indexOf(QLatin1Char('='));
                if (eq > 0)
                    kv.insert(entry.left(eq).toLower(), entry.mid(eq + 1));
                r += 1 + n;
            }
            m_txt.insert(name, kv);
        } else if (type == 1 && length == 4) { // A
            m_a.insert(name, QHostAddress(qFromBigEndian<quint32>(d.constData() + pos)));
        }
        pos += length;
    }
    resolveMdns();
}

void CastDiscovery::resolveMdns()
{
    for (auto it = m_ptr.cbegin(); it != m_ptr.cend(); ++it) {
        const QString instance = it.key();
        if (!m_srv.contains(instance))
            continue;
        const Srv srv = m_srv.value(instance);
        const QHostAddress address = m_a.value(srv.target);
        if (address.isNull())
            continue;
        const QHash<QString, QString> txt = m_txt.value(instance);
        const QString label = m_label.value(instance, instance).section(QLatin1String("._"), 0, 0);
        CastDevice dev;
        dev.address = address;
        dev.port = srv.port;
        if (it.value() == QLatin1String(kGooglecast)) {
            dev.type = QStringLiteral("chromecast");
            dev.name = txt.value(QStringLiteral("fn"), label);
            dev.model = txt.value(QStringLiteral("md"));
            dev.id = QStringLiteral("chromecast:") + txt.value(QStringLiteral("id"), instance);
        } else {
            dev.type = QStringLiteral("airplay");
            dev.name = label;
            dev.model = txt.value(QStringLiteral("model"));
            dev.id = QStringLiteral("airplay:") + txt.value(QStringLiteral("deviceid"), instance);
            // Statusbits: 0x8 PIN, 0x80 Kennwort, 0x200 einmalige Kopplung nötig
            bool ok = false;
            const quint64 flags = txt.value(QStringLiteral("flags")).toULongLong(&ok, 16);
            const QString pw = txt.value(QStringLiteral("pw")).toLower();
            dev.needsPairing = pw == QLatin1String("1") || pw == QLatin1String("true") || (ok && (flags & 0x288));
        }
        put(dev);
    }
}

void CastDiscovery::put(const CastDevice &device)
{
    CastDevice dev = device;
    dev.lastSeen = nowSeconds();
    const auto it = m_devices.constFind(dev.id);
    const bool changed = it == m_devices.cend() || it->name != dev.name || it->address != dev.address || it->port != dev.port
                         || it->controlUrl != dev.controlUrl;
    if (it != m_devices.cend())
        dev.manual |= it->manual;
    m_devices.insert(dev.id, dev);
    if (changed)
        emit devicesChanged();
}

void CastDiscovery::expire()
{
    const qint64 limit = nowSeconds() - kExpireSeconds;
    bool changed = false;
    for (auto it = m_devices.begin(); it != m_devices.end();) {
        if (!it->manual && it->lastSeen < limit) {
            it = m_devices.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    if (changed)
        emit devicesChanged();
}

bool CastDiscovery::addManual(const QString &spec, bool save)
{
    const int colon = spec.indexOf(QLatin1Char(':'));
    if (colon <= 0)
        return false;
    const QString type = spec.left(colon).toLower();
    const QString rest = spec.mid(colon + 1).trimmed();
    if (type == QLatin1String("dlna")) {
        const QUrl url(rest);
        if (!url.isValid() || url.host().isEmpty())
            return false;
        fetchDescription(url, true);
    } else if (type == QLatin1String("chromecast") || type == QLatin1String("airplay")) {
        const QUrl url(QStringLiteral("x://") + rest);
        if (url.host().isEmpty())
            return false;
        CastDevice dev;
        dev.type = type;
        dev.id = type + QLatin1Char(':') + rest;
        dev.name = url.host();
        dev.address = QHostAddress(url.host());
        dev.port = quint16(url.port(type == QLatin1String("chromecast") ? 8009 : 7000));
        dev.manual = true;
        if (dev.address.isNull())
            return false; // nur IP-Adressen
        put(dev);
    } else {
        return false;
    }
    if (save) {
        QSettings s;
        QStringList list = s.value(QStringLiteral("cast/manual")).toStringList();
        if (!list.contains(spec)) {
            list << spec;
            s.setValue(QStringLiteral("cast/manual"), list);
        }
    }
    return true;
}

void CastDiscovery::removeManual(const QString &id)
{
    const CastDevice dev = m_devices.value(id);
    if (!dev.manual)
        return;
    QSettings s;
    QStringList list = s.value(QStringLiteral("cast/manual")).toStringList();
    // Eintrag am Typ und an der Adresse wiedererkennen
    list.removeIf([&](const QString &spec) {
        return spec.startsWith(dev.type + QLatin1Char(':')) && spec.contains(dev.address.toString());
    });
    s.setValue(QStringLiteral("cast/manual"), list);
    m_devices.remove(id);
    emit devicesChanged();
}
