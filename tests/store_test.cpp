// Plugin-Store gegen eine lokale Quelle prüfen (Ordner mit index.json, z. B. ein
// Checkout von github.com/Karl-Lauterbach24/Lumen-Plugins):
//
//   store_test <store-ordner>
//
// Quellen lesen -> Plugin installieren (SHA-256 geprüft) -> installiertes Plugin
// laden (native Bibliothek, DCP-Schlüssel aus dessen Schlüsseldatei) -> manipulierte
// Datei wird abgelehnt -> Update-Erkennung -> Entfernen.
#include "DcpStream.h"
#include "PluginManager.h"
#include "PluginStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>

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
QVariantMap entry(PluginStore &s, const QString &source, const QString &id)
{
    for (const QVariant &v : s.available())
        if (v.toMap().value("source") == source && v.toMap().value("id") == id)
            return v.toMap();
    return {};
}
QVariantMap plugin(PluginManager &pm, const QString &id)
{
    for (const QVariant &v : pm.plugins())
        if (v.toMap().value("id") == id)
            return v.toMap();
    return {};
}
bool copyTree(const QString &from, const QString &to)
{
    QDir().mkpath(to);
    for (const QFileInfo &fi : QDir(from).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden)) {
        if (fi.fileName() == QLatin1String(".git"))
            continue;
        const QString target = QDir(to).filePath(fi.fileName());
        if (fi.isDir() ? !copyTree(fi.filePath(), target) : !QFile::copy(fi.filePath(), target))
            return false;
    }
    return true;
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("LumenStoreTest"));
    QCoreApplication::setApplicationName(QStringLiteral("store_test"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.2.0"));
    if (argc < 2) {
        std::fprintf(stderr, "usage: store_test <store-folder>\n");
        return 2;
    }
    const QString source = QDir(QString::fromLocal8Bit(argv[1])).absolutePath();
    const QString id = QStringLiteral("my-aacs-handler");
    qputenv("LUMEN_PLUGIN_PATH", ""); // nur der Nutzerordner dieses Tests

    PluginManager pm;
    QDir(pm.userDir()).removeRecursively(); // Testordner (eigene Organisation) leeren
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)).removeRecursively();
    QSettings().clear();
    pm.discover();

    PluginStore store(&pm);
    check(PluginStore::indexUrl(QStringLiteral("owner/repo")).toString() == QLatin1String("https://raw.githubusercontent.com/owner/repo/main/index.json"),
          QStringLiteral("Kurzform owner/repo -> raw index.json"));
    check(PluginStore::indexUrl(QStringLiteral("https://github.com/o/r/tree/dev/store")).toString()
              == QLatin1String("https://raw.githubusercontent.com/o/r/dev/store/index.json"),
          QStringLiteral("GitHub-URL mit Branch/Unterordner"));
    check(store.addSource(source), QStringLiteral("eigene Quelle hinzugefügt"));
    check(waitFor([&] { return !store.busy() && !entry(store, source, id).isEmpty(); }, 20000), QStringLiteral("Index der Quelle gelesen"));
    std::printf("     Quellen:\n");
    for (const QVariant &v : store.sources())
        std::printf("       %s: %s\n", qPrintable(v.toMap().value("source").toString()),
                    qPrintable(v.toMap().value("error").toString().isEmpty() ? QStringLiteral("%1 Plugins").arg(v.toMap().value("count").toInt())
                                                                            : v.toMap().value("error").toString()));
    const QVariantMap e = entry(store, source, id);
    check(e.value("supported").toBool() && e.value("native").toBool() && e.value("installed").toString().isEmpty(),
          QStringLiteral("Plugin angeboten (nativ, für dieses System, nicht installiert)"));

    // Installation
    bool done = false, ok = false;
    QString message;
    QObject::connect(&store, &PluginStore::installed, [&](const QString &, bool success, const QString &msg) {
        done = true;
        ok = success;
        message = msg;
    });
    store.install(source, id);
    bool finished = waitFor([&] { return done; }, 20000);
    check(finished && ok, QStringLiteral("installiert %1").arg(message));
    const QDir target(QDir(pm.userDir()).filePath(id));
    check(target.exists(QStringLiteral("plugin.json")) && target.exists(QStringLiteral("scripts/status.lua"))
              && target.exists(QStringLiteral(".lumen-store.json")),
          QStringLiteral("Dateien + Store-Marker im Nutzerordner"));
    check(!target.exists(QStringLiteral("src")), QStringLiteral("Quelltext nicht mitinstalliert"));
    const QVariantMap installed = plugin(pm, id);
    check(!installed.isEmpty() && !installed.value("enabled").toBool(), QStringLiteral("erscheint im Plugin-Tab, zunächst deaktiviert"));
    // Version des Plugins im Store (ändert sich mit dessen Releases)
    const QString storeVersion = entry(store, source, id).value("version").toString();
    check(!storeVersion.isEmpty() && entry(store, source, id).value("installed") == storeVersion,
          QStringLiteral("Store zeigt installierte Version %1").arg(storeVersion));

    // Manipulierte Quelle: Datei passt nicht zur Prüfsumme
    QTemporaryDir tmp;
    copyTree(source, tmp.path());
    {
        QFile f(tmp.filePath(QStringLiteral("plugins/%1/scripts/status.lua").arg(id)));
        f.open(QIODevice::Append);
        f.write("\n-- manipuliert\n");
    }
    // und eine höhere Version im Index (Update-Erkennung)
    {
        QFile f(tmp.filePath(QStringLiteral("index.json")));
        f.open(QIODevice::ReadOnly);
        QByteArray json = f.readAll();
        f.close();
        json.replace("\"version\": \"" + storeVersion.toUtf8() + "\"", "\"version\": \"99.0.0\"");
        f.open(QIODevice::WriteOnly | QIODevice::Truncate);
        f.write(json);
    }
    check(store.addSource(tmp.path()), QStringLiteral("zweite (manipulierte) Quelle"));
    check(waitFor([&] { return !store.busy() && !entry(store, tmp.path(), id).isEmpty(); }, 20000), QStringLiteral("Index gelesen"));
    check(entry(store, tmp.path(), id).value("update").toBool(), QStringLiteral("Update %1 -> 99.0.0 erkannt").arg(storeVersion));
    done = false;
    store.install(tmp.path(), id);
    finished = waitFor([&] { return done; }, 20000);
    check(finished && !ok && message.contains(QLatin1String("status.lua")),
          QStringLiteral("manipulierte Datei abgelehnt: %1").arg(message));
    check(target.exists(QStringLiteral("plugin.json")), QStringLiteral("bestehende Installation bleibt erhalten"));

    // Entfernen (Plugin ist in diesem Prozess nicht mehr geladen)
    pm.setEnabled(id, false);
    check(store.uninstall(id) && !target.exists(), QStringLiteral("entfernt"));
    check(plugin(pm, id).isEmpty(), QStringLiteral("aus der Plugin-Liste verschwunden"));

    // Erneut installieren und laden
    done = false;
    store.install(source, id);
    finished = waitFor([&] { return done; }, 20000);
    check(finished && ok, QStringLiteral("erneut installiert %1").arg(message));
    // Laden: Schlüsseldatei im Konfigurationsordner des Plugins
    const QString configDir = pm.userDir() + QStringLiteral("/config/") + id;
    QDir().mkpath(configDir);
    {
        QFile keys(configDir + QStringLiteral("/dcp-keys.txt"));
        check(keys.open(QIODevice::WriteOnly), QStringLiteral("Schlüsseldatei angelegt"));
        keys.write("# test\n7762f777-5e04-472c-ba5e-b872bdb33542 02f2606491e04888de2ad43d1010aaee\n");
    }
    pm.setEnabled(id, true);
    PluginManager pm2;
    pm2.loadEnabled();
    waitFor([&] { return !plugin(pm2, id).value("actions").toList().isEmpty(); }, 3000);
    const QVariantMap loaded = plugin(pm2, id);
    std::printf("     loaded=%d error=%s status=%s\n", loaded.value("loaded").toBool(), qPrintable(loaded.value("error").toString()),
                qPrintable(loaded.value("status").toString()));
    check(loaded.value("loaded").toBool() && loaded.value("error").toString().isEmpty(),
          QStringLiteral("geladen, obwohl libaacs/libbdplus fehlen (optional)"));
    check(loaded.value("actions").toList().value(0).toMap().value("id") == QLatin1String("refresh_keys"), QStringLiteral("Aktion „Refresh DCP Keys“"));
    check(Dcp::providedKey(QStringLiteral("urn:uuid:7762f777-5e04-472c-ba5e-b872bdb33542")).toHex()
              == QByteArray("02f2606491e04888de2ad43d1010aaee"),
          QStringLiteral("DCP-Schlüssel aus dem installierten Plugin"));
    check(pm2.mpvOptions().value("scripts").toString().contains(QLatin1String("status.lua")), QStringLiteral("mpv-Skript eingebunden"));
    pm2.unloadAll();


    QSettings().clear(); // Plugin-Ordner bleibt bis zum nächsten Lauf (DLL ist geladen)
    std::printf("%s (%d Fehler)\n", g_fail ? "FEHLGESCHLAGEN" : "BESTANDEN", g_fail);
    return g_fail ? 1 : 0;
}
