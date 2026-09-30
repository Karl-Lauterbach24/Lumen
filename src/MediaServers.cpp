#include "MediaServers.h"
#include "Tr.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QHostInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QSettings>
#include <QUrlQuery>
#include <QUuid>

namespace {

QString normalizeUrl(QString url)
{
    url = url.trimmed();
    if (!url.contains(QLatin1String("://")))
        url.prepend(QStringLiteral("http://"));
    while (url.endsWith(QLatin1Char('/')))
        url.chop(1);
    return url;
}

bool isEmbyType(const QString &t)
{
    return t == QLatin1String("jellyfin") || t == QLatin1String("emby");
}

QString plexTicksFree(qint64 ms)
{
    return QString::number(ms);
}

} // namespace

MediaServers::MediaServers(QObject *parent)
    : QObject(parent)
{
    QSettings s;
    m_deviceId = s.value(QStringLiteral("servers/deviceId")).toString();
    if (m_deviceId.isEmpty()) {
        m_deviceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        s.setValue(QStringLiteral("servers/deviceId"), m_deviceId);
    }
    const int n = s.beginReadArray(QStringLiteral("servers/list"));
    for (int i = 0; i < n; ++i) {
        s.setArrayIndex(i);
        m_servers.append({s.value("id").toString(), s.value("type").toString(), s.value("name").toString(), s.value("url").toString(),
                          s.value("user").toString(), s.value("userId").toString(), s.value("token").toString()});
    }
    s.endArray();
    m_pinTimer.setInterval(2000);
    connect(&m_pinTimer, &QTimer::timeout, this, &MediaServers::pollPlexPin);
}

void MediaServers::save()
{
    QSettings s;
    s.remove(QStringLiteral("servers/list"));
    s.beginWriteArray(QStringLiteral("servers/list"), int(m_servers.size()));
    for (int i = 0; i < m_servers.size(); ++i) {
        const Server &v = m_servers.at(i);
        s.setArrayIndex(i);
        s.setValue("id", v.id);
        s.setValue("type", v.type);
        s.setValue("name", v.name);
        s.setValue("url", v.url);
        s.setValue("user", v.user);
        s.setValue("userId", v.userId);
        s.setValue("token", v.token);
    }
    s.endArray();
    emit serversChanged();
}

QVariantList MediaServers::servers() const
{
    QVariantList out;
    for (const Server &s : m_servers)
        out << QVariantMap{{"id", s.id}, {"type", s.type}, {"name", s.name}, {"url", s.url}, {"user", s.user}};
    return out;
}

const MediaServers::Server *MediaServers::server(const QString &id) const
{
    for (const Server &s : m_servers)
        if (s.id == id)
            return &s;
    return nullptr;
}

void MediaServers::setStatus(const QString &s)
{
    m_status = s;
    emit statusChanged();
}

void MediaServers::busyDelta(int d)
{
    m_busy += d;
    emit busyChanged();
}

QString MediaServers::embyAuthHeader(const QString &token) const
{
    QString h = QStringLiteral("MediaBrowser Client=\"Lumen\", Device=\"%1\", DeviceId=\"%2\", Version=\"%3\"")
                    .arg(QHostInfo::localHostName(), m_deviceId, QCoreApplication::applicationVersion());
    if (!token.isEmpty())
        h += QStringLiteral(", Token=\"%1\"").arg(token);
    return h;
}

QNetworkRequest MediaServers::request(const Server &s, const QString &path, const QUrlQuery &query) const
{
    QUrl url(s.url + path);
    if (!query.isEmpty())
        url.setQuery(query);
    QNetworkRequest r(url);
    r.setTransferTimeout(20000);
    r.setRawHeader("Accept", "application/json");
    if (isEmbyType(s.type)) {
        const QByteArray auth = embyAuthHeader(s.token).toUtf8();
        r.setRawHeader("Authorization", auth);
        r.setRawHeader("X-Emby-Authorization", auth);
        if (!s.token.isEmpty())
            r.setRawHeader("X-Emby-Token", s.token.toUtf8());
    } else {
        r.setRawHeader("X-Plex-Token", s.token.toUtf8());
        r.setRawHeader("X-Plex-Client-Identifier", m_deviceId.toUtf8());
        r.setRawHeader("X-Plex-Product", "Lumen");
        r.setRawHeader("X-Plex-Version", QCoreApplication::applicationVersion().toUtf8());
    }
    return r;
}

