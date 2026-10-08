#include "OsBridge.h"

#include "MpvController.h"

#include <QCollator>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QPointer>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QSysInfo>
#include <QThreadPool>

#ifndef Q_OS_WIN
#include <unistd.h>
#endif

namespace {

const char kUpdateFile[] = "/run/lumenos/update.json";

QString kindOfVolume(const QStorageInfo &v)
{
    const QByteArray fs = v.fileSystemType().toLower();
    if (fs.startsWith("udf") || fs == "iso9660" || fs == "cd9660" || fs == "cdfs")
        return QStringLiteral("disc");
    if (fs == "cifs" || fs == "smb3" || fs == "smbfs" || fs.startsWith("nfs") || fs == "afpfs" || fs == "webdav" || fs.startsWith("fuse.ssh"))
        return QStringLiteral("network");
    return QStringLiteral("usb");
}

} // namespace

OsBridge::OsBridge(bool active, QObject *parent)
    : QObject(parent)
    , m_active(active)
{
    if (!m_active)
        return;
    m_poll.setInterval(4000);
    connect(&m_poll, &QTimer::timeout, this, &OsBridge::refresh);
    m_poll.start();
    refresh();
}

QString OsBridge::helper() const
{
    const QString custom = qEnvironmentVariable("LUMEN_OS_ADMIN"); // Tests, Entwicklung
    if (!custom.isEmpty())
        return custom;
    for (const QString &dir : {QCoreApplication::applicationDirPath() + QStringLiteral("/../share/lumen/os"), QStringLiteral("/usr/share/lumen/os")}) {
        const QString file = QDir::cleanPath(dir + QStringLiteral("/lumenos-admin"));
        if (QFileInfo(file).isExecutable())
            return file;
    }
    return {};
}

bool OsBridge::system() const
{
    static const bool there = !helper().isEmpty() && (QFileInfo::exists(QStringLiteral("/etc/lumenos")) || qEnvironmentVariableIsSet("LUMEN_OS_ADMIN"));
    return there;
}

bool OsBridge::isMedia(const QString &fileName)
{
    static const QSet<QString> ext = {"mkv", "mp4", "m4v", "mov", "avi", "m2ts", "mts", "ts", "webm", "mpg", "mpeg", "vob", "wmv", "flv", "ogv",
                                      "iso", "mxf", "mka", "flac", "mp3", "m4a", "ogg", "opus", "wav", "aac", "ac3", "dts", "evo", "divx", "3gp"};
    return ext.contains(QFileInfo(fileName).suffix().toLower());
}

void OsBridge::refresh()
{
    QVariantList places;
    const auto add = [&places](const QString &name, const QString &path, const QString &kind, qint64 free, qint64 total) {
        places.append(QVariantMap{{"name", name}, {"path", path}, {"kind", kind}, {"free", free}, {"total", total}});
    };
    // Interner Speicher: der Filmordner des Nutzers
    const QString movies = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (!movies.isEmpty()) {
        QDir().mkpath(movies);
        const QStorageInfo s(movies);
        add(QString(), movies, QStringLiteral("internal"), s.bytesAvailable(), s.bytesTotal());
    }
    // Eingehängtes: USB-Datenträger, Netzlaufwerke, Daten-Discs
    for (const QStorageInfo &v : QStorageInfo::mountedVolumes()) {
        if (!v.isValid() || !v.isReady())
            continue;
        const QString root = v.rootPath();
#if defined(Q_OS_MACOS)
        if (!root.startsWith(QLatin1String("/Volumes/")) || v.isRoot())
            continue;
#elif defined(Q_OS_WIN)
        if (v.isRoot())
            continue;
#else
        if (!root.startsWith(QLatin1String("/media/")) && !root.startsWith(QLatin1String("/run/media/")) && !root.startsWith(QLatin1String("/mnt/")))
            continue;
#endif
        const QString kind = kindOfVolume(v);
        // Film-Discs spielt die Seite „Disc“
        if (kind == QLatin1String("disc") && MpvController::detectKind(root) != QLatin1String("file"))
            continue;
        const QString name = !v.name().isEmpty() ? v.name() : QFileInfo(root).fileName();
        add(name, root, kind, v.bytesAvailable(), v.bytesTotal());
    }
    if (places != m_places) {
        m_places = places;
        emit placesChanged();
    }
    readUpdate();
    emit infoChanged();
}

