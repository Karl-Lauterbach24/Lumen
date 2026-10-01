#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QTemporaryFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScreen>
#include <QSet>
#include <QTimer>
#include <QUrl>

#include <mpv/client.h>

#include <clocale>

#include "BlurayNav.h"
#include "CastManager.h"
#include "CastOutput.h"
#include "DcpManager.h"
#include "DiscScanner.h"
#include "DvdNav.h"
#include "MediaServers.h"
#include "Updater.h"
#include "I18n.h"
#include "DisplayManager.h"
#include "DriveManager.h"
#include "MpvController.h"
#include "PluginManager.h"
#include "Recent.h"
#include "PluginStore.h"
#include "ProfileManager.h"
#include "VcdNav.h"

// "lumen --selftest <datei.json>": meldet, was die mitgelieferten Bibliotheken können, und endet.
// Damit prüfen die Paket-Builds auf jeder Plattform dasselbe (FFmpeg-mvc, libmpv mit Ton-Abgriff).
static int selfTest(const QString &file)
{
    QJsonObject out{{"version", LUMEN_VERSION}};
    mpv_handle *mpv = mpv_create();
    if (mpv) {
        mpv_set_option_string(mpv, "vo", "null");
        mpv_set_option_string(mpv, "ao", "null");
        mpv_set_option_string(mpv, "config", "no");
        mpv_set_option_string(mpv, "terminal", "no");
        if (mpv_initialize(mpv) >= 0) {
            auto prop = [&](const char *name) {
                char *v = mpv_get_property_string(mpv, name);
                const QString text = QString::fromUtf8(v ? v : "");
                mpv_free(v);
                return text;
            };
            out["mpv"] = prop("mpv-version");
            out["ffmpeg"] = prop("ffmpeg-version");
            // Blu-ray 3D: FFmpeg-mvc meldet sich mit "mvc" in der Versionskennung
            out["mvc"] = prop("ffmpeg-version").contains(QLatin1String("mvc"), Qt::CaseInsensitive);
            // Lua-Skripte (Plugins, yt-dlp)
            QTemporaryFile script(QDir::tempPath() + QStringLiteral("/lumen-selftest-XXXXXX.lua"));
            bool lua = false;
            if (script.open()) {
                script.write("mp.set_property('user-data/lumen-selftest', 'ok')\n");
                script.close();
                const QByteArray path = script.fileName().toUtf8();
                const char *load[] = {"load-script", path.constData(), nullptr};
                mpv_command(mpv, load);
                for (int i = 0; i < 40 && !lua; ++i) {
                    mpv_wait_event(mpv, 0.05);
                    lua = prop("user-data/lumen-selftest") == QLatin1String("ok");
                }
            }
            out["lua"] = lua;
        }
        mpv_terminate_destroy(mpv);
    }
    out["encoder"] = CastEncoder::videoEncoderName();
    out["audioTap"] = castTapAvailable();
    out["cast"] = !CastEncoder::videoEncoderName().isEmpty() && castTapAvailable();
    out["bluray"] = BlurayNav::available();
    out["dvdnav"] = DvdNav::available();
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return 1;
    f.write(QJsonDocument(out).toJson());
    return 0;
}