void MediaServers::get(const Server &s, const QString &path, const QUrlQuery &query, std::function<void(const QJsonDocument &)> done)
{
    busyDelta(1);
    QNetworkReply *reply = m_net.get(request(s, path, query));
    connect(reply, &QNetworkReply::finished, this, [this, reply, done] {
        reply->deleteLater();
        busyDelta(-1);
        if (reply->error() != QNetworkReply::NoError) {
            setStatus(reply->errorString());
            return;
        }
        done(QJsonDocument::fromJson(reply->readAll()));
    });
}

void MediaServers::addServer(const Server &s)
{
    for (Server &v : m_servers) {
        if (v.type == s.type && v.url == s.url && v.userId == s.userId) {
            v = Server{v.id, s.type, s.name, s.url, s.user, s.userId, s.token};
            save();
            emit serverAdded(v.id, true, {});
            return;
        }
    }
    m_servers.append(s);
    save();
    emit serverAdded(s.id, true, {});
}

// ---------------------------------------------------------------- Jellyfin/Emby

void MediaServers::addEmbyServer(const QString &type, const QString &url, const QString &user, const QString &password)
{
    Server s{QUuid::createUuid().toString(QUuid::WithoutBraces), type == QLatin1String("emby") ? QStringLiteral("emby") : QStringLiteral("jellyfin"),
             {}, normalizeUrl(url), user, {}, {}};
    QNetworkRequest r = request(s, QStringLiteral("/Users/AuthenticateByName"));
    r.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    const QByteArray body = QJsonDocument(QJsonObject{{"Username", user}, {"Pw", password}}).toJson(QJsonDocument::Compact);
    busyDelta(1);
    setStatus(LTR("Anmeldung bei %1 …").arg(s.url));
    QNetworkReply *reply = m_net.post(r, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply, s]() mutable {
        reply->deleteLater();
        busyDelta(-1);
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || o.value("AccessToken").toString().isEmpty()) {
            const QString msg = http == 401 ? LTR("Benutzername oder Passwort falsch") : reply->errorString();
            setStatus(msg);
            emit serverAdded({}, false, msg);
            return;
        }
        s.token = o.value("AccessToken").toString();
        s.userId = o.value("User").toObject().value("Id").toString();
        s.name = s.url;
        // Servername (öffentliche Info, ohne Anmeldung)
        busyDelta(1);
        QNetworkReply *info = m_net.get(request(s, QStringLiteral("/System/Info/Public")));
        connect(info, &QNetworkReply::finished, this, [this, info, s]() mutable {
            info->deleteLater();
            busyDelta(-1);
            const QJsonObject i = QJsonDocument::fromJson(info->readAll()).object();
            if (!i.value("ServerName").toString().isEmpty())
                s.name = i.value("ServerName").toString();
            addServer(s);
            setStatus(LTR("Verbunden: %1").arg(s.name));
        });
    });
}

QVariantMap MediaServers::embyItem(const Server &s, const QVariantMap &r) const
{
    const QString type = r.value("Type").toString();
    const bool folder = r.value("IsFolder").toBool() || type == QLatin1String("CollectionFolder") || type == QLatin1String("UserView");
    const QString media = r.value("MediaType").toString();
    QString subtitle;
    if (type == QLatin1String("Episode")) {
        subtitle = QStringLiteral("S%1E%2 · %3").arg(r.value("ParentIndexNumber").toInt()).arg(r.value("IndexNumber").toInt())
                       .arg(r.value("SeriesName").toString());
    } else if (r.contains("ProductionYear")) {
        subtitle = r.value("ProductionYear").toString();
    }
    QString image;
    const QString tag = r.value("ImageTags").toMap().value("Primary").toString();
    if (!tag.isEmpty())
        image = QStringLiteral("%1/Items/%2/Images/Primary?maxHeight=360&tag=%3&api_key=%4").arg(s.url, r.value("Id").toString(), tag, s.token);
    return QVariantMap{
        {"server", s.id}, {"id", r.value("Id")}, {"title", r.value("Name")}, {"subtitle", subtitle},
        {"kind", type}, {"folder", folder}, {"playable", !folder && (media == QLatin1String("Video") || media == QLatin1String("Audio"))},
        {"audio", media == QLatin1String("Audio")}, {"image", image},
        {"duration", r.value("RunTimeTicks").toDouble() / 1e7},
        {"resume", r.value("UserData").toMap().value("PlaybackPositionTicks").toDouble() / 1e7},
        {"overview", r.value("Overview")},
    };
}

