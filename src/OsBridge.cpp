#include "OsBridge.h"

#include "MpvController.h"

#include <QCollator>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QNetworkInterface>
#include <QPointer>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QSysInfo>
#include <QUrl>

#include <cmath>
#include <thread>
#include <vector>

#ifndef Q_OS_WIN
#include <unistd.h>
#endif
#ifdef Q_OS_MACOS
#include <sys/mount.h>
#include <sys/param.h>
#endif

namespace {

const char kUpdateFile[] = "/run/lumenos/update.json";

// Ein Datenträger, der so lange nicht antwortet, wird übergangen (ms)
const int kSilentAfterMs = 6000;

QString kindOfVolume(const QByteArray &fileSystem)
{
    const QByteArray fs = fileSystem.toLower();
    if (fs.startsWith("udf") || fs == "iso9660" || fs == "cd9660" || fs == "cdfs")
        return QStringLiteral("disc");
    if (fs == "cifs" || fs == "smb3" || fs == "smbfs" || fs.startsWith("nfs") || fs == "afpfs" || fs == "webdav" || fs.startsWith("fuse.ssh"))
        return QStringLiteral("network");
    return QStringLiteral("usb");
}

struct Mount
{
    QString root;
    QByteArray fileSystem;
};

// Was eingehängt ist – aus der Tabelle des Kerns gelesen, ohne einen der Datenträger anzufassen:
// ein Laufwerk, das hängt, oder ein Netzlaufwerk ohne Netz hält diese Liste nicht auf.
QList<Mount> mountTable()
{
    QList<Mount> out;
#if defined(Q_OS_LINUX)
    QFile f(QStringLiteral("/proc/self/mountinfo"));
    if (!f.open(QIODevice::ReadOnly))
        return out;
    const QList<QByteArray> lines = f.readAll().split('\n');
    for (const QByteArray &line : lines) {
        // "36 35 98:0 / /media/Filme rw,noatime - ext4 /dev/sda1 rw"
        const qsizetype dash = line.indexOf(" - ");
        const QList<QByteArray> head = line.left(qMax<qsizetype>(dash, 0)).split(' ');
        if (dash < 0 || head.size() < 5)
            continue;
        // Leerzeichen und Ähnliches im Pfad stehen dort oktal ("\040")
        QByteArray path;
        const QByteArray raw = head.at(4);
        for (qsizetype i = 0; i < raw.size(); ++i) {
            if (raw.at(i) == '\\' && i + 3 < raw.size()) {
                path.append(char(raw.mid(i + 1, 3).toInt(nullptr, 8)));
                i += 3;
            } else {
                path.append(raw.at(i));
            }
        }
        out.append({QFile::decodeName(path), line.mid(dash + 3).split(' ').value(0)});
    }
#elif defined(Q_OS_MACOS)
    const int count = getfsstat(nullptr, 0, MNT_NOWAIT);
    if (count <= 0)
        return out;
    std::vector<struct statfs> list(size_t(count) + 8);
    const int got = getfsstat(list.data(), int(list.size() * sizeof(struct statfs)), MNT_NOWAIT);
    for (int i = 0; i < got; ++i)
        out.append({QFile::decodeName(list[size_t(i)].f_mntonname), QByteArray(list[size_t(i)].f_fstypename)});
#else
    for (const QStorageInfo &v : QStorageInfo::mountedVolumes())
        out.append({v.rootPath(), v.fileSystemType()});
#endif
    return out;
}

// Gehört ein eingehängter Ort in die Mediathek (Stick, Platte, Netzlaufwerk, Daten-Disc)?
bool isPlace(const QString &root)
{
#if defined(Q_OS_MACOS)
    return root.startsWith(QLatin1String("/Volumes/"));
#elif defined(Q_OS_WIN)
    return QDir::cleanPath(root).compare(QDir::cleanPath(QDir::rootPath()), Qt::CaseInsensitive) != 0;
#else
    return root.startsWith(QLatin1String("/media/")) || root.startsWith(QLatin1String("/run/media/")) || root.startsWith(QLatin1String("/mnt/"));
#endif
}

} // namespace

