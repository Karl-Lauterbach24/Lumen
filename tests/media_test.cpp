// Medienserver-Anbindung (Jellyfin/Emby, Plex) gegen einen lokalen Test-Server:
//
//   media_test <python> <tools/mock_media_server.py> <video.mp4>
//
// Anmeldung (falsches/richtiges Passwort), Bibliotheken, Serien/Staffeln/Folgen,
// Suche, Weiterschauen, Wiedergabe über mpv, Fortschritt an den Server,
// Plex-Anmeldung über plex.tv-PIN (umgeleitet auf den Test-Server).
#include "MediaServers.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QSettings>
#include <QTcpServer>

#include <mpv/client.h>

#include <clocale>
#include <cmath>
#include <cstdio>
#include <functional>

namespace {
int g_fail = 0;
void check(bool ok, const QString &what)
{
    std::printf("%s %s\n", ok ? "OK  " : "FAIL", qPrintable(what));
    std::fflush(stdout);
    if (!ok)
        ++g_fail;
}
bool waitFor(const std::function<bool()> &cond, int ms)
{
    QElapsedTimer t;
    t.start();
    while (!cond() && t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return cond();
}
QVariantMap find(const QVariantList &items, const QString &title)
{
    for (const QVariant &v : items)
        if (v.toMap().value("title") == title)
            return v.toMap();
    return {};
}
QJsonArray serverLog(int port)
{
    QNetworkAccessManager net;
    QNetworkReply *r = net.get(QNetworkRequest(QUrl(QStringLiteral("http://127.0.0.1:%1/_log").arg(port))));
    waitFor([&] { return r->isFinished(); }, 5000);
    const QJsonArray a = QJsonDocument::fromJson(r->readAll()).array();
    r->deleteLater();
    return a;
}
// mpv öffnet die Stream-URL, liefert die Dauer
double playDuration(const QString &url)
{
    mpv_handle *mpv = mpv_create();
    for (auto [k, v] : std::initializer_list<std::pair<const char *, const char *>>{
             {"vo", "null"}, {"ao", "null"}, {"idle", "yes"}, {"terminal", "no"}, {"pause", "yes"}})
        mpv_set_option_string(mpv, k, v);
    mpv_initialize(mpv);
    const QByteArray u = url.toUtf8();
    const char *cmd[] = {"loadfile", u.constData(), nullptr};
    mpv_command(mpv, cmd);
    double d = 0;
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < 10000) {
        mpv_wait_event(mpv, 0.1);
        if (mpv_get_property(mpv, "duration", MPV_FORMAT_DOUBLE, &d) >= 0 && d > 0)
            break;
    }
    mpv_terminate_destroy(mpv);
    return d;
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("LumenMediaTest"));
    QCoreApplication::setApplicationName(QStringLiteral("media_test"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.2.0"));
    std::setlocale(LC_NUMERIC, "C");
    if (argc < 4) {
        std::fprintf(stderr, "usage: media_test <python> <mock_media_server.py> <video.mp4>\n");
        return 2;
    }
    QSettings().clear();
    qputenv("LUMEN_NO_BROWSER", "1");

    // freien Port wählen, Test-Server starten
    int port = 0;
    {
        QTcpServer probe;
        probe.listen(QHostAddress::LocalHost, 0);
        port = probe.serverPort();
    }
    QProcess mock;
    mock.start(QString::fromLocal8Bit(argv[1]), {QString::fromLocal8Bit(argv[2]), QString::number(port), QString::fromLocal8Bit(argv[3])});
    check(mock.waitForStarted(5000), QStringLiteral("Test-Server gestartet (Port %1)").arg(port));
    waitFor([] { return false; }, 1500); // Server hochfahren lassen
    const QString base = QStringLiteral("http://127.0.0.1:%1").arg(port);

    MediaServers ms;
    bool added = false, ok = false;
    QString msg;
    QObject::connect(&ms, &MediaServers::serverAdded, [&](const QString &, bool success, const QString &m) {
        added = true;
        ok = success;
        msg = m;
    });

    // ---------------- Jellyfin
    ms.addEmbyServer(QStringLiteral("jellyfin"), base, QStringLiteral("test"), QStringLiteral("wrong"));
    bool done = waitFor([&] { return added; }, 10000);
    check(done && !ok, QStringLiteral("falsches Passwort abgelehnt: %1").arg(msg));
    added = false;
    ms.addEmbyServer(QStringLiteral("jellyfin"), base + QStringLiteral("/"), QStringLiteral("test"), QStringLiteral("test"));
    done = waitFor([&] { return added; }, 10000);
    check(done && ok && ms.servers().size() == 1, QStringLiteral("Jellyfin angemeldet"));
    const QVariantMap jf = ms.servers().value(0).toMap();
    check(jf.value("name") == QLatin1String("Mock Jellyfin"), QStringLiteral("Servername %1").arg(jf.value("name").toString()));
    check(!QSettings().value(QStringLiteral("servers/list/1/token")).toString().isEmpty()
              && !QSettings().allKeys().join(',').contains(QLatin1String("assword")),
          QStringLiteral("nur Token gespeichert, kein Passwort"));

    auto waitItems = [&](int n) { return waitFor([&] { return !ms.busy() && ms.items().size() == n; }, 10000); };
    ms.openServer(jf.value("id").toString());
    check(waitItems(2) && !find(ms.items(), "Movies").isEmpty(), QStringLiteral("Bibliotheken: Movies, Shows"));
    ms.openItem(find(ms.items(), "Movies"));
    check(waitItems(1), QStringLiteral("Film-Bibliothek geöffnet"));
    const QVariantMap movie = find(ms.items(), "Test Movie");
    check(movie.value("playable").toBool() && movie.value("subtitle") == QLatin1String("2026")
              && std::abs(movie.value("resume").toDouble() - 0.5) < 0.01 && movie.value("image").toString().contains(QLatin1String("api_key=")),
          QStringLiteral("Film: abspielbar, Jahr, Fortsetzen bei 0,5 s, Cover"));
    const QString jfUrl = ms.streamUrl(movie);
    check(jfUrl.contains(QLatin1String("/Videos/m1/stream?static=true")), QStringLiteral("Direkt-Stream-URL"));
    QString requested;
    QObject::connect(&ms, &MediaServers::playRequested, [&](const QString &url, const QString &, const QVariantMap &) { requested = url; });
    ms.openItem(movie);
    check(requested == jfUrl, QStringLiteral("Öffnen eines Films fordert Wiedergabe an"));
    const double d1 = playDuration(jfUrl);
    check(std::abs(d1 - 2.0) < 0.3, QStringLiteral("mpv spielt den Jellyfin-Stream (%1 s)").arg(d1, 0, 'f', 2));
    ms.reportStart(movie, 0.5);
    ms.reportProgress(1.2, true);
    ms.reportStop(1.9);
    waitFor([&] { return serverLog(port).size() >= 3; }, 5000);
    const QJsonArray log1 = serverLog(port);
    check(log1.size() >= 3 && log1[0].toObject().value("path") == QLatin1String("/Sessions/Playing")
              && log1[2].toObject().value("path") == QLatin1String("/Sessions/Playing/Stopped")
              && log1[2].toObject().value("body").toObject().value("PositionTicks").toDouble() == 19000000.0,
          QStringLiteral("Fortschritt gemeldet: Playing, Progress, Stopped bei 1,9 s"));
    ms.back();
    check(waitItems(2), QStringLiteral("zurück zu den Bibliotheken"));
    ms.openItem(find(ms.items(), "Shows"));
    check(waitItems(1), QStringLiteral("Serien"));
    ms.openItem(find(ms.items(), "Test Show"));
    check(waitItems(1) && !find(ms.items(), "Season 1").isEmpty(), QStringLiteral("Staffeln"));
    ms.openItem(find(ms.items(), "Season 1"));
    check(waitItems(1) && find(ms.items(), "Pilot").value("subtitle") == QStringLiteral("S1E1 · Test Show"), QStringLiteral("Folge S1E1"));
    check(ms.path().size() == 4, QStringLiteral("Pfad: Server › Shows › Test Show › Season 1"));
    ms.search(QStringLiteral("pilot"));
    check(waitItems(1) && !find(ms.items(), "Pilot").isEmpty(), QStringLiteral("Suche findet die Folge"));
    ms.resume();
    check(waitItems(1) && !find(ms.items(), "Test Movie").isEmpty(), QStringLiteral("Weiterschauen"));

    // ---------------- Plex (URL + Token)
    added = false;
    ms.addPlexServer(base + QStringLiteral("/plex"), QStringLiteral("falsch"));
    done = waitFor([&] { return added; }, 10000);
    check(done && !ok, QStringLiteral("ungültiges Plex-Token abgelehnt: %1").arg(msg));
    added = false;
    ms.addPlexServer(base + QStringLiteral("/plex"), QStringLiteral("plex-test-token"));
    done = waitFor([&] { return added; }, 10000);
    check(done && ok && ms.servers().size() == 2, QStringLiteral("Plex verbunden"));
    const QVariantMap px = ms.servers().value(1).toMap();
    check(px.value("name") == QLatin1String("Mock Plex"), QStringLiteral("Plex-Servername %1").arg(px.value("name").toString()));
    ms.openServer(px.value("id").toString());
    check(waitItems(2) && !find(ms.items(), "Filme").isEmpty(), QStringLiteral("Plex-Bibliotheken"));
    ms.openItem(find(ms.items(), "Filme"));
    check(waitItems(1), QStringLiteral("Plex-Filme"));
    const QVariantMap pmovie = find(ms.items(), "Plex Movie");
    const QString pxUrl = ms.streamUrl(pmovie);
    check(pmovie.value("playable").toBool() && pxUrl.endsWith(QLatin1String("/plex/library/parts/7/file.mp4?X-Plex-Token=plex-test-token"))
              && pmovie.value("image").toString().contains(QLatin1String("/photo/:/transcode")),
          QStringLiteral("Plex-Film: Part-URL mit Token, Cover"));
    const double d2 = playDuration(pxUrl);
    check(std::abs(d2 - 2.0) < 0.3, QStringLiteral("mpv spielt den Plex-Stream (%1 s)").arg(d2, 0, 'f', 2));
    ms.reportStart(pmovie, 0.7);
    ms.reportStop(1.5);
    waitFor([&] { return serverLog(port).size() >= log1.size() + 2; }, 5000);
    const QJsonArray log2 = serverLog(port);
    const QJsonObject last = log2.last().toObject();
    check(last.value("server") == QLatin1String("plex") && last.value("query").toObject().value("state") == QLatin1String("stopped")
              && last.value("query").toObject().value("time") == QLatin1String("1500"),
          QStringLiteral("Plex-Timeline: gestoppt bei 1500 ms"));
    ms.back();
    waitItems(2);
    ms.openItem(find(ms.items(), "Serien"));
    check(waitItems(1), QStringLiteral("Plex-Serien"));
    ms.openItem(find(ms.items(), "Plex Show"));
    check(waitItems(1), QStringLiteral("Plex-Staffeln"));
    ms.openItem(find(ms.items(), "Staffel 1"));
    check(waitItems(1) && find(ms.items(), "Folge 1").value("subtitle") == QStringLiteral("S1E1 · Plex Show"), QStringLiteral("Plex-Folge S1E1"));
    ms.search(QStringLiteral("movie"));
    check(waitItems(1) && !find(ms.items(), "Plex Movie").isEmpty(), QStringLiteral("Plex-Suche (Hubs)"));

    // ---------------- Plex-Anmeldung über plex.tv (PIN)
    ms.setPlexTvBase(QUrl(base));
    ms.plexSignIn();
    const bool adopted = waitFor([&] {
        for (const QVariant &v : ms.servers())
            if (v.toMap().value("name") == QLatin1String("Mock Plex (Konto)"))
                return true;
        return false;
    }, 15000);
    QString adoptedUrl;
    for (const QVariant &v : ms.servers())
        if (v.toMap().value("name") == QLatin1String("Mock Plex (Konto)"))
            adoptedUrl = v.toMap().value("url").toString();
    check(adopted && adoptedUrl == base + QStringLiteral("/plex"),
          QStringLiteral("PIN-Anmeldung: Server übernommen, lokale Verbindung statt Relay (%1)").arg(adoptedUrl));

    ms.removeServer(jf.value("id").toString());
    // Der Konto-Server ist derselbe wie der manuell verbundene: kein Duplikat, Eintrag aktualisiert
    check(ms.servers().size() == 1, QStringLiteral("Jellyfin entfernt, ein Plex-Eintrag (kein Duplikat)"));

    mock.kill();
    mock.waitForFinished(3000);
    QSettings().clear();
    std::printf("%s (%d Fehler)\n", g_fail ? "FEHLGESCHLAGEN" : "BESTANDEN", g_fail);
    return g_fail ? 1 : 0;
}
