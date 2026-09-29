#include "DriveManager.h"
#include "DcpPackage.h"
#include "DvdNav.h"
#include "OpticalMedia.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QPointer>
#include <QProcess>
#include <QSet>
#include <QStorageInfo>
#include <QThreadPool>

#ifdef Q_OS_WIN
#include <windows.h>
#include <winioctl.h>
#endif

#ifdef Q_OS_LINUX
#include <fcntl.h>
#include <linux/cdrom.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace {

QString kindLabel(const QVariantMap &d)
{
    const QString k = d.value("kind").toString();
    if (k == QLatin1String("bluray"))
        return d.value("isUhd").toBool() ? QStringLiteral("UHD Blu-ray") : QStringLiteral("Blu-ray");
    static const QHash<QString, QString> labels = {
        {"dvd", "DVD-Video"}, {"hddvd", "HD DVD"}, {"vcd", "Video-CD"}, {"svcd", "Super Video-CD"},
        {"cdda", "Audio-CD"}, {"dcp", "DCP"},
    };
    return labels.value(k);
}

// Disc-/Paketstruktur erkennen. Blu-ray: Header von BDMV/index.bdmv
// ("INDX0100/0200" = Blu-ray, "INDX0300" = UHD)
QVariantMap inspectRoot(const QString &root)
{
    QVariantMap m;
    const QString bdmv = QDir(root).filePath(QStringLiteral("BDMV"));
    QFile index(bdmv + QStringLiteral("/index.bdmv"));
    m["isBluray"] = false;
    if (index.open(QIODevice::ReadOnly)) {
        const QByteArray head = index.read(8);
        m["isBluray"] = head.startsWith("INDX");
        m["isUhd"] = head == "INDX0300";
        m["is3d"] = QFileInfo::exists(bdmv + QStringLiteral("/STREAM/SSIF"));
        m["hasAacs"] = QFileInfo::exists(QDir(root).filePath(QStringLiteral("AACS")));
        if (m["isBluray"].toBool())
            m["kind"] = QStringLiteral("bluray");
        return m;
    }
    QString kind;
    if (DvdNav::isDvd(root))
        kind = QStringLiteral("dvd");
    else if (Dcp::isDcp(root))
        kind = QStringLiteral("dcp");
    else
        kind = Optical::detect(root);
    if (!kind.isEmpty())
        m["kind"] = kind;
    return m;
}

#ifdef Q_OS_WIN
void queryModel(wchar_t letter, QVariantMap &d)
{
    wchar_t path[] = {L'\\', L'\\', L'.', L'\\', letter, L':', 0};
    HANDLE h = CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return;
    STORAGE_PROPERTY_QUERY q{};
    q.PropertyId = StorageDeviceProperty;
    q.QueryType = PropertyStandardQuery;
    alignas(8) char buf[1024] = {};
    DWORD got = 0;
    if (DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof(q), buf, sizeof(buf), &got, nullptr)) {
        auto *desc = reinterpret_cast<STORAGE_DEVICE_DESCRIPTOR *>(buf);
        auto str = [&](DWORD off) {
            return off && off < got ? QString::fromLatin1(buf + off).trimmed() : QString();
        };
        d["vendor"] = str(desc->VendorIdOffset);
        d["model"] = str(desc->ProductIdOffset);
        d["firmware"] = str(desc->ProductRevisionOffset);
    }
    CloseHandle(h);
}
#endif

#ifdef Q_OS_LINUX
QString readSys(const QString &p)
{
    QFile f(p);
    return f.open(QIODevice::ReadOnly) ? QString::fromLatin1(f.readAll()).trimmed() : QString();
}
#endif

} // namespace

DriveManager::DriveManager(QObject *parent)
    : QObject(parent)
{
    m_poll.setInterval(3000);
    connect(&m_poll, &QTimer::timeout, this, &DriveManager::refresh);
    m_poll.start();
    refresh();
}

DriveManager::~DriveManager()
{
    QThreadPool::globalInstance()->waitForDone(5000);
}

void DriveManager::refresh()
{
    if (m_scanning)
        return;
    m_scanning = true;
    emit scanningChanged();
    // Optische Laufwerke können beim Anlaufen Sekunden blockieren -> Worker-Thread
    QPointer<DriveManager> self(this);
    QThreadPool::globalInstance()->start([self] {
        QVariantList list = scan();
        if (!self)
            return;
        QMetaObject::invokeMethod(self, [self, list] {
            if (self)
                self->applyScan(list);
        }, Qt::QueuedConnection);
    });
}