// Die Suche nach Datenträgern läuft in einem eigenen Faden; das hier teilt er mit der Oberfläche.
struct OsBridge::Scan
{
    QMutex mutex;
    bool running = false;
    int generation = 0;   // nur das Ergebnis der jüngsten Suche zählt
    QString at;           // der Datenträger, den die Suche gerade befragt
    QElapsedTimer since;  // seit wann
    QSet<QString> silent; // antworten nicht: werden übergangen, bis sie es wieder tun
};

OsBridge::OsBridge(bool active, QObject *parent)
    : QObject(parent)
    , m_active(active)
    , m_scan(std::make_shared<Scan>())
{
    if (!m_active)
        return;
    m_poll.setInterval(4000);
    connect(&m_poll, &QTimer::timeout, this, &OsBridge::refresh);
    m_poll.start();
    // Was die Sitzung oder der Nutzer vorgibt (/etc/lumenos/session.env), bleibt, wie es ist
    m_ownAacs = !qEnvironmentVariableIsSet("LIBAACS_PATH") && !qEnvironmentVariableIsSet("LIBBDPLUS_PATH");
    m_ownDvdcss = !qEnvironmentVariableIsSet("LUMEN_DVDCSS_LIBRARY");
    useDiscLibraries();
    refresh();
}

// LumenOS: die Disc-Bibliotheken, die der Nutzer auf dem Gerät eingerichtet hat (Einstellungen, „Discs“).
// Lumen bringt keine mit; hier wird nur gesagt, wo die des Systems liegen – und zwar jedes Mal, wenn
// sich daran etwas geändert haben kann, damit Lumen dafür nicht neu starten muss:
//   - DVD: libdvdcss des Systems (Lumens eigener Platzhalter gleichen Namens lädt sie nach),
//   - Blu-ray: mit einer Schlüsseldatei liest die libaacs des Systems sie von selbst; ohne eine nimmt
//     MakeMKVs Bibliothek deren Platz ein, wenn MakeMKV eingerichtet ist.
void OsBridge::useDiscLibraries()
{
    if (!m_active || !system())
        return;
#if defined(Q_OS_LINUX)
    if (m_ownDvdcss) {
        QByteArray library;
        QStringList folders = {QStringLiteral("/usr/lib"), QStringLiteral("/usr/local/lib")};
        const QFileInfoList multiarch = QDir(QStringLiteral("/usr/lib")).entryInfoList({QStringLiteral("*-linux-gnu*")}, QDir::Dirs);
        for (const QFileInfo &f : multiarch)
            folders.prepend(f.absoluteFilePath());
        for (const QString &folder : std::as_const(folders)) {
            if (QFileInfo::exists(folder + QStringLiteral("/libdvdcss.so.2"))) {
                library = QFile::encodeName(folder + QStringLiteral("/libdvdcss.so.2"));
                break;
            }
        }
        if (library != m_dvdcss) {
            m_dvdcss = library;
            if (library.isEmpty())
                qunsetenv("LUMEN_DVDCSS_LIBRARY");
            else
                qputenv("LUMEN_DVDCSS_LIBRARY", library);
        }
    }
    if (m_ownAacs) {
        const bool keys = QFileInfo(QDir::homePath() + QStringLiteral("/.config/aacs/KEYDB.cfg")).size() > 0;
        const QByteArray library = !keys && QFileInfo::exists(QStringLiteral("/usr/lib/libmmbd.so.0")) ? QByteArrayLiteral("/usr/lib/libmmbd") : QByteArray();
        if (library != m_aacs) {
            m_aacs = library;
            if (library.isEmpty()) {
                qunsetenv("LIBAACS_PATH");
                qunsetenv("LIBBDPLUS_PATH");
            } else {
                qputenv("LIBAACS_PATH", library);
                qputenv("LIBBDPLUS_PATH", library);
            }
        }
    }
#endif
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

bool OsBridge::live() const
{
    return QFileInfo::exists(QStringLiteral("/run/live/medium/live/filesystem.squashfs")) || qEnvironmentVariableIsSet("LUMEN_OS_LIVE");
}

QString OsBridge::overlayDir()
{
    static const QString dir = [] {
        const QString d = qEnvironmentVariable("LUMEN_OS_OVERLAY", QStringLiteral("/var/lib/lumenos/overlay"));
        if (!QFileInfo::exists(d + QStringLiteral("/qml/OsMain.qml")))
            return QString();
        QFile api(d + QStringLiteral("/API"));
        if (!api.open(QIODevice::ReadOnly) || api.readAll().trimmed().toInt() > kApi)
            return QString(); // verlangt mehr, als dieses Programm kann
        return d;
    }();
    return dir;
}

QString OsBridge::overlayVersion() const
{
    QFile f(overlayDir() + QStringLiteral("/VERSION"));
    return !overlayDir().isEmpty() && f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).trimmed() : QString();
}