// ---------------------------------------------------------------- Plex

void MediaServers::addPlexServer(const QString &url, const QString &token)
{
    Server s{QUuid::createUuid().toString(QUuid::WithoutBraces), QStringLiteral("plex"), {}, normalizeUrl(url), {}, {}, token.trimmed()};
    setStatus(LTR("Verbinde mit %1 …").arg(s.url));
    busyDelta(1);
    QNetworkReply *reply = m_net.get(request(s, QStringLiteral("/identity")));
    connect(reply, &QNetworkReply::finished, this, [this, reply, s]() mutable {
        reply->deleteLater();
        busyDelta(-1);
        if (reply->error() != QNetworkReply::NoError) {
            const QString msg = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 401 ? LTR("Plex-Token ungültig")
                                                                                                           : reply->errorString();
            setStatus(msg);
            emit serverAdded({}, false, msg);
            return;
        }
        // Name aus der Wurzel des Servers (friendlyName)
        busyDelta(1);
        QNetworkReply *root = m_net.get(request(s, QStringLiteral("/")));
        connect(root, &QNetworkReply::finished, this, [this, root, s]() mutable {
            root->deleteLater();
            busyDelta(-1);
            const QJsonObject mc = QJsonDocument::fromJson(root->readAll()).object().value("MediaContainer").toObject();
            s.name = mc.value("friendlyName").toString(s.url);
            if (root->error() == QNetworkReply::NoError) {
                addServer(s);
                setStatus(LTR("Verbunden: %1").arg(s.name));
            } else {
                setStatus(root->errorString());
                emit serverAdded({}, false, root->errorString());
            }
        });
    });
}

void MediaServers::plexSignIn()
{
    QUrl url(m_plexTv);
    url.setPath(QStringLiteral("/api/v2/pins"));
    url.setQuery(QStringLiteral("strong=true"));
    QNetworkRequest r(url);
    r.setRawHeader("Accept", "application/json");
    r.setRawHeader("X-Plex-Product", "Lumen");
    r.setRawHeader("X-Plex-Client-Identifier", m_deviceId.toUtf8());
    busyDelta(1);
    QNetworkReply *reply = m_net.post(r, QByteArray());
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        busyDelta(-1);
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        if (reply->error() != QNetworkReply::NoError || o.value("code").toString().isEmpty()) {
            setStatus(LTR("plex.tv nicht erreichbar: %1").arg(reply->errorString()));
            return;
        }
        m_pinId = QString::number(o.value("id").toVariant().toLongLong());
        m_pinCode = o.value("code").toString();
        // Anmeldung im Browser des Nutzers (Lumen sieht das Passwort nie)
        QUrl auth(QStringLiteral("https://app.plex.tv/auth"));
        auth.setFragment(QStringLiteral("?clientID=%1&code=%2&context%5Bdevice%5D%5Bproduct%5D=Lumen").arg(m_deviceId, m_pinCode),
                         QUrl::StrictMode);
        m_plexPinUrl = auth.toString();
        m_pinPolls = 0;
        m_pinTimer.start();
        setStatus(LTR("Im Browser bei Plex anmelden – Lumen wartet auf die Freigabe …"));
        if (!qEnvironmentVariableIsSet("LUMEN_NO_BROWSER"))
            QDesktopServices::openUrl(auth);
    });
}

void MediaServers::cancelPlexSignIn()
{
    m_pinTimer.stop();
    m_plexPinUrl.clear();
    setStatus({});
}

void MediaServers::pollPlexPin()
{
    if (++m_pinPolls > 150) { // 5 Minuten
        cancelPlexSignIn();
        setStatus(LTR("Plex-Anmeldung abgelaufen"));
        return;
    }
    QUrl url(m_plexTv);
    url.setPath(QStringLiteral("/api/v2/pins/") + m_pinId);
    QNetworkRequest r(url);
    r.setRawHeader("Accept", "application/json");
    r.setRawHeader("X-Plex-Client-Identifier", m_deviceId.toUtf8());
    QNetworkReply *reply = m_net.get(r);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const QString token = QJsonDocument::fromJson(reply->readAll()).object().value("authToken").toString();
        if (token.isEmpty() || !m_pinTimer.isActive())
            return;
        m_pinTimer.stop();
        m_plexPinUrl.clear();
        adoptPlexResources(token);
    });
}

