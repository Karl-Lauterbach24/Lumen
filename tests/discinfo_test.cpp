// Disc-Erkennung: Inhaltsverzeichnis/MusicBrainz-Disc-ID/CD-Text einer Audio-CD (Abbild) und
// die Skript-Schnittstelle der Plugins (Ereignisse, HTTP über Lumen, Metadaten zurück):
//
//   discinfo_test <plugin-ordner> <python> <tools/mock_disc_server.py>
//
// <plugin-ordner> enthält das Plugin "disc-identify" (Repository Lumen-Plugins, plugins/).
// Das Plugin fragt statt MusicBrainz/Wikidata den lokalen Test-Server.
#include "OpticalMedia.h"
#include "PluginManager.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QProcess>
#include <QTcpServer>
#include <QTemporaryDir>

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
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("LumenDiscInfoTest"));
    QCoreApplication::setApplicationName(QStringLiteral("discinfo_test"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.2.1"));
    std::setlocale(LC_NUMERIC, "C");
    if (argc < 4) {
        std::fprintf(stderr, "usage: discinfo_test <plugin-dir> <python> <mock_disc_server.py>\n");
        return 2;
    }

    // ---------------- MusicBrainz-Disc-ID (Beispiel aus der MusicBrainz-Datenbank)
    check(Optical::musicBrainzDiscId(1, 6, 95462, {150, 15363, 32314, 46592, 63414, 80489})
              == QLatin1String("49HHV7Eb8UKF3aQiNmu1GR8vKTY-"),
          QStringLiteral("MusicBrainz-Disc-ID aus dem Inhaltsverzeichnis"));
    const QString testId = Optical::musicBrainzDiscId(1, 3, 600, {150, 300, 450});
    check(testId == QLatin1String("IWFVjcDW_sHbcouxZ99EXu2rOyw-"), QStringLiteral("Disc-ID des Test-Abbilds %1").arg(testId));

    // ---------------- Audio-CD-Abbild (CUE/BIN, 3 Tracks je 2 s, mit CD-Text)
    QTemporaryDir tmp;
    QVariantMap disc{{"device", tmp.filePath(QStringLiteral("cd.cue"))}, {"kind", "cdda"}, {"label", ""},
                     {"mbDiscId", testId},
                     {"toc", QVariantMap{{"first", 1}, {"last", 3}, {"leadout", 600}, {"offsets", QVariantList{150, 300, 450}}}}};
    {
        QFile bin(tmp.filePath(QStringLiteral("cd.bin")));
        check(bin.open(QIODevice::WriteOnly) && bin.write(QByteArray(450 * 2352, '\0')) == 450 * 2352, QStringLiteral("Abbild geschrieben"));
        QFile cue(tmp.filePath(QStringLiteral("cd.cue")));
        if (cue.open(QIODevice::WriteOnly))
            cue.write("PERFORMER \"Cue Artist\"\nTITLE \"Cue Album\"\nFILE \"cd.bin\" BINARY\n"
                      "  TRACK 01 AUDIO\n    TITLE \"Intro\"\n    PERFORMER \"Cue Artist\"\n    INDEX 01 00:00:00\n"
                      "  TRACK 02 AUDIO\n    TITLE \"Middle\"\n    PERFORMER \"Cue Artist\"\n    INDEX 01 00:02:00\n"
                      "  TRACK 03 AUDIO\n    TITLE \"Finale\"\n    PERFORMER \"Somebody Else\"\n    INDEX 01 00:04:00\n");
    }
    if (Optical::cdioAvailable()) {
        const QString cue = tmp.filePath(QStringLiteral("cd.cue"));
        check(Optical::detect(cue) == QLatin1String("cdda"), QStringLiteral("Abbild als Audio-CD erkannt"));
        const QVariantMap scan = Optical::scan(cue, QStringLiteral("cdda"));
        const QVariantList titles = scan.value("titles").toList();
        const QVariantMap toc = scan.value("toc").toMap();
        check(titles.size() == 3, QStringLiteral("3 Tracks (%1)").arg(titles.size()));
        check(toc.value("first") == 1 && toc.value("last") == 3 && toc.value("leadout") == 600
                  && toc.value("offsets").toList() == QVariantList({150, 300, 450}),
              QStringLiteral("Inhaltsverzeichnis: 1–3, Lead-out %1").arg(toc.value("leadout").toInt()));
        check(scan.value("mbDiscId") == testId, QStringLiteral("Disc-ID aus dem Scan %1").arg(scan.value("mbDiscId").toString()));
        check(scan.value("cdText").toBool() && scan.value("discName") == QStringLiteral("Cue Artist – Cue Album"),
              QStringLiteral("CD-Text: %1").arg(scan.value("discName").toString()));
        check(titles.value(0).toMap().value("label") == QLatin1String("1. Intro")
                  && titles.value(2).toMap().value("label") == QLatin1String("3. Finale")
                  && titles.value(2).toMap().value("artist") == QLatin1String("Somebody Else")
                  && !titles.value(0).toMap().contains("artist"),
              QStringLiteral("CD-Text: Tracknamen, abweichender Interpret nur bei Track 3"));
    } else {
        std::printf("     (ohne libcdio gebaut – Abbild-Scan übersprungen)\n");
    }

    // ---------------- Test-Server
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
    int port = 0;
    {
        QTcpServer probe;
        probe.listen(QHostAddress::LocalHost, 0);
        port = probe.serverPort();
    }
    const QString base = QStringLiteral("http://127.0.0.1:%1").arg(port);
    QProcess mock;
    mock.setProcessChannelMode(QProcess::ForwardedChannels);
    mock.start(QString::fromLocal8Bit(argv[2]), {QString::fromLocal8Bit(argv[3]), QString::number(port)});
    QNetworkAccessManager net;
    auto pumpQt = [](const std::function<bool()> &cond, int ms) {
        QElapsedTimer t;
        t.start();
        while (!cond() && t.elapsed() < ms)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        return cond();
    };
    auto serverLog = [&] {
        QNetworkReply *r = net.get(QNetworkRequest(QUrl(base + QStringLiteral("/_log"))));
        pumpQt([&] { return r->isFinished(); }, 15000);
        const bool ok = r->isFinished() && r->error() == QNetworkReply::NoError;
        const QJsonArray a = ok ? QJsonDocument::fromJson(r->readAll()).array() : QJsonArray();
        delete r;
        return qMakePair(ok, a);
    };
    QElapsedTimer up;
    up.start();
    bool ready = false;
    while (!ready && up.elapsed() < 60000 && mock.state() != QProcess::NotRunning) {
        ready = serverLog().first;
        if (!ready)
            pumpQt([] { return false; }, 200);
    }
    check(ready, QStringLiteral("Test-Server bereit (Port %1)").arg(port));

    // ---------------- Plugin laden, mpv mit dessen Skript starten
    qputenv("LUMEN_PLUGIN_PATH", argv[1]);
    qputenv("LUMEN_PLUGINS_ENABLE", "disc-identify");
    for (const char *var : {"LUMEN_DISCID_MUSICBRAINZ", "LUMEN_DISCID_COVERART", "LUMEN_DISCID_WIKIDATA"})
        qputenv(var, base.toUtf8());
    PluginManager pm;
    pm.discover();
    pm.loadEnabled();
    auto plugin = [&] {
        for (const QVariant &v : pm.plugins())
            if (v.toMap().value("id") == QLatin1String("disc-identify"))
                return v.toMap();
        return QVariantMap();
    };
    check(plugin().value("loaded").toBool(), QStringLiteral("Plugin disc-identify geladen %1").arg(plugin().value("error").toString()));

    mpv_handle *mpv = mpv_create();
    for (auto [k, v] : std::initializer_list<std::pair<const char *, const char *>>{
             {"vo", "null"}, {"ao", "null"}, {"idle", "yes"}, {"terminal", "no"}, {"config", "no"}})
        mpv_set_option_string(mpv, k, v);
    const QVariantMap opts = pm.mpvOptions();
    for (auto it = opts.cbegin(); it != opts.cend(); ++it)
        mpv_set_option_string(mpv, it.key().toUtf8().constData(), it.value().toString().toUtf8().constData());
    mpv_request_log_messages(mpv, "warn");
    if (mpv_initialize(mpv) < 0)
        return 1;
    pm.attach(mpv);

    QList<QVariantMap> infos;
    QObject::connect(&pm, &PluginManager::discInfoProvided, [&](const QVariantMap &i) { infos << i; });
    // wie im Player: Nachrichten der Skripte an den Plugin-Manager weiterreichen
    auto waitFor = [&](const std::function<bool()> &cond, int ms) {
        QElapsedTimer t;
        t.start();
        while (!cond() && t.elapsed() < ms) {
            while (mpv_event *ev = mpv_wait_event(mpv, 0.02)) {
                if (ev->event_id == MPV_EVENT_NONE)
                    break;
                if (ev->event_id == MPV_EVENT_LOG_MESSAGE) {
                    auto *m = static_cast<mpv_event_log_message *>(ev->data);
                    std::printf("     mpv[%s] %s", m->prefix, m->text);
                } else if (ev->event_id == MPV_EVENT_CLIENT_MESSAGE) {
                    auto *m = static_cast<mpv_event_client_message *>(ev->data);
                    QStringList args;
                    for (int i = 0; i < m->num_args; ++i)
                        args << QString::fromUtf8(m->args[i]);
                    pm.handleScriptMessage(args);
                }
            }
            QCoreApplication::processEvents();
        }
        return cond();
    };
    auto statusHas = [&](const char *text) { return plugin().value("status").toString().contains(QLatin1String(text)); };

    const bool scriptUp = waitFor([&] { return statusHas("Ready"); }, 10000);
    check(scriptUp, QStringLiteral("Skript läuft, Status: %1").arg(plugin().value("status").toString()));

    // ---------------- Audio-CD -> MusicBrainz
    pm.sendEvent(QStringLiteral("disc"), disc);
    check(waitFor([&] { return !infos.isEmpty(); }, 20000), QStringLiteral("Metadaten zur Audio-CD erhalten"));
    const QVariantMap cd = infos.value(0);
    check(cd.value("title") == QLatin1String("Test Album") && cd.value("artist") == QLatin1String("Test Artist")
              && cd.value("year") == QLatin1String("1999") && cd.value("source") == QLatin1String("MusicBrainz"),
          QStringLiteral("Album: %1 – %2 (%3)").arg(cd.value("artist").toString(), cd.value("title").toString(), cd.value("year").toString()));
    check(cd.value("device") == disc.value("device"), QStringLiteral("Metadaten gehören zur gemeldeten Disc"));
    const QVariantList tracks = cd.value("tracks").toList();
    check(tracks.size() == 3 && tracks.value(0).toMap().value("title") == QLatin1String("First Song")
              && tracks.value(1).toMap().value("artist") == QLatin1String("Test Artist feat. A Guest")
              && tracks.value(2).toMap().value("title") == QStringLiteral("Third Söng"),
          QStringLiteral("3 Tracknamen vom richtigen Medium des Sets (%1)").arg(tracks.value(0).toMap().value("title").toString()));
    check(cd.value("cover").toString() == base + QStringLiteral("/release/11111111-2222-3333-4444-555555555555/front-250"),
          QStringLiteral("Cover-Adresse"));
    const bool named = waitFor([&] { return statusHas("Test Album"); }, 5000);
    check(named, QStringLiteral("Status: %1").arg(plugin().value("status").toString()));
    {
        const QJsonObject req = serverLog().second.last().toObject();
        check(req.value("path").toString() == QStringLiteral("/ws/2/discid/") + testId
                  && req.value("query").toString().contains(QLatin1String("toc=1+3+600+150+300+450"))
                  && req.value("user-agent").toString().startsWith(QLatin1String("Lumen/")),
              QStringLiteral("Anfrage: Disc-ID, Inhaltsverzeichnis, Kennung „%1“").arg(req.value("user-agent").toString()));
    }

    // unbekannte CD: keine Metadaten, Hinweis im Status
    infos.clear();
    QVariantMap unknown = disc;
    unknown["mbDiscId"] = QStringLiteral("AAAAAAAAAAAAAAAAAAAAAAAAAAA-");
    pm.sendEvent(QStringLiteral("disc"), unknown);
    const bool notFound = waitFor([&] { return statusHas("not found"); }, 20000);
    check(notFound && infos.isEmpty(), QStringLiteral("unbekannte CD: %1").arg(plugin().value("status").toString()));

    // ---------------- DVD -> Wikidata (Film statt des ersten Treffers „media franchise“)
    pm.sendEvent(QStringLiteral("disc"), {{"device", "D:/"}, {"kind", "dvd"}, {"label", "THE_MATRIX_D1"}});
    check(waitFor([&] { return !infos.isEmpty(); }, 20000), QStringLiteral("Metadaten zur DVD erhalten"));
    check(infos.value(0).value("title") == QLatin1String("The Matrix") && infos.value(0).value("year") == QLatin1String("1999")
              && infos.value(0).value("source") == QLatin1String("Wikidata"),
          QStringLiteral("DVD-Label THE_MATRIX_D1 -> %1 (%2), %3").arg(infos.value(0).value("title").toString(),
                                                                      infos.value(0).value("year").toString(),
                                                                      infos.value(0).value("source").toString()));
    check(serverLog().second.last().toObject().value("query").toString().contains(QLatin1String("search=The%20Matrix")),
          QStringLiteral("Suchbegriff ist das bereinigte Label"));

    // Label ohne Treffer: bereinigtes Label als Name
    infos.clear();
    pm.sendEvent(QStringLiteral("disc"), {{"device", "D:/"}, {"kind", "dvd"}, {"label", "HOLIDAY_1998_DISC_2"}});
    const bool labelOnly = waitFor([&] { return !infos.isEmpty(); }, 20000);
    check(labelOnly && infos.value(0).value("title") == QLatin1String("Holiday 1998")
              && infos.value(0).value("source") == QLatin1String("Disc label"),
          QStringLiteral("unbekanntes Label -> „%1“").arg(infos.value(0).value("title").toString()));

    // nichtssagendes Label: keine Anfrage, keine Metadaten
    infos.clear();
    const int before = serverLog().second.size();
    pm.sendEvent(QStringLiteral("disc"), {{"device", "D:/"}, {"kind", "dvd"}, {"label", "DVD_VIDEO"}});
    check(waitFor([&] { return statusHas("says nothing"); }, 10000) && infos.isEmpty() && serverLog().second.size() == before,
          QStringLiteral("nichtssagendes Label DVD_VIDEO wird nicht nachgeschlagen"));

    // Blu-ray mit eigenem Titel in den Disc-Metadaten: unverändert als Suchbegriff
    pm.sendEvent(QStringLiteral("disc"), {{"device", "D:/"}, {"kind", "bluray"}, {"label", "MATRIX_BD"}, {"discName", "The Matrix"}});
    check(waitFor([&] { return !infos.isEmpty(); }, 20000) && infos.value(0).value("source") == QLatin1String("Wikidata"),
          QStringLiteral("Blu-ray: Titel aus den Disc-Metadaten"));

    // ---------------- Wiedergabe des Abbilds (Protokoll lumencdda, Kapitel = Tracks mit CD-Text-Namen)
    if (Optical::cdioAvailable()) {
        Optical::attachProtocol(mpv);
        const Optical::Prepared prep = Optical::prepare(tmp.filePath(QStringLiteral("cd.cue")), QStringLiteral("cdda"), 1);
        check(prep.error.isEmpty() && prep.url.startsWith(QLatin1String("lumencdda://")), QStringLiteral("Audio-CD vorbereitet: %1 %2").arg(prep.url, prep.error));
        for (auto it = prep.options.cbegin(); it != prep.options.cend(); ++it)
            mpv_set_property_string(mpv, it.key().toUtf8().constData(), it.value().toString().toUtf8().constData());
        mpv_set_property_string(mpv, "pause", "yes");
        const QByteArray u = prep.url.toUtf8();
        const char *load[] = {"loadfile", u.constData(), nullptr};
        mpv_command(mpv, load);
        auto prop = [&](const char *name) {
            char *v = mpv_get_property_string(mpv, name);
            const QString r = v ? QString::fromUtf8(v) : QString();
            mpv_free(v);
            return r;
        };
        const bool opened = waitFor([&] { return prop("duration").toDouble() > 5.9; }, 10000);
        check(opened && std::abs(prop("duration").toDouble() - 6.0) < 0.05, QStringLiteral("Abbild spielt: Dauer %1 s").arg(prop("duration")));
        check(prop("audio-params/samplerate") == QLatin1String("44100") && prop("audio-params/channel-count") == QLatin1String("2"),
              QStringLiteral("44,1 kHz Stereo (%1 Hz, %2 Kanäle)").arg(prop("audio-params/samplerate"), prop("audio-params/channel-count")));
        check(prop("chapter-list/count") == QLatin1String("3") && prop("chapter-list/2/title") == QLatin1String("3. Finale")
                  && std::abs(prop("chapter-list/1/time").toDouble() - 2.0) < 0.01,
              QStringLiteral("3 Kapitel mit Tracknamen (%1), Track 2 bei %2 s").arg(prop("chapter-list/2/title"), prop("chapter-list/1/time")));
        const bool atTrack2 = waitFor([&] { return prop("chapter") == QLatin1String("1"); }, 5000);
        check(atTrack2, QStringLiteral("Start bei gewähltem Track 2 (Kapitel %1)").arg(prop("chapter")));
    }

    pm.detach();
    mpv_terminate_destroy(mpv);
    pm.unloadAll();
    mock.kill();
    mock.waitForFinished(3000);
    std::printf("%s (%d Fehler)\n", g_fail ? "FEHLGESCHLAGEN" : "BESTANDEN", g_fail);
    return g_fail ? 1 : 0;
}
