#include "PluginStore.h"
#include "Platform.h"
#include "PluginManager.h"
#include "Tr.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <QVersionNumber>

#include <algorithm>
#include <memory>

namespace {

// Passt eine Datei des Index (bin/<plattform>/…) zu diesem Build?
bool platformMatches(const QString &platform)
{
    return Platform::binaryTags().contains(platform);
}

QNetworkRequest request(const QUrl &url)
{
    QNetworkRequest r(url);
    r.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Lumen/%1").arg(QCoreApplication::applicationVersion()));
    r.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    r.setTransferTimeout(30000);
    return r;
}

// Ordner verschieben; unter Windows halten Virenscanner frisch geschriebene Dateien
// kurz offen -> mehrfach versuchen, zuletzt kopieren und löschen
bool moveDir(const QString &from, const QString &to)
{
    for (int attempt = 0; attempt < 20; ++attempt) {
        if (QDir().rename(from, to))
            return true;
        QThread::msleep(100);
    }
    QDirIterator it(from, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString file = it.next();
        const QString target = QDir(to).filePath(QDir(from).relativeFilePath(file));
        QDir().mkpath(QFileInfo(target).absolutePath());
        if (!QFile::copy(file, target))
            return false;
    }
    QDir(from).removeRecursively();
    return true;
}

// Nur einfache relative Pfade (keine absoluten Pfade, kein "..")
bool safeRelative(const QString &path)
{
    if (path.isEmpty() || QDir::isAbsolutePath(path) || path.contains(QLatin1Char('\\')) || path.contains(QLatin1Char(':')))
        return false;
    for (const QString &part : path.split(QLatin1Char('/')))
        if (part.isEmpty() || part == QLatin1String("..") || part == QLatin1String("."))
            return false;
    return true;
}

} // namespace

PluginStore::PluginStore(PluginManager *plugins, QObject *parent)
    : QObject(parent)
    , m_plugins(plugins)
{
    connect(m_plugins, &PluginManager::pluginsChanged, this, &PluginStore::rebuild);
}

QString PluginStore::defaultSource()
{
    return QStringLiteral("Karl-Lauterbach24/Lumen-Plugins");
}

QUrl PluginStore::indexUrl(const QString &source)
{
    const QString s = source.trimmed();
    static const QRegularExpression shorthand(QStringLiteral("^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$"));
    static const QRegularExpression github(QStringLiteral("^https?://github\\.com/([^/]+)/([^/#?]+?)(?:\\.git)?(?:/tree/([^/#?]+)(/[^#?]*)?)?/?$"));
    if (shorthand.match(s).hasMatch() && !QFileInfo::exists(s))
        return QUrl(QStringLiteral("https://raw.githubusercontent.com/%1/main/index.json").arg(s));
    const auto m = github.match(s);
    if (m.hasMatch()) {
        const QString branch = m.captured(3).isEmpty() ? QStringLiteral("main") : m.captured(3);
        return QUrl(QStringLiteral("https://raw.githubusercontent.com/%1/%2/%3%4/index.json")
                        .arg(m.captured(1), m.captured(2), branch, m.captured(4)));
    }
    if (QFileInfo(s).isDir())
        return QUrl::fromLocalFile(QDir(s).absoluteFilePath(QStringLiteral("index.json")));
    if (QFileInfo(s).isFile())
        return QUrl::fromLocalFile(QFileInfo(s).absoluteFilePath());
    QUrl url(s);
    if (url.scheme().startsWith(QLatin1String("http")) || url.isLocalFile()) {
        if (!url.path().endsWith(QLatin1String(".json")))
            url.setPath(url.path() + (url.path().endsWith(QLatin1Char('/')) ? QString() : QStringLiteral("/")) + QStringLiteral("index.json"));
        return url;
    }
    return {};
}

QStringList PluginStore::sourceList() const
{
    QStringList list{defaultSource()};
    for (const QString &s : QSettings().value(QStringLiteral("plugins/stores")).toStringList())
        if (!list.contains(s))
            list << s;
    return list;
}

QVariantList PluginStore::sources() const
{
    QVariantList out;
    for (const QString &s : sourceList()) {
        out << QVariantMap{
            {"source", s}, {"builtin", s == defaultSource()}, {"name", m_indexes.value(s).value("name").toString()},
            {"error", m_errors.value(s)}, {"count", m_indexes.value(s).value("plugins").toList().size()},
        };
    }
    return out;
}