int main(int argc, char *argv[])
{
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QGuiApplication app(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("Lumen"));
    QGuiApplication::setApplicationName(QStringLiteral("Lumen"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Lumen"));
    QGuiApplication::setApplicationVersion(QStringLiteral(LUMEN_VERSION));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/qt/qml/Lumen/resources/logo/lumen-icon-256.png")));

    // libmpv verlangt den C-Locale für Zahlen
    std::setlocale(LC_NUMERIC, "C");
    {
        const QStringList a = app.arguments();
        const int i = a.indexOf(QStringLiteral("--selftest"));
        if (i >= 0 && i + 1 < a.size())
            return selfTest(a.at(i + 1));
    }
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    I18n i18n; // vor allen anderen: Texte der Objekte sind dann schon übersetzt
    // Plugins vor libmpv/libbluray laden: Umgebung, Disc-Bibliotheken, mpv-Skripte
    PluginManager plugins;
    plugins.loadEnabled();
    PluginStore store(&plugins);
    DisplayManager displays;
    DriveManager drives;
    ProfileManager profiles;
    DiscScanner scanner;
    BlurayNav nav;
    DvdNav dvd;
    VcdNav vcd;
    MpvController player(&displays, &nav);
    player.setDvdNav(&dvd);
    player.setVcdNav(&vcd);
    player.addOptionProvider([&plugins] { return plugins.mpvOptions(); });
    player.addProtocol([&plugins](mpv_handle *mpv) { plugins.attach(mpv); });
    QObject::connect(&player, &MpvController::mpvDestroying, &plugins, &PluginManager::detach);

    player.initialize(profiles.currentProfile());
    DcpManager dcp(&player, &displays);
    QObject::connect(&player, &MpvController::dcpRequested, &dcp, [&](const QString &path, int cpl) {
        // Bereits geladenes Paket: direkt die gewünschte CPL, sonst einlesen und starten
        if (QDir::cleanPath(dcp.root()) == QDir::cleanPath(path) && !dcp.cpls().isEmpty())
            dcp.play(cpl < 0 ? 0 : cpl);
        else
            dcp.open(path, true);
    });

    QObject::connect(&profiles, &ProfileManager::currentProfileChanged, &player,
                     [&] { player.applyProfile(profiles.currentProfile()); });
    QObject::connect(&player, &MpvController::shutdownRequested, &app, &QCoreApplication::quit);
    // Ereignisse an Plugins
    QObject::connect(&player, &MpvController::fileLoaded, &plugins, [&] {
        plugins.sendEvent(QStringLiteral("file-loaded"),
                          {{"path", player.path()}, {"kind", player.sourceKind()}, {"title", player.mediaTitle()}});
    });
    QObject::connect(&player, &MpvController::idleChanged, &plugins, [&] {
        if (player.idle())
            plugins.sendEvent(QStringLiteral("end-file"), {{"path", player.path()}});
    });
    QObject::connect(&scanner, &DiscScanner::scanned, &plugins, [&] {
        const QVariantMap i = scanner.info();
        if (i.value("device").toString().isEmpty())
            return;
        QVariantMap payload{{"device", i.value("device")}, {"kind", i.value("kind")},
                            {"label", i.value("label", i.value("title"))}};
        // Angaben, mit denen ein Plugin die Disc erkennen kann
        for (const char *key : {"discName", "volumeId", "mbDiscId", "toc", "cdText"})
            if (i.contains(QLatin1String(key)))
                payload.insert(QLatin1String(key), i.value(QLatin1String(key)));
        QVariantList titles;
        for (const QVariant &v : i.value("titles").toList()) {
            const QVariantMap t = v.toMap();
            titles << QVariantMap{{"index", t.value("index")}, {"duration", t.value("duration")}, {"chapters", t.value("chapters")}};
        }
        payload.insert(QStringLiteral("titles"), titles);
        plugins.sendEvent(QStringLiteral("disc"), payload);
    });
    QObject::connect(&player, &MpvController::pluginMessage, &plugins, &PluginManager::handleScriptMessage);
    QObject::connect(&plugins, &PluginManager::discInfoProvided, &scanner, &DiscScanner::applyMetadata);
    QObject::connect(&plugins, &PluginManager::openRequested, &player, [&](const QString &url) {
        const QFileInfo fi(url);
        if (fi.exists())
            player.openSource(fi.absoluteFilePath(), QStringLiteral("main"));
        else
            player.openLocation(url);
    });

    // Medienserver: Wiedergabe über den Player, Fortschritt zurück an den Server
    MediaServers servers;
    struct { bool active = false; QString url; double position = 0; } serverPlay;
    QObject::connect(&servers, &MediaServers::playRequested, &player, [&](const QString &url, const QString &title, const QVariantMap &item) {
        if (serverPlay.active)
            servers.reportStop(serverPlay.position);
        const double resume = item.value("resume").toDouble();
        const double start = resume > 10 && resume < item.value("duration").toDouble() - 30 ? resume : 0;
        player.openStream(url, title, start);
        servers.reportStart(item, start);
        serverPlay = {true, url, start};
    });
    // Zuletzt gespielt (Startseite): lokale Quellen mit Position zum Fortsetzen
    Recent recent;
    auto recentKey = [&]() -> QString {
        if (player.sourceKind() == QLatin1String("file"))
            return QFileInfo::exists(player.path()) ? player.path() : QString();
        return player.device();
    };
    // mpv meldet "Datei geladen" und den Pfad unabhängig voneinander – beides abwarten
    auto noteCurrent = [&] {
        if (player.idle())
            return;
        const bool file = player.sourceKind() == QLatin1String("file");
        recent.note(recentKey(), player.sourceKind(), file ? QString() : scanner.info().value("discName").toString());
    };
    QObject::connect(&player, &MpvController::fileLoaded, &recent, noteCurrent);
    QObject::connect(&player, &MpvController::mediaChanged, &recent, noteCurrent);
    QObject::connect(&player, &MpvController::idleChanged, &recent, noteCurrent);
    QObject::connect(&scanner, &DiscScanner::infoChanged, &recent, [&] {
        // Disc-Name (auch nachträglich von einem Plugin) als Titel übernehmen
        const QString name = scanner.info().value("discName").toString();
        if (!player.idle() && !name.isEmpty() && player.sourceKind() != QLatin1String("file")
            && scanner.info().value("device").toString() == player.device())
            recent.note(player.device(), player.sourceKind(), name);
    });
    QTimer recentProgress;
    recentProgress.setInterval(5000);
    QObject::connect(&recentProgress, &QTimer::timeout, &recent, [&] {
        if (!player.idle() && player.sourceKind() == QLatin1String("file") && player.duration() > 60)
            recent.notePosition(recentKey(), player.position(), player.duration());
    });
    recentProgress.start();

    QTimer serverProgress;
    serverProgress.setInterval(10000);
    QObject::connect(&serverProgress, &QTimer::timeout, &servers, [&] {
        if (serverPlay.active && !player.idle()) {
            serverPlay.position = player.position();
            servers.reportProgress(serverPlay.position, player.paused());
        }
    });
    serverProgress.start();
    auto serverStop = [&] {
        if (!serverPlay.active)
            return;
        servers.reportStop(serverPlay.position);
        serverPlay.active = false;
    };
    QObject::connect(&player, &MpvController::positionChanged, &servers, [&] {
        if (serverPlay.active && player.path() == serverPlay.url)
            serverPlay.position = player.position();
    });
    QObject::connect(&player, &MpvController::mediaChanged, &servers, [&] {
        if (serverPlay.active && !player.path().isEmpty() && player.path() != serverPlay.url)
            serverStop();
    });
    QObject::connect(&player, &MpvController::idleChanged, &servers, [&] {
        if (player.idle())
            serverStop();
    });

    // Übertragung an Empfänger im Netz; Tasten der TV-Fernbedienung steuern den Player
    CastManager cast(&player);
    QObject::connect(&cast, &CastManager::remoteKey, &player, [&](const QString &key) {
        static const QSet<QString> nav = {"up", "down", "left", "right", "enter", "menu"};
        if (nav.contains(key))
            player.command({"script-message", "lumen-key", key});
        else if (key == QLatin1String("back"))
            player.command({"script-message", "lumen-key",
                            player.sourceKind().endsWith(QLatin1String("vcd")) ? "return" : "popup"});
        else if (key == QLatin1String("playpause"))
            player.togglePause();
        else if (key == QLatin1String("play") || key == QLatin1String("pause"))
            player.setPaused(key == QLatin1String("pause"));
        else if (key == QLatin1String("stop"))
            player.stop();
        else if (key == QLatin1String("rewind") || key == QLatin1String("forward"))
            player.seek(key == QLatin1String("forward") ? 10 : -10, true);
        else if (key == QLatin1String("next"))
            player.nextChapter();
        else if (key == QLatin1String("prev"))
            player.prevChapter();
    });

    // Entwickler-Hilfe: LUMEN_CAST_AUTO=<Text> überträgt an den ersten Empfänger, dessen Kennung
    // oder Name den Text enthält, sobald er gefunden ist (z. B. "tv:" für die erste TV-App)
    const QString castAuto = qEnvironmentVariable("LUMEN_CAST_AUTO");
    if (!castAuto.isEmpty()) {
        cast.openDialog();
        QObject::connect(&cast, &CastManager::devicesChanged, &cast, [&cast, castAuto] {
            if (cast.active())
                return;
            for (const QVariant &v : cast.devices()) {
                const QVariantMap d = v.toMap();
                if (d.value("id").toString().contains(castAuto) || d.value("name").toString().contains(castAuto)) {
                    cast.start(d.value("id").toString());
                    return;
                }
            }
        });
    }

    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&] {
        cast.shutdown();
        serverStop();
        plugins.sendEvent(QStringLiteral("shutdown"));
        player.shutdown();
        displays.restoreAll();
    });

    qmlRegisterSingletonInstance("Lumen.Core", 1, 0, "Player", &player);
    qmlRegisterSingletonInstance("Lumen.Core", 1, 0, "Drives", &drives);
    qmlRegisterSingletonInstance("Lumen.Core", 1, 0, "Profiles", &profiles);
    qmlRegisterSingletonInstance("Lumen.Core", 1, 0, "Displays", &displays);
    qmlRegisterSingletonInstance("Lumen.Core", 1, 0, "Disc", &scanner);
    qmlRegisterSingletonInstance("Lumen.Core", 1, 0, "Nav", &nav);
    qmlRegisterSingletonInstance("Lumen.Core", 1, 0, "DvdNav", &dvd);
    qmlRegisterSingletonInstance("Lumen.Core", 1, 0, "VcdNav", &vcd);
    qmlRegisterSingletonInstance("Lumen.Core", 1, 0, "Dcp", &dcp);
    qmlRegisterSingletonInstance("Lumen.Core", 1, 0, "I18n", &i18n);
    qmlRegisterSingletonInstance("Lumen.Core", 1, 0, "Plugins", &plugins);
    qmlRegisterSingletonInstance("Lumen.Core", 1, 0, "Store", &store);
    qmlRegisterSingletonInstance("Lumen.Core", 1, 0, "Servers", &servers);
    qmlRegisterSingletonInstance("Lumen.Core", 1, 0, "Recent", &recent);
    qmlRegisterSingletonInstance("Lumen.Core", 1, 0, "Cast", &cast);
    Updater updater;
    qmlRegisterSingletonInstance("Lumen.Core", 1, 0, "Updater", &updater);
    QTimer::singleShot(4000, &updater, &Updater::checkAutomatically);
    QObject::connect(&i18n, &I18n::languageChanged, &profiles, &ProfileManager::retranslate);

    QQmlApplicationEngine engine;
    i18n.setEngine(&engine);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.loadFromModule("Lumen", "Main");

    // Mehrere Bildschirme: Steuerfenster auf den kleinsten, Wiedergabe auf den größten
    auto placeControl = [&] {
        if (engine.rootObjects().isEmpty())
            return;
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        const QString id = displays.controlOutput();
        if (!window || id.isEmpty())
            return;
        const QVariantMap o = displays.output(id);
        for (QScreen *s : QGuiApplication::screens()) {
            if (s->name() != id && s->geometry().topLeft() != QPoint(o.value("x").toInt(), o.value("y").toInt()))
                continue;
            if (window->screen() == s)
                return;
            const QRect area = s->availableGeometry();
            window->setScreen(s);
            window->resize(std::min(window->width(), int(area.width() * 0.95)), std::min(window->height(), int(area.height() * 0.95)));
            window->setPosition(area.center() - QPoint(window->width() / 2, window->height() / 2));
            return;
        }
    };
    placeControl();
    QObject::connect(&displays, &DisplayManager::outputsChanged, &app, [&] {
        placeControl();
        player.onOutputsChanged();
    });
    QObject::connect(qApp, &QGuiApplication::screenAdded, &displays, &DisplayManager::refresh);
    QObject::connect(qApp, &QGuiApplication::screenRemoved, &displays, &DisplayManager::refresh);

    // lumen [--menu] [--kdm <datei>] <datei|iso|ordner|laufwerk|dcp|cue>
    QStringList args = app.arguments().mid(1);
    const bool withMenu = args.removeAll(QStringLiteral("--menu")) > 0;
    for (int i = args.indexOf(QStringLiteral("--kdm")); i >= 0 && i + 1 < args.size(); i = args.indexOf(QStringLiteral("--kdm"))) {
        dcp.loadKdm(QUrl::fromLocalFile(QFileInfo(args.at(i + 1)).absoluteFilePath()));
        args.remove(i, 2);
    }
    if (!args.isEmpty()) {
        const QString target = args.constFirst();
        const QFileInfo fi(target);
        if (fi.exists()) {
            const QString kind = MpvController::detectKind(fi.absoluteFilePath());
            if (kind != QLatin1String("file") && kind != QLatin1String("dcp"))
                scanner.scan(fi.absoluteFilePath());
            player.openSource(fi.absoluteFilePath(), withMenu ? QStringLiteral("menu") : QStringLiteral("main"));
        } else {
            player.openLocation(target);
        }
    }

    // Entwickler-Hilfe: LUMEN_SNAPSHOT=<datei.png> speichert das Steuerfenster nach 2,5 s (LUMEN_SNAPSHOT_DELAY in ms)
    const QString snapshot = qEnvironmentVariable("LUMEN_SNAPSHOT");
    if (!snapshot.isEmpty() && !engine.rootObjects().isEmpty()) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        const int delay = qEnvironmentVariableIntValue("LUMEN_SNAPSHOT_DELAY");
        // LUMEN_SNAPSHOT_HELP=1: mit geöffnetem Hilfe-Dialog (für README-Bilder)
        if (qEnvironmentVariableIsSet("LUMEN_SNAPSHOT_HELP"))
            QMetaObject::invokeMethod(window, "showHelp");
        // LUMEN_SNAPSHOT_RECENT=<n>: n-ten Eintrag aus „Zuletzt gespielt“ öffnen (Test des Fortsetzens)
        if (qEnvironmentVariableIsSet("LUMEN_SNAPSHOT_RECENT"))
            QMetaObject::invokeMethod(window, "openRecentIndex", Q_ARG(QVariant, qEnvironmentVariableIntValue("LUMEN_SNAPSHOT_RECENT")));
        QTimer::singleShot(delay > 0 ? delay : 2500, window, [window, snapshot] { window->grabWindow().save(snapshot); });
    }

    return app.exec();
}