QVariantMap OsBridge::info() const
{
    QStringList addresses;
    for (const QNetworkInterface &i : QNetworkInterface::allInterfaces()) {
        if (!(i.flags() & QNetworkInterface::IsUp) || (i.flags() & QNetworkInterface::IsLoopBack))
            continue;
        for (const QNetworkAddressEntry &a : i.addressEntries()) {
            if (a.ip().protocol() == QAbstractSocket::IPv4Protocol)
                addresses << a.ip().toString();
        }
    }
    return {{"version", QCoreApplication::applicationVersion()},
            {"hostname", QSysInfo::machineHostName()},
            {"system", QSysInfo::prettyProductName()},
            {"addresses", addresses}};
}

void OsBridge::readUpdate()
{
    QFile f(qEnvironmentVariable("LUMEN_OS_UPDATE_FILE", QString::fromLatin1(kUpdateFile)));
    const QVariantMap now = f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object().toVariantMap() : QVariantMap();
    if (now != m_update) {
        m_update = now;
        emit updateChanged();
    }
}

void OsBridge::admin(const QStringList &arguments, const QJSValue &done)
{
    const QString program = helper();
    const auto answer = [done](int code, const QString &out, const QString &err) {
        if (done.isCallable())
            QJSValue(done).call({code, out, err});
    };
    if (program.isEmpty()) {
        answer(127, QString(), QStringLiteral("lumenos-admin"));
        return;
    }
    auto *process = new QProcess(this);
    QStringList args = arguments;
#ifndef Q_OS_WIN
    // als gewöhnlicher Nutzer über sudo (die Regel dafür legt LumenOS an)
    const bool root = ::geteuid() == 0 || qEnvironmentVariableIsSet("LUMEN_OS_ADMIN");
#else
    const bool root = true;
#endif
    if (root) {
        process->setProgram(program);
    } else {
        process->setProgram(QStringLiteral("sudo"));
        args.prepend(program);
        args.prepend(QStringLiteral("-n"));
    }
    process->setArguments(args);
    connect(process, &QProcess::finished, this, [this, process, answer](int code, QProcess::ExitStatus status) {
        answer(status == QProcess::NormalExit ? code : 126, QString::fromUtf8(process->readAllStandardOutput()),
               QString::fromUtf8(process->readAllStandardError()));
        process->deleteLater();
        refresh();
    });
    connect(process, &QProcess::errorOccurred, this, [process, answer](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        answer(127, QString(), process->errorString());
        process->deleteLater();
    });
    process->start();
}

void OsBridge::setBusy(bool busy)
{
    if (!m_active)
        return;
    // im Laufzeitordner der Sitzung; lumenos-update liest die Datei
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (dir.isEmpty())
        return;
    QFile f(dir + QStringLiteral("/lumen-os.state"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(busy ? "busy\n" : "idle\n");
}

QString OsBridge::parentFolder(const QString &path) const
{
    return QFileInfo(QDir::cleanPath(path)).absolutePath();
}

void OsBridge::listFolder(const QString &path)
{
    QPointer<OsBridge> self(this);
    QThreadPool::globalInstance()->start([self, path] {
        QVariantList dirs, files;
        const QFileInfoList all = QDir(path).entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot, QDir::NoSort);
        QList<QFileInfo> sorted = all;
        QCollator collator;
        collator.setNumericMode(true); // "Folge 2" vor "Folge 10"
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        std::sort(sorted.begin(), sorted.end(), [&](const QFileInfo &a, const QFileInfo &b) { return collator.compare(a.fileName(), b.fileName()) < 0; });
        for (const QFileInfo &fi : std::as_const(sorted)) {
            if (fi.fileName().startsWith(QLatin1Char('.')) || fi.fileName().startsWith(QLatin1Char('$')))
                continue;
            if (fi.isDir()) {
                // Ein Ordner mit Disc-Struktur (BDMV, VIDEO_TS …) ist ein Film, kein Ordner
                const QString kind = MpvController::detectKind(fi.absoluteFilePath());
                dirs.append(QVariantMap{{"name", fi.fileName()}, {"path", fi.absoluteFilePath()}, {"dir", kind == QLatin1String("file")},
                                        {"kind", kind == QLatin1String("file") ? QStringLiteral("folder") : kind}, {"size", 0}});
            } else if (isMedia(fi.fileName())) {
                files.append(QVariantMap{{"name", fi.completeBaseName()}, {"path", fi.absoluteFilePath()}, {"dir", false},
                                         {"kind", fi.suffix().toLower() == QLatin1String("iso") ? QStringLiteral("image") : QStringLiteral("file")},
                                         {"size", fi.size()}});
            }
        }
        if (!self)
            return;
        QMetaObject::invokeMethod(self, [self, path, entries = dirs + files] {
            if (self)
                emit self->folderListed(path, entries);
        }, Qt::QueuedConnection);
    });
}
