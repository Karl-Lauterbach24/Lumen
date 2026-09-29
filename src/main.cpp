#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>

#include <clocale>

#include "BlurayNav.h"
#include "DcpManager.h"
#include "DiscScanner.h"
#include "DvdNav.h"
#include "DisplayManager.h"
#include "DriveManager.h"
#include "MpvController.h"
#include "ProfileManager.h"

int main(int argc, char *argv[])
{
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QGuiApplication app(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("Lumen"));
    QGuiApplication::setApplicationName(QStringLiteral("Lumen"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Lumen"));
    QGuiApplication::setApplicationVersion(QStringLiteral(LUMEN_VERSION));

    // libmpv verlangt den C-Locale für Zahlen
    std::setlocale(LC_NUMERIC, "C");
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    DisplayManager displays;
    DriveManager drives;
    ProfileManager profiles;
    DiscScanner scanner;
    BlurayNav nav;
    DvdNav dvd;
    MpvController player(&displays, &nav);
    player.setDvdNav(&dvd);

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
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&] {
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
    qmlRegisterSingletonInstance("Lumen.Core", 1, 0, "Dcp", &dcp);

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.loadFromModule("Lumen", "Main");

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
