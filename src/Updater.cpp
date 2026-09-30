#include "Updater.h"
#include "Tr.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QSysInfo>
#include <QVersionNumber>

namespace {

QNetworkRequest request(const QUrl &url)
{
    QNetworkRequest r(url);
    r.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Lumen/%1").arg(QCoreApplication::applicationVersion()));
    r.setRawHeader("Accept", "application/vnd.github+json");
    r.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    return r;
}

QString quoted(const QString &path)
{
    return QLatin1Char('"') + QDir::toNativeSeparators(path) + QLatin1Char('"');
}

} // namespace

Updater::Updater(QObject *parent)
    : QObject(parent)
{
    m_api = QUrl(qEnvironmentVariable("LUMEN_UPDATE_API",
                                      QStringLiteral("https://api.github.com/repos/%1/releases/latest").arg(repository())));
}

QString Updater::assetName(const QString &version)
{
#if defined(Q_OS_WIN)
    return QStringLiteral("Lumen-%1-windows-x64.zip").arg(version);
#elif defined(Q_OS_MACOS)
    if (QSysInfo::currentCpuArchitecture() != QLatin1String("arm64"))
        return {}; // bisher nur Apple-Silicon-Builds
    return QStringLiteral("Lumen-%1-macos-arm64.dmg").arg(version);
#else
    Q_UNUSED(version)
    return {};
#endif
}

bool Updater::autoCheck() const
{
    return QSettings().value(QStringLiteral("update/auto"), true).toBool();
}

void Updater::setAutoCheck(bool on)
{
    QSettings().setValue(QStringLiteral("update/auto"), on);
    emit changed();
}

void Updater::setStatus(const QString &s)
{
    m_status = s;
    emit changed();
}

void Updater::fail(const QString &message)
{
    m_busy = false;
    m_progress = 0;
    setStatus(message);
}

void Updater::checkAutomatically()
{
    if (!autoCheck())
        return;
    const QDateTime last = QSettings().value(QStringLiteral("update/lastCheck")).toDateTime();
    if (last.isValid() && last.secsTo(QDateTime::currentDateTimeUtc()) < 20 * 3600)
        return;
    check();
}

void Updater::check()
{
    m_busy = true;
    setStatus(LTR("Suche nach Updates …"));
    QNetworkReply *reply = m_net.get(request(m_api));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        m_busy = false;
        if (reply->error() != QNetworkReply::NoError) {
            fail(LTR("Update-Prüfung fehlgeschlagen: %1").arg(reply->errorString()));
            return;
        }
        QSettings().setValue(QStringLiteral("update/lastCheck"), QDateTime::currentDateTimeUtc());
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        QString tag = o.value("tag_name").toString();
        if (tag.startsWith(QLatin1Char('v')))
            tag.remove(0, 1);
        m_latest = tag;
        m_page = o.value("html_url").toString();
        m_notes = o.value("body").toString().left(2000);
        m_assetName = assetName(tag);
        m_asset = QUrl();
        m_sums = QUrl();
        for (const QJsonValue &a : o.value("assets").toArray()) {
            const QJsonObject ao = a.toObject();
            if (!m_assetName.isEmpty() && ao.value("name").toString() == m_assetName)
                m_asset = QUrl(ao.value("browser_download_url").toString());
            if (ao.value("name").toString() == QLatin1String("SHA256SUMS.txt"))
                m_sums = QUrl(ao.value("browser_download_url").toString());
        }
        if (!m_sums.isValid())
            m_asset = QUrl(); // ohne Prüfsummen keine automatische Installation
        m_available = !tag.isEmpty()
                      && QVersionNumber::fromString(tag) > QVersionNumber::fromString(QCoreApplication::applicationVersion());
        setStatus(m_available ? LTR("Lumen %1 ist verfügbar").arg(m_latest)
                              : LTR("Lumen ist aktuell (%1)").arg(QCoreApplication::applicationVersion()));
    });
}

void Updater::openPage()
{
    QDesktopServices::openUrl(QUrl(m_page.isEmpty() ? QStringLiteral("https://github.com/%1/releases").arg(repository()) : m_page));
}

void Updater::install()
{
    if (!canInstall()) {
        openPage();
        return;
    }
    m_busy = true;
    m_progress = 0;
    setStatus(LTR("Lade Prüfsummen …"));
    QNetworkReply *sums = m_net.get(request(m_sums));
    connect(sums, &QNetworkReply::finished, this, [this, sums] {
        sums->deleteLater();
        QByteArray expected;
        for (const QByteArray &line : sums->readAll().split('\n')) {
            const QList<QByteArray> parts = line.simplified().split(' ');
            if (parts.size() == 2 && QString::fromUtf8(parts[1]).remove(QLatin1Char('*')) == m_assetName)
                expected = parts[0].toLower();
        }
        if (sums->error() != QNetworkReply::NoError || expected.size() != 64) {
            fail(LTR("Keine Prüfsumme für %1 – Update abgebrochen").arg(m_assetName));
            return;
        }
        setStatus(LTR("Lade Lumen %1 …").arg(m_latest));
        QNetworkReply *reply = m_net.get(request(m_asset));
        connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 done, qint64 total) {
            m_progress = total > 0 ? double(done) / double(total) : 0;
            emit changed();
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply, expected] {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                fail(LTR("Download fehlgeschlagen: %1").arg(reply->errorString()));
                return;
            }
            verifyAndApply(reply->readAll(), expected);
        });
    });
}