void MediaServers::adoptPlexResources(const QString &token)
{
    QUrl url(m_plexTv);
    url.setPath(QStringLiteral("/api/v2/resources"));
    url.setQuery(QStringLiteral("includeHttps=1&includeRelay=1"));
    QNetworkRequest r(url);
    r.setRawHeader("Accept", "application/json");
    r.setRawHeader("X-Plex-Token", token.toUtf8());
    r.setRawHeader("X-Plex-Client-Identifier", m_deviceId.toUtf8());
    busyDelta(1);
    QNetworkReply *reply = m_net.get(r);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        busyDelta(-1);
        int added = 0;
        for (const QJsonValue &v : QJsonDocument::fromJson(reply->readAll()).array()) {
            const QJsonObject res = v.toObject();
            if (!res.value("provides").toString().contains(QLatin1String("server")))
                continue;
            // lokale direkte Verbindung bevorzugt, dann entfernte, zuletzt Relay
            QString best;
            int bestRank = 99;
            for (const QJsonValue &c : res.value("connections").toArray()) {
                const QJsonObject co = c.toObject();
                const int rank = co.value("relay").toBool() ? 2 : co.value("local").toBool() ? 0 : 1;
                if (rank < bestRank) {
                    bestRank = rank;
                    best = co.value("uri").toString();
                }
            }
            if (best.isEmpty())
                continue;
            addServer({QUuid::createUuid().toString(QUuid::WithoutBraces), QStringLiteral("plex"), res.value("name").toString(),
                       normalizeUrl(best), {}, {}, res.value("accessToken").toString()});
            ++added;
        }
        setStatus(added ? LTR("%1 Plex-Server übernommen").arg(added) : LTR("Kein Plex-Server im Konto gefunden"));
    });
}

QVariantMap MediaServers::plexItem(const Server &s, const QVariantMap &r) const
{
    const QString type = r.value("type").toString();
    static const QStringList playableTypes{"movie", "episode", "track", "clip"};
    const bool playable = playableTypes.contains(type);
    QString subtitle;
    if (type == QLatin1String("episode"))
        subtitle = QStringLiteral("S%1E%2 · %3").arg(r.value("parentIndex").toInt()).arg(r.value("index").toInt()).arg(r.value("grandparentTitle").toString());
    else if (r.contains("year"))
        subtitle = r.value("year").toString();
    const QString thumb = r.value("thumb").toString();
    QString image;
    if (!thumb.isEmpty()) {
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("width"), QStringLiteral("240"));
        q.addQueryItem(QStringLiteral("height"), QStringLiteral("360"));
        q.addQueryItem(QStringLiteral("minSize"), QStringLiteral("1"));
        q.addQueryItem(QStringLiteral("url"), QString::fromLatin1(QUrl::toPercentEncoding(thumb)));
        q.addQueryItem(QStringLiteral("X-Plex-Token"), s.token);
        image = s.url + QStringLiteral("/photo/:/transcode?") + q.toString(QUrl::FullyEncoded);
    }
    QString part;
    const QVariantList media = r.value("Media").toList();
    if (!media.isEmpty())
        part = media.first().toMap().value("Part").toList().value(0).toMap().value("key").toString();
    return QVariantMap{
        {"server", s.id}, {"id", r.value("ratingKey")}, {"key", r.value("key")}, {"title", r.value("title")}, {"subtitle", subtitle},
        {"kind", type}, {"folder", !playable}, {"playable", playable && !part.isEmpty()}, {"audio", type == QLatin1String("track")},
        {"image", image}, {"part", part}, {"duration", r.value("duration").toDouble() / 1000.0},
        {"resume", r.value("viewOffset").toDouble() / 1000.0}, {"overview", r.value("summary")},
    };
}

// ---------------------------------------------------------------- Navigation

void MediaServers::setItems(const QVariantList &items)
{
    m_items = items;
    m_path.clear();
    for (const Level &l : m_stack)
        m_path << l.title;
    emit itemsChanged();
}

void MediaServers::openServer(const QString &id)
{
    const Server *s = server(id);
    if (!s)
        return;
    m_current = id;
    m_stack.clear();
    load({s->name, {}, {}}, true);
}

void MediaServers::openItem(const QVariantMap &item)
{
    if (item.value("playable").toBool()) {
        const QString url = streamUrl(item);
        if (!url.isEmpty())
            emit playRequested(url, item.value("title").toString(), item);
        return;
    }
    if (item.value("folder").toBool())
        load({item.value("title").toString(), item, {}}, true);
}