QString OsBridge::icon(const QString &name) const
{
    const QString file = overlayDir() + QStringLiteral("/icons/") + name + QStringLiteral(".svg");
    if (!overlayDir().isEmpty() && QFileInfo::exists(file))
        return QUrl::fromLocalFile(file).toString();
    return QStringLiteral("qrc:/qt/qml/Lumen/icons/") + name + QStringLiteral(".svg");
}

bool OsBridge::isMedia(const QString &fileName)
{
    static const QSet<QString> ext = {"mkv", "mp4", "m4v", "mov", "avi", "m2ts", "mts", "ts", "webm", "mpg", "mpeg", "vob", "wmv", "flv", "ogv",
                                      "iso", "mxf", "mka", "flac", "mp3", "m4a", "ogg", "opus", "wav", "aac", "ac3", "dts", "evo", "divx", "3gp"};
    return ext.contains(QFileInfo(fileName).suffix().toLower());
}

void OsBridge::refresh()
{
    readUpdate();
    // (nur melden, was sich geändert hat: an diesen Angaben hängen Seiten, die sich sonst alle paar
    // Sekunden neu aufbauten)
    const QVariantMap now = info();
    if (now != m_info) {
        m_info = now;
        emit infoChanged();
    }
    scanPlaces();
}

// Die Orte, an denen Filme liegen. Kein Datenträger wird von der Oberfläche aus angefasst: ein
// USB-Laufwerk, das hängt, oder ein Netzlaufwerk, dessen Rechner aus ist, lässt eine solche Anfrage
// Minuten oder für immer warten. Die Suche läuft deshalb in einem eigenen Faden; bleibt sie an einem
// Datenträger stehen, wird der übergangen und ohne ihn neu gesucht.
void OsBridge::scanPlaces()
{
    const std::shared_ptr<Scan> scan = m_scan;
    int generation = 0;
    QSet<QString> skip;
    {
        QMutexLocker lock(&scan->mutex);
        if (scan->running) {
            if (scan->at.isEmpty() || scan->since.elapsed() < kSilentAfterMs)
                return;
            qWarning("LumenOS: %s antwortet nicht und wird übergangen", qPrintable(scan->at));
            scan->silent.insert(scan->at);
            scan->at.clear();
        }
        scan->running = true;
        generation = ++scan->generation;
        skip = scan->silent;
    }
    // Interner Speicher: der Filmordner des Nutzers
    const QString movies = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (!movies.isEmpty())
        QDir().mkpath(movies);
    QPointer<OsBridge> self(this);
    std::thread([scan, generation, skip, movies, self] {
        QVariantList places;
        const auto add = [&places](const QString &name, const QString &path, const QString &kind, qint64 free, qint64 total) {
            places.append(QVariantMap{{"name", name}, {"path", path}, {"kind", kind}, {"free", free}, {"total", total}});
        };
        if (!movies.isEmpty()) {
            const QStorageInfo s(movies);
            add(QString(), movies, QStringLiteral("internal"), s.bytesAvailable(), s.bytesTotal());
        }
        // Eingehängtes: USB-Datenträger, Netzlaufwerke, Daten-Discs
        QList<Mount> mounts;
        {
            // Ein Ort kann zweimal in der Tabelle stehen: der Platzhalter des Systems, der ein Netzlaufwerk
            // beim ersten Zugriff einhängt (autofs), und darüber das Netzlaufwerk selbst. Der letzte gilt.
            QHash<QString, int> at;
            const QList<Mount> all = mountTable();
            for (const Mount &m : all) {
                if (at.contains(m.root)) {
                    mounts[at.value(m.root)] = m;
                } else {
                    at.insert(m.root, int(mounts.size()));
                    mounts.append(m);
                }
            }
        }
        {
            // was nicht mehr eingehängt ist, muss auch nicht mehr übergangen werden
            QSet<QString> mounted;
            for (const Mount &m : mounts)
                mounted.insert(m.root);
            QMutexLocker lock(&scan->mutex);
            scan->silent.intersect(mounted);
        }
        for (const Mount &m : mounts) {
            if (!isPlace(m.root) || skip.contains(m.root))
                continue;
            {
                QMutexLocker lock(&scan->mutex);
                if (scan->generation != generation)
                    return;
                scan->at = m.root;
                scan->since.start();
            }
            // --- ab hier wird der Datenträger gefragt: das kann hängen bleiben
            const QStorageInfo v(m.root);
            // (ein Platzhalter hängt bei dieser Frage sein Netzlaufwerk ein; bleibt er einer, ist es nicht da)
            const QByteArray fileSystem = m.fileSystem == "autofs" ? v.fileSystemType() : m.fileSystem;
            const bool usable = v.isValid() && v.isReady() && fileSystem != "autofs";
            const QString kind = kindOfVolume(fileSystem);
            // Film-Discs spielt die Seite „Disc“
            const bool film = usable && kind == QLatin1String("disc") && MpvController::detectKind(m.root) != QLatin1String("file");
            const qint64 free = usable ? v.bytesAvailable() : 0, total = usable ? v.bytesTotal() : 0;
            // ---
            {
                QMutexLocker lock(&scan->mutex);
                if (scan->generation != generation) {
                    // Die Suche wurde ohne diesen Datenträger neu begonnen; jetzt hat er geantwortet
                    scan->silent.remove(m.root);
                    return;
                }
                scan->at.clear();
            }
            if (usable && !film)
                add(QFileInfo(m.root).fileName(), m.root, kind, free, total);
        }
        if (!QCoreApplication::instance())
            return;
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, scan, generation, places] {
            {
                QMutexLocker lock(&scan->mutex);
                if (scan->generation != generation)
                    return;
                scan->running = false;
            }
            if (self && places != self->m_places) {
                self->m_places = places;
                emit self->placesChanged();
            }
        }, Qt::QueuedConnection);
    }).detach();
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
    // Was am System baut oder lädt, soll eine Aktualisierung nicht mittendrin abreißen (sie startet
    // Lumen neu und beendet damit auch dessen Hilfsprogramm): so lange gilt Lumen als beschäftigt.
    static const QSet<QString> lengthy = {"install-to", "install-makemkv", "install-dvdcss", "remove-dvdcss", "keydb-fetch", "makemkv-key", "makemkv-betakey"};
    const bool counts = lengthy.contains(arguments.value(0));
    const auto ended = [this, counts] {
        if (counts) {
            --m_working;
            tellBusy();
        }
    };
    connect(process, &QProcess::finished, this, [this, process, answer, ended](int code, QProcess::ExitStatus status) {
        ended();
        answer(status == QProcess::NormalExit ? code : 126, QString::fromUtf8(process->readAllStandardOutput()),
               QString::fromUtf8(process->readAllStandardError()));
        process->deleteLater();
        useDiscLibraries(); // der Auftrag kann eine eingerichtet oder entfernt haben
        refresh();
    });
    connect(process, &QProcess::errorOccurred, this, [process, answer, ended](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        ended();
        answer(127, QString(), process->errorString());
        process->deleteLater();
    });
    if (counts) {
        ++m_working;
        tellBusy();
    }
    process->start();
}