bool PluginStore::addSource(const QString &source)
{
    const QString s = source.trimmed();
    if (s.isEmpty() || !indexUrl(s).isValid() || indexUrl(s).isEmpty())
        return false;
    QStringList list = QSettings().value(QStringLiteral("plugins/stores")).toStringList();
    if (!list.contains(s) && s != defaultSource()) {
        list << s;
        QSettings().setValue(QStringLiteral("plugins/stores"), list);
    }
    refresh();
    return true;
}

void PluginStore::removeSource(const QString &source)
{
    QStringList list = QSettings().value(QStringLiteral("plugins/stores")).toStringList();
    list.removeAll(source);
    QSettings().setValue(QStringLiteral("plugins/stores"), list);
    m_indexes.remove(source);
    m_errors.remove(source);
    rebuild();
}

void PluginStore::setStatus(const QString &s)
{
    m_status = s;
    emit changed();
}

void PluginStore::refresh()
{
    for (const QString &source : sourceList()) {
        const QUrl url = indexUrl(source);
        if (!url.isValid() || url.isEmpty()) {
            m_errors.insert(source, LTR("Ungültige Quelle"));
            continue;
        }
        ++m_pending;
        QNetworkReply *reply = m_net.get(request(url));
        connect(reply, &QNetworkReply::finished, this, [this, reply, source] {
            reply->deleteLater();
            --m_pending;
            QJsonParseError err{};
            const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &err);
            if (reply->error() != QNetworkReply::NoError) {
                m_errors.insert(source, reply->errorString());
            } else if (err.error != QJsonParseError::NoError || !doc.isObject()) {
                m_errors.insert(source, LTR("index.json fehlerhaft: %1").arg(err.errorString()));
            } else {
                m_errors.remove(source);
                m_indexes.insert(source, doc.object().toVariantMap());
            }
            rebuild();
        });
    }
    emit changed();
}

QString PluginStore::installedVersion(const QString &id) const
{
    for (const QVariant &v : m_plugins->plugins()) {
        const QVariantMap p = v.toMap();
        if (p.value("id") == id)
            return p.value("version").toString().isEmpty() ? QStringLiteral("0") : p.value("version").toString();
    }
    return {};
}

bool PluginStore::storeInstalled(const QString &id) const
{
    return QFileInfo::exists(QDir(m_plugins->userDir()).filePath(id + QStringLiteral("/.lumen-store.json")));
}

void PluginStore::rebuild()
{
    m_available.clear();
    for (const QString &source : sourceList()) {
        const QVariantMap index = m_indexes.value(source);
        for (const QVariant &v : index.value("plugins").toList()) {
            const QVariantMap p = v.toMap();
            const QString id = p.value("id").toString();
            if (id.isEmpty() || !safeRelative(id))
                continue;
            bool native = false, supported = true;
            QStringList platforms;
            for (const QVariant &f : p.value("files").toList()) {
                const QString platform = f.toMap().value("platform").toString();
                if (!platform.isEmpty()) {
                    native = true;
                    if (!platforms.contains(platform))
                        platforms << platform;
                }
            }
            if (native && std::none_of(platforms.cbegin(), platforms.cend(), platformMatches))
                supported = false;
            const QString installed = installedVersion(id);
            const bool update = !installed.isEmpty()
                                && QVersionNumber::fromString(p.value("version").toString()) > QVersionNumber::fromString(installed);
            m_available << QVariantMap{
                {"source", source}, {"sourceName", index.value("name", source)}, {"id", id},
                {"name", p.value("name", id)}, {"version", p.value("version")}, {"description", p.value("description")},
                {"author", p.value("author")}, {"installed", installed}, {"update", update}, {"native", native},
                {"supported", supported}, {"storeInstalled", storeInstalled(id)}, {"tags", p.value("tags")},
            };
        }
    }
    emit changed();
}

