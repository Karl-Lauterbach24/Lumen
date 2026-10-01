#pragma once

#include <QHash>
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QUdpSocket>
#include <QUrl>
#include <QVariantMap>

// Ein Empfänger im lokalen Netz
struct CastDevice
{
    QString id;       // eindeutig, z. B. "dlna:uuid:…"
    QString type;     // dlna | chromecast | airplay | tv
    QString name;
    QString model;
    QHostAddress address;
    quint16 port = 0;
    // DLNA: Steuer-Adresse und Diensttyp von AVTransport
    QUrl controlUrl;
    QString serviceType;
    // AirPlay: Gerät verlangt eine Kopplung/ein Kennwort (aus dem TXT-Eintrag)
    bool needsPairing = false;
    bool manual = false;
    qint64 lastSeen = 0;
};

// Sucht Empfänger:
//   DLNA/UPnP    SSDP (M-SEARCH nach MediaRenderer) + Gerätebeschreibung
//   Chromecast   mDNS  _googlecast._tcp
//   AirPlay      mDNS  _airplay._tcp
// Gesendet wird von einem beliebigen Port; die Antworten kommen direkt dorthin zurück
// (kein Beitritt zu den Multicast-Gruppen nötig, verträgt sich mit Bonjour/Avahi).
// Zusätzlich: von Hand eingetragene Geräte (Netze, die Multicast nicht durchlassen).
class CastDiscovery : public QObject
{
    Q_OBJECT

public:
    explicit CastDiscovery(QObject *parent = nullptr);

    void scan();
    QList<CastDevice> devices() const { return m_devices.values(); }
    CastDevice device(const QString &id) const { return m_devices.value(id); }

    // "dlna:http://host:port/description.xml", "chromecast:host[:port]", "airplay:host[:port]"
    bool addManual(const QString &spec, bool save = true);
    void removeManual(const QString &id);

    // Ein empfangenes Datagramm auswerten (auch für Tests)
    void handleSsdp(const QByteArray &datagram);
    void handleMdns(const QByteArray &datagram);

signals:
    void devicesChanged();

private:
    void sendSsdp();
    void sendMdns();
    void onSsdp();
    void onMdns();
    void fetchDescription(const QUrl &location, bool manual);
    void put(const CastDevice &device);
    void resolveMdns();
    void expire();

    struct Srv
    {
        QString target;
        quint16 port = 0;
    };

    QUdpSocket m_ssdp;
    QUdpSocket m_mdns;
    QNetworkAccessManager m_net;
    QTimer m_expire;
    QHash<QString, CastDevice> m_devices;
    QSet<QUrl> m_pendingDescriptions;
    // mDNS-Einträge, wie sie eintreffen (Namen klein geschrieben)
    QHash<QString, QString> m_ptr;                    // Instanz -> Dienst
    QHash<QString, QString> m_label;                  // Instanz -> Name in Originalschreibweise
    QHash<QString, Srv> m_srv;                        // Instanz -> Ziel
    QHash<QString, QHash<QString, QString>> m_txt;    // Instanz -> Schlüssel/Werte
    QHash<QString, QHostAddress> m_a;                 // Rechnername -> IPv4
};
