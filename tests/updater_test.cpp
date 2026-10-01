// Update-Prüfung und Ein-Klick-Update (Windows) gegen einen lokalen Release-Server:
//
//   updater_test <python>
//
// Baut ein Release im GitHub-Format (latest.json, ZIP, SHA256SUMS.txt), prüft
// Erkennung, Download, Prüfsumme und Entpacken (Dry-Run ohne Neustart) sowie das
// Verwerfen bei falscher Prüfsumme und "aktuell" bei älteren Releases.
#include "Updater.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSettings>
#include <QTcpServer>
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
void write(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(data);
}
QByteArray read(const QString &path)
{
    QFile f(path);
    f.open(QIODevice::ReadOnly);
    return f.readAll();
}
void release(const QString &dir, int port, const QString &version, const QByteArray &sumsOverride = {})
{
    // wie im Release: ZIP (portable) und MSI (Installer) nebeneinander
    const QString asset = Updater::assetName(version), msi = Updater::assetName(version, true);
    const QByteArray zip = read(QDir(dir).filePath(QStringLiteral("pkg.zip")));
    const QByteArray msiData = "MSI-Paket " + version.toUtf8();
    write(QDir(dir).filePath(asset), zip);
    write(QDir(dir).filePath(msi), msiData);
    const QByteArray sum = sumsOverride.isEmpty() ? QCryptographicHash::hash(zip, QCryptographicHash::Sha256).toHex() : sumsOverride;
    write(QDir(dir).filePath(QStringLiteral("SHA256SUMS.txt")),
          sum + "  " + asset.toUtf8() + "\n" + QCryptographicHash::hash(msiData, QCryptographicHash::Sha256).toHex() + "  " + msi.toUtf8() + "\n");
    const QString base = QStringLiteral("http://127.0.0.1:%1/").arg(port);
    const QJsonObject latest{
        {"tag_name", "v" + version}, {"html_url", base + "release"}, {"body", "Neu: Test-Release"},
        {"assets", QJsonArray{QJsonObject{{"name", asset}, {"browser_download_url", base + asset}},
                              QJsonObject{{"name", msi}, {"browser_download_url", base + msi}},
                              QJsonObject{{"name", "SHA256SUMS.txt"}, {"browser_download_url", base + "SHA256SUMS.txt"}}}},
    };
    write(QDir(dir).filePath(QStringLiteral("latest.json")), QJsonDocument(latest).toJson());
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("LumenUpdaterTest"));
    QCoreApplication::setApplicationName(QStringLiteral("updater_test"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.2.0"));
    if (argc < 2) {
        std::fprintf(stderr, "usage: updater_test <python>\n");
        return 2;
    }
    QSettings().clear();

    QTemporaryDir root;
    const QString web = root.filePath(QStringLiteral("web"));
    // Paket wie im Release: Lumen/lumen.exe + Lumen/README.md
    write(root.filePath(QStringLiteral("pkg/Lumen/lumen.exe")), "neue Version");
    write(root.filePath(QStringLiteral("pkg/Lumen/README.md")), "Lumen");
    QDir().mkpath(web);
    check(QProcess::execute(QStringLiteral("tar"), {QStringLiteral("-a"), QStringLiteral("-cf"), QDir(web).filePath(QStringLiteral("pkg.zip")),
                                                    QStringLiteral("-C"), root.filePath(QStringLiteral("pkg")), QStringLiteral("Lumen")}) == 0,
          QStringLiteral("ZIP-Paket erstellt"));

    int port = 0;
    {
        QTcpServer probe;
        probe.listen(QHostAddress::LocalHost, 0);
        port = probe.serverPort();
    }
    QProcess server;
    server.start(QString::fromLocal8Bit(argv[1]), {QStringLiteral("-m"), QStringLiteral("http.server"), QString::number(port),
                                                   QStringLiteral("--bind"), QStringLiteral("127.0.0.1"), QStringLiteral("--directory"), web});
    check(server.waitForStarted(5000), QStringLiteral("Release-Server gestartet"));
    waitFor([] { return false; }, 1500);

    Updater up;
    up.setApiUrl(QUrl(QStringLiteral("http://127.0.0.1:%1/latest.json").arg(port)));
    const QString installTo = root.filePath(QStringLiteral("install"));
    QDir().mkpath(installTo);
    up.setInstallDir(installTo);
    up.setDryRun(true);

    // 1. älteres Release -> aktuell
    release(web, port, QStringLiteral("0.1.5"));
    up.check();
    waitFor([&] { return !up.busy() && !up.status().contains(QLatin1String("Suche")); }, 10000);
    check(!up.available(), QStringLiteral("älteres Release: kein Update (%1)").arg(up.status()));

    // 2. neueres Release -> verfügbar, installierbar
    release(web, port, QStringLiteral("9.9.0"));
    up.check();
    waitFor([&] { return !up.busy() && up.available(); }, 10000);
    check(up.available() && up.latestVersion() == QLatin1String("9.9.0") && up.canInstall() && up.notes().contains(QLatin1String("Test-Release")),
          QStringLiteral("Update 9.9.0 erkannt, installierbar"));
    bool ready = false;
    QObject::connect(&up, &Updater::readyToRestart, [&] { ready = true; });
    up.install();
    waitFor([&] { return ready || (!up.busy() && up.status().contains(QLatin1String("stimmt"))); }, 20000);
    check(ready && read(QDir(up.preparedDir()).filePath(QStringLiteral("lumen.exe"))) == "neue Version",
          QStringLiteral("heruntergeladen, Prüfsumme geprüft, entpackt (%1)").arg(up.status()));

    // 3. falsche Prüfsumme -> verworfen
    release(web, port, QStringLiteral("9.9.1"), QByteArray(64, 'a'));
    up.check();
    waitFor([&] { return !up.busy() && up.latestVersion() == QLatin1String("9.9.1"); }, 10000);
    ready = false;
    up.install();
    waitFor([&] { return ready || (!up.busy() && up.status().contains(QLatin1String("stimmt nicht"))); }, 20000);
    check(!ready && up.status().contains(QLatin1String("stimmt nicht")), QStringLiteral("falsche Prüfsumme verworfen (%1)").arg(up.status()));

    // 4. per MSI installierte Kopie -> lädt das MSI statt des ZIP
    check(!up.msiInstalled(), QStringLiteral("portable Kopie: kein MSI"));
    write(QDir(installTo).filePath(QStringLiteral("install-type.txt")), "msi\r\n");
    check(up.msiInstalled(), QStringLiteral("install-type.txt: per MSI installiert"));
    release(web, port, QStringLiteral("9.9.2"));
    up.check();
    waitFor([&] { return !up.busy() && up.latestVersion() == QLatin1String("9.9.2"); }, 10000);
    ready = false;
    up.install();
    waitFor([&] { return ready || (!up.busy() && up.status().contains(QLatin1String("stimmt"))); }, 20000);
    check(ready && up.preparedDir().endsWith(QLatin1String("Lumen-9.9.2-windows-x64.msi")) && read(up.preparedDir()) == "MSI-Paket 9.9.2",
          QStringLiteral("MSI heruntergeladen und geprüft (%1)").arg(QFileInfo(up.preparedDir()).fileName()));

    server.kill();
    server.waitForFinished(3000);
    QSettings().clear();
    std::printf("%s (%d Fehler)\n", g_fail ? "FEHLGESCHLAGEN" : "BESTANDEN", g_fail);
    return g_fail ? 1 : 0;
}
