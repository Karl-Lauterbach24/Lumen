#pragma once

#include <QNetworkAccessManager>
#include <QUrlQuery>

#include <functional>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

// Medienserver: Jellyfin, Emby und Plex.
//
//   Jellyfin/Emby  Anmeldung mit Benutzer + Passwort (Users/AuthenticateByName);
//                  gespeichert wird nur das Zugriffstoken. Bibliotheken, Ordner,
//                  Serien/Staffeln, Weiterschauen, Suche; Wiedergabe direkt
//                  (Videos/<id>/stream?static=true), Fortschritt an den Server.
//   Plex           Anmeldung über plex.tv (PIN, im Browser des Nutzers) oder
//                  Server-URL + Token; Bibliotheken, Serien/Staffeln, Suche;
//                  Wiedergabe direkt (Datei-Part), Fortschritt (timeline).
//
// Server werden in den Einstellungen gespeichert (Adresse, Name, Nutzer, Token).
class QJsonDocument;

class MediaServers : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList servers READ servers NOTIFY serversChanged)
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
    Q_PROPERTY(QVariantList path READ path NOTIFY itemsChanged)
    Q_PROPERTY(QString current READ current NOTIFY itemsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString plexPinUrl READ plexPinUrl NOTIFY statusChanged)

public:
    explicit MediaServers(QObject *parent = nullptr);

    QVariantList servers() const;
    QVariantList items() const { return m_items; }
    QVariantList path() const { return m_path; }
    QString current() const { return m_current; }
    bool busy() const { return m_busy > 0; }
    QString status() const { return m_status; }
    QString plexPinUrl() const { return m_plexPinUrl; }

    // Jellyfin/Emby: type "jellyfin"/"emby"
    Q_INVOKABLE void addEmbyServer(const QString &type, const QString &url, const QString &user, const QString &password);
    // Plex mit bekanntem Token
    Q_INVOKABLE void addPlexServer(const QString &url, const QString &token);
    // Plex-Anmeldung über plex.tv: öffnet den Browser, fragt die PIN ab und übernimmt die Server
    Q_INVOKABLE void plexSignIn();
    Q_INVOKABLE void cancelPlexSignIn();
    Q_INVOKABLE void removeServer(const QString &id);

    // Navigation: Server-Wurzel (Bibliotheken), Ordner, zurück, Suche, Weiterschauen
    Q_INVOKABLE void openServer(const QString &id);
    Q_INVOKABLE void openItem(const QVariantMap &item);
    Q_INVOKABLE void back();
    Q_INVOKABLE void search(const QString &text);
    Q_INVOKABLE void resume();

    // Abspielbare URL eines Eintrags (leer bei Ordnern)
    Q_INVOKABLE QString streamUrl(const QVariantMap &item) const;
    // Wiedergabe beginnt/endet (Fortschritt an den Server; Sekunden)
    Q_INVOKABLE void reportStart(const QVariantMap &item, double position);
    Q_INVOKABLE void reportProgress(double position, bool paused);
    Q_INVOKABLE void reportStop(double position);

    // Für Tests: plex.tv-Adresse ersetzen
    void setPlexTvBase(const QUrl &url) { m_plexTv = url; }

signals:
    void serversChanged();
    void itemsChanged();
    void busyChanged();
    void statusChanged();
    void serverAdded(const QString &id, bool ok, const QString &message);
    void playRequested(const QString &url, const QString &title, const QVariantMap &item);

private:
    struct Server {
        QString id, type, name, url, user, userId, token;
    };
    struct Level {
        QString title;
        QVariantMap item;   // leer = Server-Wurzel
        QString query;      // Suche/Weiterschauen
    };

    QNetworkRequest request(const Server &s, const QString &path, const QUrlQuery &query = {}) const;
    QString embyAuthHeader(const QString &token = {}) const;
    void load(const Level &level, bool push);
    void get(const Server &s, const QString &path, const QUrlQuery &query, std::function<void(const QJsonDocument &)> done);
    void setItems(const QVariantList &items);
    QVariantMap embyItem(const Server &s, const QVariantMap &raw) const;
    QVariantMap plexItem(const Server &s, const QVariantMap &raw) const;
    void save();
    void setStatus(const QString &s);
    const Server *server(const QString &id) const;
    void addServer(const Server &s);
    void pollPlexPin();
    void adoptPlexResources(const QString &token);
    void busyDelta(int d);

    QNetworkAccessManager m_net;
    QList<Server> m_servers;
    QString m_current;
    QList<Level> m_stack;
    QVariantList m_items;
    QVariantList m_path;
    QString m_status;
    int m_busy = 0;
    QString m_deviceId;
    // laufende Wiedergabe (Fortschritt)
    QString m_playServer;
    QVariantMap m_playItem;
    QString m_playSession;
    // Plex-PIN-Anmeldung
    QUrl m_plexTv = QUrl(QStringLiteral("https://plex.tv"));
    QTimer m_pinTimer;
    QString m_pinId, m_pinCode, m_plexPinUrl;
    int m_pinPolls = 0;
};