void OsBridge::setBusy(bool busy)
{
    m_playing = busy;
    tellBusy();
}

void OsBridge::tellBusy()
{
    if (!m_active)
        return;
    // im Laufzeitordner der Sitzung; lumenos-update liest die Datei
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (dir.isEmpty())
        return;
    QFile f(dir + QStringLiteral("/lumen-os.state"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(m_playing || m_working > 0 ? "busy\n" : "idle\n");
}

QString OsBridge::parentFolder(const QString &path) const
{
    return QFileInfo(QDir::cleanPath(path)).absolutePath();
}

void OsBridge::listFolder(const QString &path)
{
    // Ein eigener Faden, nicht der gemeinsame Vorrat: ein Ordner auf einem Datenträger, der nicht
    // antwortet, hielte dort auf Dauer einen Platz besetzt (den braucht die Suche nach Discs).
    QPointer<OsBridge> self(this);
    std::thread([self, path] {
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
        if (!QCoreApplication::instance())
            return;
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, path, entries = dirs + files] {
            if (self)
                emit self->folderListed(path, entries);
        }, Qt::QueuedConnection);
    }).detach();
}

QImage OsBackdrop::requestImage(const QString &, QSize *size, const QSize &requestedSize)
{
    // nicht größer als Full HD gerechnet: der Verlauf ist weich, ein 4K-Bildschirm vergrößert ihn
    QSize s = requestedSize.isEmpty() ? QSize(1920, 1080) : requestedSize;
    if (s.width() > 1920 || s.height() > 1080)
        s.scale(1920, 1080, Qt::KeepAspectRatio);
    QImage image(s, QImage::Format_RGB32);
    struct Glow
    {
        double x, y, radius; // Mitte (Anteil an Breite und Höhe), Halbmesser (Anteil an der Höhe)
        double color[3];
        double alpha;        // Deckkraft in der Mitte
    };
    static const Glow glows[] = {{0.12, 0.02, 0.85, {96, 122, 255}, 0.20}, {0.95, 1.02, 0.75, {160, 96, 255}, 0.11}};
    static const double top[3] = {13, 16, 23}, bottom[3] = {7, 8, 11};
    quint32 seed = 0x9e3779b9u;
    const auto noise = [&seed] { // -1 … 1, zur Mitte hin häufiger
        const auto next = [&seed] {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            return double(seed & 0xffff) / 65536.0;
        };
        const double a = next();
        return a + next() - 1.0;
    };
    const int w = s.width(), h = s.height();
    for (int y = 0; y < h; ++y) {
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        const double t = h > 1 ? double(y) / (h - 1) : 0.0;
        for (int x = 0; x < w; ++x) {
            double c[3];
            for (int i = 0; i < 3; ++i)
                c[i] = top[i] + (bottom[i] - top[i]) * t;
            for (const Glow &g : glows) {
                const double dx = x - g.x * w, dy = y - g.y * h;
                const double far = std::sqrt(dx * dx + dy * dy) / (g.radius * h);
                if (far >= 1.0)
                    continue;
                const double a = g.alpha * std::pow(1.0 - far, 1.7);
                for (int i = 0; i < 3; ++i)
                    c[i] += (g.color[i] - c[i]) * a;
            }
            int out[3];
            for (int i = 0; i < 3; ++i)
                out[i] = qBound(0, int(std::lround(c[i] + noise())), 255);
            line[x] = qRgb(out[0], out[1], out[2]);
        }
    }
    if (size)
        *size = s;
    return image;
}
