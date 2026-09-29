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
#include "DiscScanner.h"
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
    MpvController player(&displays, &nav);

    player.initialize(profiles.currentProfile());

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

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.loadFromModule("Lumen", "Main");

    // lumen [--menu] <datei|iso|ordner|laufwerk>
    QStringList args = app.arguments().mid(1);
    const bool withMenu = args.removeAll(QStringLiteral("--menu")) > 0;
    if (!args.isEmpty()) {
        const QString target = args.constFirst();
        const QFileInfo fi(target);
        const bool disc = fi.suffix().compare(QLatin1String("iso"), Qt::CaseInsensitive) == 0
                          || QFileInfo::exists(target + QStringLiteral("/BDMV/index.bdmv"));
        if (disc)
            scanner.scan(fi.absoluteFilePath());
        if (disc && withMenu)
            player.openDiscMenu(fi.absoluteFilePath());
        else
            player.openLocation(target);
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