void PluginStore::install(const QString &source, const QString &id)
{
    QVariantMap entry;
    for (const QVariant &v : m_indexes.value(source).value("plugins").toList())
        if (v.toMap().value("id") == id)
            entry = v.toMap();
    if (entry.isEmpty() || !safeRelative(id)) {
        emit installed(id, false, LTR("Plugin nicht im Index"));
        return;
    }
    const QString path = entry.value("path", QStringLiteral("plugins/") + id).toString();
    const QUrl base = indexUrl(source).resolved(QUrl(path + QStringLiteral("/")));

    struct Job { QString rel; QString sha; };
    QList<Job> jobs;
    for (const QVariant &f : entry.value("files").toList()) {
        const QVariantMap m = f.toMap();
        const QString platform = m.value("platform").toString();
        if (!platform.isEmpty() && !platformMatches(platform))
            continue;
        const QString rel = m.value("path").toString();
        if (!safeRelative(rel)) {
            emit installed(id, false, LTR("Unzulässiger Dateipfad im Index: %1").arg(rel));
            return;
        }
        jobs << Job{rel, m.value("sha256").toString().toLower()};
    }
    if (jobs.isEmpty()) {
        emit installed(id, false, LTR("Keine Dateien für diese Plattform"));
        return;
    }

    // Erst vollständig in einen Zwischenordner laden und prüfen, dann austauschen
    QDir().mkpath(m_plugins->userDir());
    auto staging = std::make_shared<QTemporaryDir>(QDir(m_plugins->userDir()).filePath(QStringLiteral(".staging-XXXXXX")));
    if (!staging->isValid()) {
        emit installed(id, false, LTR("Zwischenordner nicht anlegbar"));
        return;
    }
    auto remaining = std::make_shared<int>(int(jobs.size()));
    auto failed = std::make_shared<QString>();
    setStatus(LTR("Lade %1 …").arg(entry.value("name", id).toString()));
    ++m_pending;
    for (const Job &job : jobs) {
        QNetworkReply *reply = m_net.get(request(base.resolved(QUrl(job.rel))));
        connect(reply, &QNetworkReply::finished, this, [=, this] {
            reply->deleteLater();
            const QByteArray data = reply->readAll();
            if (reply->error() != QNetworkReply::NoError) {
                *failed = QStringLiteral("%1: %2").arg(job.rel, reply->errorString());
            } else if (!job.sha.isEmpty() && QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex() != job.sha.toLatin1()) {
                *failed = LTR("Prüfsumme falsch: %1").arg(job.rel);
            } else {
                const QString target = QDir(staging->path()).filePath(job.rel);
                QDir().mkpath(QFileInfo(target).absolutePath());
                QFile out(target);
                if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size())
                    *failed = LTR("Nicht schreibbar: %1").arg(job.rel);
            }
            if (--*remaining > 0)
                return;
            --m_pending;
            if (failed->isEmpty()) {
                QFile marker(QDir(staging->path()).filePath(QStringLiteral(".lumen-store.json")));
                if (marker.open(QIODevice::WriteOnly))
                    marker.write(QJsonDocument(QJsonObject{{"source", source}, {"version", entry.value("version").toString()}})
                                     .toJson(QJsonDocument::Compact));
                marker.close();
                const QString target = QDir(m_plugins->userDir()).filePath(id);
                const QString old = target + QStringLiteral(".old");
                QDir(old).removeRecursively();
                // Laufende native Bibliotheken lassen sich unter Windows nicht löschen: altes Verzeichnis nur umbenennen
                if (QFileInfo::exists(target) && !QDir().rename(target, old))
                    *failed = LTR("Altes Plugin ist in Benutzung – bitte deaktivieren und neu starten");
                else if (!moveDir(staging->path(), target))
                    *failed = LTR("Installation nicht abschließbar");
                else
                    staging->setAutoRemove(false);
                QDir(old).removeRecursively();
            }
            m_plugins->rescan();
            const QString name = entry.value("name", id).toString();
            setStatus(failed->isEmpty() ? LTR("%1 installiert – im Plugin-Tab aktivieren").arg(name) : *failed);
            emit installed(id, failed->isEmpty(), failed->isEmpty() ? QString() : *failed);
        });
    }
}

bool PluginStore::uninstall(const QString &id)
{
    if (!safeRelative(id) || !storeInstalled(id))
        return false;
    for (const QVariant &v : m_plugins->plugins()) {
        const QVariantMap p = v.toMap();
        if (p.value("id") == id && p.value("loaded").toBool()) {
            m_plugins->setEnabled(id, false);
            setStatus(LTR("Plugin läuft noch – nach dem Neustart erneut entfernen"));
            return false;
        }
    }
    const bool ok = QDir(QDir(m_plugins->userDir()).filePath(id)).removeRecursively();
    m_plugins->rescan();
    setStatus(ok ? LTR("Plugin entfernt") : LTR("Plugin konnte nicht entfernt werden"));
    return ok;
}