void DriveManager::applyScan(const QVariantList &list)
{
    m_scanning = false;
    emit scanningChanged();
    if (list == m_drives)
        return;

    QSet<QString> before;
    for (const auto &v : m_drives) {
        const QVariantMap d = v.toMap();
        if (!d.value("kind").toString().isEmpty())
            before.insert(d.value("path").toString());
    }
    m_drives = list;
    emit drivesChanged();
    for (const auto &v : m_drives) {
        const QVariantMap d = v.toMap();
        // Neu eingelegte Disc (DCP-Festplatten starten nicht von selbst)
        const QString kind = d.value("kind").toString();
        if (!kind.isEmpty() && kind != QLatin1String("dcp") && !before.contains(d.value("path").toString()))
            emit discInserted(d);
    }
}

QVariantMap DriveManager::inspect(const QString &path) const
{
    QVariantMap m = inspectRoot(path);
    m["device"] = path;
    m["path"] = path;
    m["label"] = QFileInfo(path).fileName();
    return m;
}

QVariantList DriveManager::scan()
{
    QVariantList out;
    QSet<QString> seenRoots;

#ifdef Q_OS_WIN
    const DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (!(mask & (1u << i)))
            continue;
        const wchar_t letter = wchar_t(L'A' + i);
        wchar_t root[] = {letter, L':', L'\\', 0};
        if (GetDriveTypeW(root) != DRIVE_CDROM)
            continue;

        QVariantMap d;
        d["optical"] = true;
        d["device"] = QStringLiteral("%1:\\").arg(QChar(letter));
        d["path"] = d["device"];

        const UINT oldMode = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
        wchar_t label[MAX_PATH + 1] = {};
        wchar_t fs[MAX_PATH + 1] = {};
        const bool hasDisc = GetVolumeInformationW(root, label, MAX_PATH + 1, nullptr, nullptr, nullptr, fs, MAX_PATH + 1);
        SetErrorMode(oldMode);

        d["hasDisc"] = hasDisc;
        d["label"] = QString::fromWCharArray(label);
        d["filesystem"] = QString::fromWCharArray(fs);
        queryModel(letter, d);
        if (hasDisc) {
            const QVariantMap info = inspectRoot(d["path"].toString());
            for (auto it = info.cbegin(); it != info.cend(); ++it)
                d.insert(it.key(), it.value());
        }
        seenRoots.insert(QDir::cleanPath(d["path"].toString()).toLower());
        out.append(d);
    }
#elif defined(Q_OS_LINUX)
    const QStringList blocks = QDir(QStringLiteral("/sys/block")).entryList({QStringLiteral("sr*")}, QDir::Dirs | QDir::System);
    const auto volumes = QStorageInfo::mountedVolumes();
    for (const QString &b : blocks) {
        QVariantMap d;
        d["optical"] = true;
        d["device"] = QStringLiteral("/dev/") + b;
        d["vendor"] = readSys(QStringLiteral("/sys/block/%1/device/vendor").arg(b));
        d["model"] = readSys(QStringLiteral("/sys/block/%1/device/model").arg(b));
        d["firmware"] = readSys(QStringLiteral("/sys/block/%1/device/rev").arg(b));
        d["hasDisc"] = readSys(QStringLiteral("/sys/block/%1/size").arg(b)).toLongLong() > 0;
        d["path"] = d["device"]; // libbluray liest ungemountete Discs direkt via UDF
        for (const QStorageInfo &v : volumes) {
            if (QString::fromLocal8Bit(v.device()) == d["device"].toString()) {
                d["path"] = v.rootPath();
                d["label"] = v.name();
                d["filesystem"] = QString::fromLatin1(v.fileSystemType());
                const QVariantMap info = inspectRoot(v.rootPath());
                for (auto it = info.cbegin(); it != info.cend(); ++it)
                    d.insert(it.key(), it.value());
                seenRoots.insert(QDir::cleanPath(v.rootPath()));
                break;
            }
        }
        if (!d.contains("isBluray") && d["hasDisc"].toBool()) {
            d["isBluray"] = true; // ungemountet: Annahme, libbluray prüft beim Öffnen
            d["kind"] = QStringLiteral("bluray");
        }
        out.append(d);
    }
