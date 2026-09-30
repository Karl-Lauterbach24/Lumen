#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>
#include <QUrl>

#include <clocale>

#include "BlurayNav.h"
#include "DcpManager.h"
#include "DiscScanner.h"
#include "DvdNav.h"
#include "MediaServers.h"
#include "I18n.h"
#include "DisplayManager.h"
#include "DriveManager.h"
#include "MpvController.h"
#include "PluginManager.h"
#include "PluginStore.h"
#include "ProfileManager.h"
#include "VcdNav.h"

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
    QObject::connect(&scanner, &DiscScanner::infoChanged, &plugins, [&] {
        const QVariantMap i = scanner.info();
        if (!i.value("device").toString().isEmpty())
            plugins.sendEvent(QStringLiteral("disc"), {{"device", i.value("device")}, {"kind", i.value("kind")},
                                                       {"label", i.value("label", i.value("title"))}});
    });
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

    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&] {
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
        QTimer::singleShot(delay > 0 ? delay : 2500, window, [window, snapshot] { window->grabWindow().save(snapshot); });
    }

    return app.exec();
}