void MediaServers::back()
{
    if (m_stack.size() <= 1) {
        m_current.clear();
        m_stack.clear();
        setItems({});
        return;
    }
    m_stack.removeLast();
    const Level l = m_stack.takeLast();
    load(l, true);
}

void MediaServers::search(const QString &text)
{
    if (text.trimmed().isEmpty() || !server(m_current))
        return;
    load({LTR("Suche: %1").arg(text), {}, QStringLiteral("search:") + text.trimmed()}, true);
}

void MediaServers::resume()
{
    if (!server(m_current))
        return;
    load({LTR("Weiterschauen"), {}, QStringLiteral("resume")}, true);
}

void MediaServers::load(const Level &level, bool push)
{
    const Server *sp = server(m_current);
    if (!sp)
        return;
    const Server s = *sp;
    if (push)
        m_stack.append(level);
    const QString query = level.query;
    const QVariantMap item = level.item;
    auto finish = [this](const QVariantList &items) { setItems(items); };

    if (isEmbyType(s.type)) {
        const QString user = QStringLiteral("/Users/") + s.userId;
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("Fields"), QStringLiteral("Overview,PrimaryImageAspectRatio"));
        q.addQueryItem(QStringLiteral("EnableImageTypes"), QStringLiteral("Primary"));
        QString path;
        if (query == QLatin1String("resume")) {
            path = user + QStringLiteral("/Items/Resume");
            q.addQueryItem(QStringLiteral("Limit"), QStringLiteral("50"));
        } else if (query.startsWith(QLatin1String("search:"))) {
            path = user + QStringLiteral("/Items");
            q.addQueryItem(QStringLiteral("SearchTerm"), query.mid(7));
            q.addQueryItem(QStringLiteral("Recursive"), QStringLiteral("true"));
            q.addQueryItem(QStringLiteral("IncludeItemTypes"), QStringLiteral("Movie,Series,Episode,Video,MusicVideo,Audio"));
            q.addQueryItem(QStringLiteral("Limit"), QStringLiteral("100"));
        } else if (item.isEmpty()) {
            path = user + QStringLiteral("/Views");
        } else {
            path = user + QStringLiteral("/Items");
            q.addQueryItem(QStringLiteral("ParentId"), item.value("id").toString());
            q.addQueryItem(QStringLiteral("SortBy"), QStringLiteral("ParentIndexNumber,IndexNumber,SortName"));
        }
        get(s, path, q, [this, s, finish](const QJsonDocument &doc) {
            QVariantList out;
            for (const QVariant &v : doc.object().toVariantMap().value("Items").toList())
                out << embyItem(s, v.toMap());
            finish(out);
        });
        return;
    }

    // Plex
    QString path;
    QUrlQuery q;
    if (query == QLatin1String("resume")) {
        path = QStringLiteral("/library/onDeck");
    } else if (query.startsWith(QLatin1String("search:"))) {
        path = QStringLiteral("/hubs/search");
        q.addQueryItem(QStringLiteral("query"), query.mid(7));
        q.addQueryItem(QStringLiteral("limit"), QStringLiteral("30"));
    } else if (item.isEmpty()) {
        path = QStringLiteral("/library/sections");
    } else {
        path = item.value("key").toString();
    }
    get(s, path, q, [this, s, finish, item](const QJsonDocument &doc) {
        const QVariantMap mc = doc.object().toVariantMap().value("MediaContainer").toMap();
        QVariantList out;
        if (item.isEmpty() && mc.contains("Directory") && !mc.contains("Metadata")) {
            for (const QVariant &v : mc.value("Directory").toList()) {
                const QVariantMap d = v.toMap();
                out << QVariantMap{{"server", s.id}, {"id", d.value("key")}, {"key", QStringLiteral("/library/sections/%1/all").arg(d.value("key").toString())},
                                   {"title", d.value("title")}, {"kind", d.value("type")}, {"folder", true}, {"playable", false}};
            }
        }
        QVariantList metadata = mc.value("Metadata").toList();
        for (const QVariant &hub : mc.value("Hub").toList())
            metadata += hub.toMap().value("Metadata").toList();
        for (const QVariant &v : metadata)
            out << plexItem(s, v.toMap());
        finish(out);
    });
}