#endif

    // Alle Plattformen: eingehängte Volumes mit Disc-Struktur (macOS-Discs, ISO-Mounts)
    // und DCP-Festplatten (Paket im Wurzelordner oder eine Ebene tiefer)
    const QString systemRoot = QDir::cleanPath(QStorageInfo::root().rootPath()).toLower();
    for (const QStorageInfo &v : QStorageInfo::mountedVolumes()) {
        if (!v.isValid() || !v.isReady())
            continue;
        const QString root = v.rootPath();
        const QString key = QDir::cleanPath(root);
#ifdef Q_OS_WIN
        if (seenRoots.contains(key.toLower()))
            continue;
#else
        if (seenRoots.contains(key) || root == QLatin1String("/") || root.startsWith(QLatin1String("/boot"))
            || root.startsWith(QLatin1String("/snap")) || root.startsWith(QLatin1String("/sys")) || root.startsWith(QLatin1String("/proc")))
            continue;
#endif
        const QVariantMap info = inspectRoot(root);
        auto addEntry = [&](const QVariantMap &inf, const QString &path, const QString &label) {
            QVariantMap d = inf;
            const QByteArray fs = v.fileSystemType();
            d["optical"] = fs.startsWith("udf") || fs == "UDF" || fs == "CDFS" || fs == "iso9660";
            d["device"] = path;
            d["path"] = path;
            d["hasDisc"] = true;
            d["label"] = label;
            d["filesystem"] = QString::fromLatin1(fs);
            d["mountDevice"] = QString::fromLocal8Bit(v.device());
            out.append(d);
        };
        if (!info.value("kind").toString().isEmpty()) {
            addEntry(info, root, v.name());
            continue;
        }
        if (key.toLower() == systemRoot)
            continue;
        // DCP-Auslieferung (Kino-Festplatte): ein Ordner je Paket
        const QStringList dirs = QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (int i = 0; i < dirs.size() && i < 64; ++i) {
            const QString sub = QDir(root).filePath(dirs[i]);
            if (Dcp::isDcp(sub))
                addEntry(QVariantMap{{"kind", QStringLiteral("dcp")}}, sub, dirs[i]);
        }
    }

    for (auto &v : out) {
        QVariantMap d = v.toMap();
        const QString hw = QStringList{d.value("vendor").toString(), d.value("model").toString()}.join(QLatin1Char(' ')).trimmed();
        QString disc;
        const QString kl = kindLabel(d);
        if (!d.value("hasDisc").toBool())
            disc = QStringLiteral("leer");
        else if (!kl.isEmpty())
            disc = kl + QStringLiteral(" · ") + d.value("label").toString();
        else
            disc = d.value("label").toString();
        d["kindLabel"] = kl;
        d["title"] = QStringLiteral("%1  %2").arg(QDir::toNativeSeparators(d.value("device").toString()), disc);
        d["hardware"] = hw;
        v = d;
    }
    return out;
}

bool DriveManager::eject(const QString &device)
{
#ifdef Q_OS_WIN
    if (device.size() < 2 || device.at(1) != QLatin1Char(':'))
        return false;
    const std::wstring path = QStringLiteral("\\\\.\\%1:").arg(device.at(0)).toStdWString();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    DWORD got = 0;
    const bool ok = DeviceIoControl(h, IOCTL_STORAGE_EJECT_MEDIA, nullptr, 0, nullptr, 0, &got, nullptr);
    CloseHandle(h);
    QTimer::singleShot(1500, this, &DriveManager::refresh);
    return ok;
#elif defined(Q_OS_LINUX)
    if (device.startsWith(QLatin1String("/dev/"))) {
        const int fd = ::open(device.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK);
        if (fd < 0)
            return QProcess::startDetached(QStringLiteral("eject"), {device});
        const bool ok = ::ioctl(fd, CDROMEJECT, 0) == 0;
        ::close(fd);
        if (!ok) // gemountet -> udisks/eject übernimmt das Aushängen
            return QProcess::startDetached(QStringLiteral("eject"), {device});
        return true;
    }
    return QProcess::startDetached(QStringLiteral("eject"), {device});
#elif defined(Q_OS_MACOS)
    return QProcess::startDetached(QStringLiteral("drutil"), {QStringLiteral("tray"), QStringLiteral("eject")});
#else
    Q_UNUSED(device)
    return false;
#endif
}
