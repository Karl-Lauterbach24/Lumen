#include "BdjSetup.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

#include <cstdio>
#include <utility>

#ifdef LUMEN_HAVE_BLURAY
#include <libbluray/bluray.h>
#endif

#ifdef LUMEN_HAVE_BLURAY
// Java-Laufzeit für BD-J-Menüs: libbluray nimmt JAVA_HOME und sucht sonst selbst. Die Pakete für
// macOS und Windows bringen eine kleine Laufzeit mit (Ordner "jre", tools/make_jre.py); sie geht
// einem installierten Java vor – mit ihr sind die Menüs geprüft. Fehlt sie (eigener Build): Unter
// macOS findet libbluray nur das alte Browser-Plugin (sein Aufruf von /usr/libexec/java_home
// scheitert in 1.5.0), unter Windows nur Registry-Einträge, die neuere OpenJDK-Installer nicht mehr
// anlegen; dort sucht Lumen selbst. Linux kennt die Ordner der Distributionen.
static void findJavaRuntime()
{
    if (qEnvironmentVariableIsSet("JAVA_HOME"))
        return;
    QStringList homes;
    QStringList libs;
#if defined(Q_OS_MACOS)
    libs = {QStringLiteral("lib/server/libjvm.dylib"), QStringLiteral("jre/lib/server/libjvm.dylib")};
    homes << QCoreApplication::applicationDirPath() + QStringLiteral("/../Resources/jre");
    if (QFileInfo::exists(homes.first() + QLatin1Char('/') + libs.first())) {
        qputenv("JAVA_HOME", QDir::toNativeSeparators(QDir(homes.first()).absolutePath()).toLocal8Bit());
        return;
    }
    QProcess p;
    p.start(QStringLiteral("/usr/libexec/java_home"), QStringList{});
    if (p.waitForFinished(3000) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0)
        homes << QString::fromLocal8Bit(p.readAllStandardOutput()).trimmed();
    else
        p.kill();
    // Homebrew trägt sein OpenJDK nicht bei java_home ein
    homes << QStringLiteral("/opt/homebrew/opt/openjdk/libexec/openjdk.jdk/Contents/Home")
          << QStringLiteral("/usr/local/opt/openjdk/libexec/openjdk.jdk/Contents/Home");
#elif defined(Q_OS_WIN)
    libs = {QStringLiteral("bin/server/jvm.dll"), QStringLiteral("jre/bin/server/jvm.dll")};
    // die mitgelieferte Laufzeit: Ordner "jre" neben dem Programm
    homes << QCoreApplication::applicationDirPath() + QStringLiteral("/jre");
    bool registry = false;
    for (const char *key : {"Java Runtime Environment", "JRE", "JDK"}) {
        const QSettings reg(QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\JavaSoft\\") + QLatin1String(key),
                            QSettings::NativeFormat);
        registry = registry || reg.contains(QStringLiteral("CurrentVersion"));
    }
    // was libbluray in der Registry selbst findet, bleibt ihm überlassen
    if (!registry) {
        const QString programs = qEnvironmentVariable("ProgramW6432", qEnvironmentVariable("ProgramFiles"));
        for (const char *vendor :
             {"Eclipse Adoptium", "Java", "Microsoft", "Zulu", "BellSoft", "Amazon Corretto", "OpenJDK"}) {
            const QDir dir(programs + QLatin1Char('/') + QLatin1String(vendor));
            // neueste Version zuerst
            const QStringList entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed);
            for (const QString &e : entries)
                homes << dir.absoluteFilePath(e);
        }
    }
#else
    // Linux: die Pakete bringen die Laufzeit unter <prefix>/lib/lumen/jre mit (tools/build_deps.sh);
    // ohne sie kennt libbluray die Ordner der Distributionen
    libs = {QStringLiteral("lib/server/libjvm.so")};
    homes << QCoreApplication::applicationDirPath() + QStringLiteral("/../lib/lumen/jre");
    if (qEnvironmentVariableIsSet("LUMEN_DEPS_JRE")) // Tests aus dem Build-Ordner
        homes.prepend(qEnvironmentVariable("LUMEN_DEPS_JRE"));
#endif
    for (const QString &home : std::as_const(homes)) {
        if (home.isEmpty())
            continue;
        for (const QString &lib : std::as_const(libs)) {
            if (!QFileInfo::exists(home + QLatin1Char('/') + lib))
                continue;
            qputenv("JAVA_HOME", QDir::toNativeSeparators(QDir(home).absolutePath()).toLocal8Bit());
            return;
        }
    }
}

#endif

// BD-J-Menüs: libbluray lädt sein Java-Archiv nur in der eigenen Version
// (libbluray-j2se-<Version>.jar, daneben libbluray-awt-j2se-<Version>.jar). Ein Ordner "bdj" mit
// genau diesem Archiv – im Datenordner, neben dem Programm oder im Paket – wird libbluray über
// LIBBLURAY_CP genannt. Fehlt es, bleibt die Variable frei: mit ihr sucht libbluray nirgends sonst,
// ohne sie in /usr/share/java (Linux: Paket libbluray-bdj der Distribution).
void BdjSetup::prepare(const QStringList &extraDirs)
{
#ifdef LUMEN_HAVE_BLURAY
    if (qEnvironmentVariableIsSet("LIBBLURAY_CP")) {
        findJavaRuntime();
        return;
    }
    int major = 0, minor = 0, micro = 0;
    bd_get_version(&major, &minor, &micro);
    const QString jar = QStringLiteral("/libbluray-j2se-%1.%2.%3.jar").arg(major).arg(minor).arg(micro);
    const QString app = QCoreApplication::applicationDirPath();
    const QStringList dirs = extraDirs
        + QStringList{QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/bdj"),
                      app + QStringLiteral("/bdj"), app + QStringLiteral("/../Resources/bdj"),
                      app + QStringLiteral("/../share/lumen/bdj")};
    for (const QString &dir : dirs) {
        if (!QFileInfo(dir + jar).isReadable())
            continue;
        // mit Trennzeichen am Ende: ein Ordner, libbluray hängt den Namen seiner Version an
        qputenv("LIBBLURAY_CP", QDir::toNativeSeparators(QDir(dir).absolutePath() + QLatin1Char('/')).toLocal8Bit());
        break;
    }
    findJavaRuntime();
    if (qEnvironmentVariableIsSet("LUMEN_MPV_LOG"))
        std::fprintf(stderr, "[lumen] BD-J: libbluray %d.%d.%d, archive %s, Java %s\n", major, minor, micro,
                     qEnvironmentVariableIsSet("LIBBLURAY_CP") ? qgetenv("LIBBLURAY_CP").constData() : "left to libbluray",
                     qEnvironmentVariableIsSet("JAVA_HOME") ? qgetenv("JAVA_HOME").constData() : "left to libbluray");
#else
    Q_UNUSED(extraDirs)
#endif
}