QString MediaServers::streamUrl(const QVariantMap &item) const
{
    const Server *s = server(item.value("server").toString());
    if (!s || !item.value("playable").toBool())
        return {};
    if (isEmbyType(s->type))
        return QStringLiteral("%1/%2/%3/stream?static=true&api_key=%4")
            .arg(s->url, item.value("audio").toBool() ? QStringLiteral("Audio") : QStringLiteral("Videos"), item.value("id").toString(), s->token);
    return s->url + item.value("part").toString() + QStringLiteral("?X-Plex-Token=") + s->token;
}

// ---------------------------------------------------------------- Fortschritt

void MediaServers::reportStart(const QVariantMap &item, double position)
{
    m_playServer = item.value("server").toString();
    m_playItem = item;
    m_playSession = QUuid::createUuid().toString(QUuid::WithoutBraces);
    reportProgress(position, false);
}

void MediaServers::reportProgress(double position, bool paused)
{
    const Server *s = server(m_playServer);
    if (!s || m_playItem.isEmpty())
        return;
    if (isEmbyType(s->type)) {
        const bool first = !m_playItem.value("started").toBool();
        m_playItem["started"] = true;
        QNetworkRequest r = request(*s, first ? QStringLiteral("/Sessions/Playing") : QStringLiteral("/Sessions/Playing/Progress"));
        r.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        const QJsonObject body{{"ItemId", m_playItem.value("id").toString()}, {"PositionTicks", qint64(position * 1e7)},
                               {"PlaySessionId", m_playSession}, {"IsPaused", paused}, {"CanSeek", true}, {"PlayMethod", "DirectPlay"}};
        connect(m_net.post(r, QJsonDocument(body).toJson(QJsonDocument::Compact)), &QNetworkReply::finished, this,
                [this] { static_cast<QNetworkReply *>(sender())->deleteLater(); });
        return;
    }
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("ratingKey"), m_playItem.value("id").toString());
    q.addQueryItem(QStringLiteral("key"), QStringLiteral("/library/metadata/") + m_playItem.value("id").toString());
    q.addQueryItem(QStringLiteral("state"), paused ? QStringLiteral("paused") : QStringLiteral("playing"));
    q.addQueryItem(QStringLiteral("time"), plexTicksFree(qint64(position * 1000)));
    q.addQueryItem(QStringLiteral("duration"), plexTicksFree(qint64(m_playItem.value("duration").toDouble() * 1000)));
    connect(m_net.get(request(*s, QStringLiteral("/:/timeline"), q)), &QNetworkReply::finished, this,
            [this] { static_cast<QNetworkReply *>(sender())->deleteLater(); });
}

void MediaServers::reportStop(double position)
{
    const Server *s = server(m_playServer);
    if (!s || m_playItem.isEmpty())
        return;
    if (isEmbyType(s->type)) {
        QNetworkRequest r = request(*s, QStringLiteral("/Sessions/Playing/Stopped"));
        r.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        const QJsonObject body{{"ItemId", m_playItem.value("id").toString()}, {"PositionTicks", qint64(position * 1e7)},
                               {"PlaySessionId", m_playSession}};
        connect(m_net.post(r, QJsonDocument(body).toJson(QJsonDocument::Compact)), &QNetworkReply::finished, this,
                [this] { static_cast<QNetworkReply *>(sender())->deleteLater(); });
    } else {
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("ratingKey"), m_playItem.value("id").toString());
        q.addQueryItem(QStringLiteral("key"), QStringLiteral("/library/metadata/") + m_playItem.value("id").toString());
        q.addQueryItem(QStringLiteral("state"), QStringLiteral("stopped"));
        q.addQueryItem(QStringLiteral("time"), plexTicksFree(qint64(position * 1000)));
        q.addQueryItem(QStringLiteral("duration"), plexTicksFree(qint64(m_playItem.value("duration").toDouble() * 1000)));
        connect(m_net.get(request(*s, QStringLiteral("/:/timeline"), q)), &QNetworkReply::finished, this,
                [this] { static_cast<QNetworkReply *>(sender())->deleteLater(); });
    }
    m_playItem.clear();
    m_playServer.clear();
}

void MediaServers::removeServer(const QString &id)
{
    for (int i = 0; i < m_servers.size(); ++i) {
        if (m_servers.at(i).id == id) {
            m_servers.removeAt(i);
            break;
        }
    }
    if (m_current == id) {
        m_current.clear();
        m_stack.clear();
        setItems({});
    }
    save();
}