void Updater::verifyAndApply(const QByteArray &data, const QByteArray &expectedSha)
{
    if (QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex() != expectedSha) {
        fail(LTR("Prüfsumme stimmt nicht – Update verworfen"));
        return;
    }
    const QString archive = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath(m_assetName);
    QFile f(archive);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size()) {
        fail(LTR("Download nicht speicherbar"));
        return;
    }
    f.close();
    setStatus(LTR("Installiere …"));
    if (apply(archive) && !m_dryRun)
        QCoreApplication::quit(); // das Hilfsskript ersetzt die Dateien und startet neu
}

QString Updater::installDir() const
{
    if (!m_installDir.isEmpty())
        return m_installDir;
#ifdef Q_OS_MACOS
    return QDir(QCoreApplication::applicationDirPath() + QStringLiteral("/../..")).absolutePath(); // Lumen.app
#else
    return QCoreApplication::applicationDirPath();
#endif
}

bool Updater::apply(const QString &archive)
{
    const QString staging = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath(QStringLiteral("lumen-update-") + m_latest);
    QDir(staging).removeRecursively();
    QDir().mkpath(staging);
    const QString target = installDir();
    const QString pid = QString::number(QCoreApplication::applicationPid());

#if defined(Q_OS_WIN)
    // Windows 10/11 bringen bsdtar mit, das auch ZIP entpackt
    if (QProcess::execute(QStringLiteral("tar"), {QStringLiteral("-xf"), archive, QStringLiteral("-C"), staging}) != 0) {
        fail(LTR("Update-Archiv nicht entpackbar"));
        return false;
    }
    QString source;
    QDirIterator it(staging, {QStringLiteral("lumen.exe")}, QDir::Files, QDirIterator::Subdirectories);
    if (it.hasNext())
        source = QFileInfo(it.next()).absolutePath();
    if (source.isEmpty()) {
        fail(LTR("Update-Archiv enthält kein lumen.exe"));
        return false;
    }
    QFile probe(QDir(target).filePath(QStringLiteral(".lumen-update-probe")));
    if (!probe.open(QIODevice::WriteOnly)) {
        fail(LTR("Keine Schreibrechte in %1 – bitte die neue Version manuell installieren").arg(QDir::toNativeSeparators(target)));
        openPage();
        return false;
    }
    probe.close();
    probe.remove();
    m_prepared = source;
    if (m_dryRun) {
        m_busy = false;
        setStatus(LTR("Update bereit"));
        emit readyToRestart();
        return true;
    }
    const QString script = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath(QStringLiteral("lumen-update.cmd"));
    QFile s(script);
    if (!s.open(QIODevice::WriteOnly)) {
        fail(LTR("Update-Skript nicht schreibbar"));
        return false;
    }
    s.write(QStringLiteral("@echo off\r\n"
                           ":wait\r\n"
                           "tasklist /FI \"PID eq %1\" 2>NUL | find \"%1\" >NUL && (timeout /t 1 /nobreak >NUL & goto wait)\r\n"
                           "robocopy %2 %3 /E /NFL /NDL /NJH /NJS /R:5 /W:1 >NUL\r\n"
                           "start \"\" %4\r\n"
                           "rd /s /q %5\r\n")
                .arg(pid, quoted(source), quoted(target), quoted(QDir(target).filePath(QStringLiteral("lumen.exe"))), quoted(staging))
                .toLocal8Bit());
    s.close();
    return QProcess::startDetached(QStringLiteral("cmd.exe"), {QStringLiteral("/c"), QDir::toNativeSeparators(script)});
#elif defined(Q_OS_MACOS)
    const QString mount = staging + QStringLiteral("/mnt");
    QDir().mkpath(mount);
    if (QProcess::execute(QStringLiteral("hdiutil"), {QStringLiteral("attach"), QStringLiteral("-nobrowse"), QStringLiteral("-readonly"),
                                                      QStringLiteral("-mountpoint"), mount, archive}) != 0) {
        fail(LTR("Update-Image nicht einhängbar"));
        return false;
    }
    const QString app = staging + QStringLiteral("/Lumen.app");
    const int copied = QProcess::execute(QStringLiteral("ditto"), {mount + QStringLiteral("/Lumen.app"), app});
    QProcess::execute(QStringLiteral("hdiutil"), {QStringLiteral("detach"), mount});
    if (copied != 0 || !QFileInfo::exists(app + QStringLiteral("/Contents/MacOS/Lumen"))) {
        fail(LTR("Update-Image enthält kein Lumen.app"));
        return false;
    }
    m_prepared = app;
    if (m_dryRun) {
        m_busy = false;
        setStatus(LTR("Update bereit"));
        emit readyToRestart();
        return true;
    }
    const QString script = staging + QStringLiteral("/update.sh");
    QFile s(script);
    if (!s.open(QIODevice::WriteOnly)) {
        fail(LTR("Update-Skript nicht schreibbar"));
        return false;
    }
    s.write(QStringLiteral("#!/bin/sh\n"
                           "while kill -0 %1 2>/dev/null; do sleep 1; done\n"
                           "rm -rf %3 && ditto %2 %3 && xattr -dr com.apple.quarantine %3\n"
                           "open %3\n"
                           "rm -rf %4\n")
                .arg(pid, quoted(app), quoted(target), quoted(staging))
                .toUtf8());
    s.close();
    return QProcess::startDetached(QStringLiteral("/bin/sh"), {script});
#else
    Q_UNUSED(archive)
    Q_UNUSED(target)
    Q_UNUSED(pid)
    openPage();
    return false;
#endif
}
