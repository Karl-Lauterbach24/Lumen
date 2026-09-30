#include "PluginManager.h"
#include "DcpStream.h"
#include "Tr.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>

#include <mpv/client.h>
#include <mpv/stream_cb.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace {

const char *platformKey()
{
#if defined(Q_OS_WIN)
    return "windows";
#elif defined(Q_OS_MACOS)
    return "macos";
#else
    return "linux";
#endif
}

// Manifest-Wert: "pfad" oder {"windows": …, "macos": …, "linux": …}
QString platformValue(const QVariant &v)
{
    if (v.typeId() == QMetaType::QVariantMap)
        return v.toMap().value(QLatin1String(platformKey())).toString();
    return v.toString();
}

char *copyString(const QByteArray &s)
{
    char *out = static_cast<char *>(std::malloc(size_t(s.size()) + 1));
    if (out)
        std::memcpy(out, s.constData(), size_t(s.size()) + 1);
    return out;
}

const QHash<QString, const char *> &discLibraryInfo()
{
    // Bibliothek -> Umgebungsvariable von libbluray (libdvdcss lädt libdvdread
    // über den Dateinamen; das Vorladen genügt)
    static const QHash<QString, const char *> info = {
        {QStringLiteral("aacs"), "LIBAACS_PATH"},
        {QStringLiteral("bdplus"), "LIBBDPLUS_PATH"},
        {QStringLiteral("dvdcss"), nullptr},
    };
    return info;
}

} // namespace

// Host-Funktionen für native Plugins (ctx = PluginManager::Plugin)
struct PluginHostImpl
{
    using P = PluginManager::Plugin;
    static P *p(void *ctx) { return static_cast<P *>(ctx); }

    static void log(void *ctx, int level, const char *message)
    {
        const QString text = QStringLiteral("[%1] %2").arg(p(ctx)->id, QString::fromUtf8(message));
        if (level <= LUMEN_LOG_ERROR)
            qWarning().noquote() << text;
        else if (level == LUMEN_LOG_WARN)
            qWarning().noquote() << text;
        else
            qInfo().noquote() << text;
    }
    static int command(void *ctx, const char **args)
    {
        mpv_handle *mpv = p(ctx)->owner->m_mpv;
        return mpv && args ? mpv_command(mpv, args) : -1;
    }
    static char *getProperty(void *ctx, const char *name)
    {
        mpv_handle *mpv = p(ctx)->owner->m_mpv;
        if (!mpv || !name)
            return nullptr;
        char *v = mpv_get_property_string(mpv, name);
        if (!v)
            return nullptr;
        char *out = copyString(QByteArray(v));
        mpv_free(v);
        return out;
    }
    static int setProperty(void *ctx, const char *name, const char *value)
    {
        mpv_handle *mpv = p(ctx)->owner->m_mpv;
        return mpv && name && value ? mpv_set_property_string(mpv, name, value) : -1;
    }
    static void freeString(void *, char *s) { std::free(s); }
    static void showText(void *ctx, const char *text, int ms)
    {
        const QByteArray d = QByteArray::number(ms > 0 ? ms : 2000);
        const char *args[] = {"show-text", text ? text : "", d.constData(), nullptr};
        command(ctx, args);
    }
    static int addAction(void *ctx, const char *id, const char *label)
    {
        if (!id || !label)
            return -1;
        P *plugin = p(ctx);
        PluginManager *m = plugin->owner;
        const QString aid = QString::fromUtf8(id), text = QString::fromUtf8(label);
        QMetaObject::invokeMethod(m, [plugin, m, aid, text] {
            plugin->actions.append({aid, text});
            emit m->pluginsChanged();
        }, Qt::QueuedConnection);
        return 0;
    }
    static void setStatus(void *ctx, const char *text)
    {
        P *plugin = p(ctx);
        PluginManager *m = plugin->owner;
        const QString s = QString::fromUtf8(text ? text : "");
        QMetaObject::invokeMethod(m, [plugin, m, s] {
            plugin->status = s;
            emit m->pluginsChanged();
        }, Qt::QueuedConnection);
    }
    static const char *pluginDir(void *ctx) { return p(ctx)->dirUtf8.constData(); }
    static const char *configDir(void *ctx) { return p(ctx)->configUtf8.constData(); }
    static int open(void *ctx, const char *url)
    {
        if (!url)
            return -1;
        PluginManager *m = p(ctx)->owner;
        const QString u = QString::fromUtf8(url);
        QMetaObject::invokeMethod(m, [m, u] { emit m->openRequested(u); }, Qt::QueuedConnection);
        return 0;
    }

    // mpv-Stream über das URL-Schema eines Plugins
    static int streamOpen(void *ud, char *uri, mpv_stream_cb_info *info)
    {
        P *plugin = p(ud);
        auto *s = new lumen_stream{};
        s->struct_size = sizeof(lumen_stream);
        if (!plugin->api->stream_open || plugin->api->stream_open(plugin->ctx, uri, s) != 0 || !s->read) {
            delete s;
            return MPV_ERROR_LOADING_FAILED;
        }
        info->cookie = s;
        info->read_fn = [](void *c, char *buf, uint64_t n) -> int64_t {
            auto *st = static_cast<lumen_stream *>(c);
            return st->read(st->cookie, buf, n);
        };
        if (s->seek) {
            info->seek_fn = [](void *c, int64_t offset) -> int64_t {
                auto *st = static_cast<lumen_stream *>(c);
                const int64_t r = st->seek(st->cookie, offset);
                return r < 0 ? MPV_ERROR_GENERIC : r;
            };
        }
        info->size_fn = [](void *c) -> int64_t {
            auto *st = static_cast<lumen_stream *>(c);
            const int64_t r = st->size ? st->size(st->cookie) : -1;
            return r < 0 ? MPV_ERROR_UNSUPPORTED : r;
        };
        info->close_fn = [](void *c) {
            auto *st = static_cast<lumen_stream *>(c);
            if (st->close)
                st->close(st->cookie);
            delete st;
        };
        return 0;
    }
};

PluginManager::PluginManager(QObject *parent)
    : QObject(parent)
{
    discover();
}

PluginManager::~PluginManager()
{
    unloadAll();
}

QString PluginManager::userDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/plugins");
}

QStringList PluginManager::searchPaths() const
{
    QStringList paths;
    const QString env = qEnvironmentVariable("LUMEN_PLUGIN_PATH");
    if (!env.isEmpty())
        paths += env.split(QDir::listSeparator(), Qt::SkipEmptyParts);
    paths << userDir();
    const QString app = QCoreApplication::applicationDirPath();
    paths << app + QStringLiteral("/plugins");
#if defined(Q_OS_MACOS)
    paths << app + QStringLiteral("/../PlugIns/lumen");
#elif defined(Q_OS_LINUX)
    paths << app + QStringLiteral("/../lib/lumen/plugins");
#endif
    return paths;
}

void PluginManager::rescan()
{
    std::vector<std::unique_ptr<Plugin>> loaded;
    for (auto &p : m_plugins)
        if (p->loaded)
            loaded.push_back(std::move(p));
    discover();
    for (auto &l : loaded) {
        auto it = std::find_if(m_plugins.begin(), m_plugins.end(), [&](const auto &p) { return p && p->id == l->id; });
        if (it != m_plugins.end())
            *it = std::move(l);
        else
            m_plugins.push_back(std::move(l));
    }
    emit pluginsChanged();
}

void PluginManager::discover()
{
    m_plugins.clear();
    QSettings settings;
    const QStringList enabled = settings.value(QStringLiteral("plugins/enabled")).toStringList();
    // Tests/Entwicklung: LUMEN_PLUGINS_ENABLE=all oder id1,id2
    const QString force = qEnvironmentVariable("LUMEN_PLUGINS_ENABLE");
    const QStringList forced = force.split(QLatin1Char(','), Qt::SkipEmptyParts);

    QSet<QString> seen;
    for (const QString &base : searchPaths()) {
        const QDir root(base);
        if (!root.exists())
            continue;
        for (const QString &sub : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            const QString dir = root.filePath(sub);
            QFile f(dir + QStringLiteral("/plugin.json"));
            if (!f.open(QIODevice::ReadOnly))
                continue;
            QJsonParseError err{};
            const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
            auto p = std::make_unique<Plugin>();
            p->owner = this;
            p->dir = QDir::cleanPath(QFileInfo(dir).absoluteFilePath());
            p->manifest = doc.object().toVariantMap();
            p->id = p->manifest.value("id", sub).toString();
            if (seen.contains(p->id))
                continue; // erster Fund gewinnt (Reihenfolge der Suchordner)
            seen.insert(p->id);
            p->name = p->manifest.value("name", p->id).toString();
            p->version = p->manifest.value("version").toString();
            p->description = p->manifest.value("description").toString();
            p->author = p->manifest.value("author").toString();
            if (err.error != QJsonParseError::NoError)
                p->error = LTR("plugin.json fehlerhaft: %1").arg(err.errorString());
            p->enabled = forced.contains(QStringLiteral("all")) || forced.contains(p->id) || enabled.contains(p->id);

            if (!platformValue(p->manifest.value("library")).isEmpty())
                p->kinds << LTR("Nativ");
            if (!p->manifest.value("scripts").toList().isEmpty())
                p->kinds << LTR("mpv-Skripte");
            const QVariantMap libs = p->manifest.value("discLibraries").toMap();
            static const QHash<QString, QString> libNames = {
                {"aacs", "AACS"}, {"bdplus", "BD+"}, {"dvdcss", "CSS"}};
            for (auto it = libs.cbegin(); it != libs.cend(); ++it)
                p->kinds << libNames.value(it.key(), it.key());
            m_plugins.push_back(std::move(p));
        }
    }
    emit pluginsChanged();
}

QString PluginManager::resolveLibrary(const Plugin &p, const QString &base) const
{
    if (base.isEmpty())
        return {};
    const QFileInfo fi(QDir(p.dir).filePath(base));
    if (fi.isFile())
        return fi.absoluteFilePath();
    const QString dir = fi.absolutePath(), name = fi.fileName();
    QStringList names{name + QStringLiteral(".dll"), name + QStringLiteral(".so"), name + QStringLiteral(".dylib")};
    if (!name.startsWith(QLatin1String("lib")))
        names << QStringLiteral("lib") + name + QStringLiteral(".so") << QStringLiteral("lib") + name + QStringLiteral(".dylib")
              << QStringLiteral("lib") + name + QStringLiteral(".dll");
    for (const QString &n : names) {
        const QString path = dir + QLatin1Char('/') + n;
        if (QFileInfo(path).isFile())
            return QFileInfo(path).absoluteFilePath();
    }
    return {};
}

// libbluray hängt Endung (und Version) selbst an: LIBAACS_PATH=/x/libaacs -> /x/libaacs.so.0
QByteArray PluginManager::libbluraySpec(const QString &file)
{
    QString s = QDir::toNativeSeparators(file);
#if defined(Q_OS_WIN)
    s.remove(QRegularExpression(QStringLiteral("\\.dll$"), QRegularExpression::CaseInsensitiveOption));
#elif defined(Q_OS_MACOS)
    s.remove(QRegularExpression(QStringLiteral("(\\.\\d+)?\\.dylib$")));
#else
    s.remove(QRegularExpression(QStringLiteral("\\.so(\\.\\d+)*$")));
#endif
    return s.toLocal8Bit();
}

void PluginManager::loadEnabled()
{
    m_enabledAtStart.clear();
    bool keyProvider = false;
    for (auto &p : m_plugins) {
        if (!p->enabled || !p->error.isEmpty())
            continue;
        m_enabledAtStart << p->id;
        load(*p);
        if (p->loaded && p->api && p->api->dcp_content_key)
            keyProvider = true;
    }
    if (keyProvider)
        Dcp::setKeyProvider([this](const QByteArray &keyId) { return providedDcpKey(keyId); });
    m_restartNeeded = false;
    emit pluginsChanged();
}

void PluginManager::load(Plugin &p)
{
    const QString dir = p.dir;
    auto expand = [&](QString v) {
        v.replace(QLatin1String("${pluginDir}"), dir);
        v.replace(QLatin1String("${configDir}"), userDir() + QStringLiteral("/config/") + p.id);
        return v;
    };

    // 1. Umgebung
    const QVariantMap env = p.manifest.value("env").toMap();
    for (auto it = env.cbegin(); it != env.cend(); ++it)
        qputenv(it.key().toLocal8Bit().constData(), expand(platformValue(it.value())).toLocal8Bit());

    // 2. Disc-Bibliotheken des Nutzers (libaacs, libbdplus, libdvdcss)
    const QVariantMap libs = p.manifest.value("discLibraries").toMap();
    for (auto it = libs.cbegin(); it != libs.cend(); ++it) {
        if (!discLibraryInfo().contains(it.key())) {
            p.error = LTR("Unbekannte Disc-Bibliothek „%1“").arg(it.key());
            return;
        }
        const QString file = resolveLibrary(p, expand(platformValue(it.value())));
        if (file.isEmpty()) {
            // "optional": true – Bibliothek bringt der Nutzer selbst mit; ohne sie läuft der Rest des Plugins
            if (it.value().toMap().value("optional").toBool()) {
                p.status = LTR("%1 fehlt – Bibliothek nach %2 kopieren").arg(it.key(), QDir::toNativeSeparators(QFileInfo(QDir(p.dir).filePath(platformValue(it.value()))).absolutePath()));
                continue;
            }
            p.error = LTR("Bibliothek „%1“ nicht gefunden").arg(platformValue(it.value()));
            return;
        }
#ifdef Q_OS_WIN
        // Abhängigkeiten der Bibliothek (z. B. libgcrypt) liegen meist daneben
        const QByteArray path = qgetenv("PATH");
        const QByteArray libDir = QDir::toNativeSeparators(QFileInfo(file).absolutePath()).toLocal8Bit();
        if (!path.contains(libDir))
            qputenv("PATH", libDir + ';' + path);
#endif
        // Vorladen: libbluray/libdvdread finden die bereits geladene Bibliothek
        // über ihren Namen (Windows: Modulname, Linux: SONAME)
        auto lib = std::make_unique<QLibrary>(file);
        lib->setLoadHints(QLibrary::ExportExternalSymbolsHint);
        if (!lib->load()) {
            p.error = LTR("Bibliothek „%1“ konnte nicht geladen werden: %2").arg(QFileInfo(file).fileName(), lib->errorString());
            return;
        }
        p.preloaded.push_back(std::move(lib));
        if (const char *var = discLibraryInfo().value(it.key()))
            qputenv(var, libbluraySpec(file));
        // Lumens libdvdcss-Shim (Windows/macOS) reicht an diese Bibliothek durch
        if (it.key() == QLatin1String("dvdcss"))
            qputenv("LUMEN_DVDCSS_LIBRARY", QDir::toNativeSeparators(file).toUtf8());
        if (!m_discLibs.contains(it.key()))
            m_discLibs << it.key();
    }

    // 3. mpv-Skripte und -Optionen
    for (const QVariant &s : p.manifest.value("scripts").toList()) {
        const QString path = QDir(dir).filePath(expand(s.toString()));
        if (QFileInfo::exists(path))
            p.scripts << QDir::toNativeSeparators(path);
        else
            p.status = LTR("Skript fehlt: %1").arg(s.toString());
    }
    const QVariantMap opts = p.manifest.value("mpvOptions").toMap();
    for (auto it = opts.cbegin(); it != opts.cend(); ++it)
        p.mpvOptions.insert(it.key(), expand(it.value().toString()));

    // 4. Native Bibliothek
    const QString libName = platformValue(p.manifest.value("library"));
    if (!libName.isEmpty()) {
        const QString file = resolveLibrary(p, expand(libName));
        if (file.isEmpty()) {
            p.error = LTR("Plugin-Bibliothek „%1“ nicht gefunden").arg(libName);
            return;
        }
        p.library = std::make_unique<QLibrary>(file);
        if (!p.library->load()) {
            p.error = LTR("Plugin-Bibliothek konnte nicht geladen werden: %1").arg(p.library->errorString());
            return;
        }
        auto entry = reinterpret_cast<lumen_plugin_entry_fn>(p.library->resolve("lumen_plugin_entry"));
        const lumen_plugin *api = entry ? entry() : nullptr;
        if (!api) {
            p.error = LTR("Keine Einstiegsfunktion lumen_plugin_entry()");
            return;
        }
        if (api->api_version != LUMEN_PLUGIN_API_VERSION || api->struct_size < sizeof(lumen_plugin)) {
            p.error = LTR("Plugin-API %1 wird nicht unterstützt (Lumen: %2)").arg(api->api_version).arg(LUMEN_PLUGIN_API_VERSION);
            return;
        }
        p.api = api;
        setupHost(p);
        if (api->init) {
            p.ctx = api->init(p.host.get());
            if (!p.ctx) {
                p.api = nullptr;
                if (p.status.isEmpty())
                    p.error = LTR("Initialisierung abgelehnt");
                else
                    p.error = p.status;
                return;
            }
        }
    }
    p.loaded = true;
}

void PluginManager::setupHost(Plugin &p)
{
    p.dirUtf8 = QDir::toNativeSeparators(p.dir).toUtf8();
    const QString cfg = userDir() + QStringLiteral("/config/") + p.id;
    QDir().mkpath(cfg);
    p.configUtf8 = QDir::toNativeSeparators(cfg).toUtf8();
    p.host = std::make_unique<lumen_host>();
    lumen_host &h = *p.host;
    h.struct_size = sizeof(lumen_host);
    h.api_version = LUMEN_PLUGIN_API_VERSION;
    h.ctx = &p;
    h.log = PluginHostImpl::log;
    h.command = PluginHostImpl::command;
    h.get_property = PluginHostImpl::getProperty;
    h.set_property = PluginHostImpl::setProperty;
    h.free_string = PluginHostImpl::freeString;
    h.show_text = PluginHostImpl::showText;
    h.add_action = PluginHostImpl::addAction;
    h.set_status = PluginHostImpl::setStatus;
    h.plugin_dir = PluginHostImpl::pluginDir;
    h.config_dir = PluginHostImpl::configDir;
    h.open = PluginHostImpl::open;
}

void PluginManager::unloadAll()
{
    for (auto &p : m_plugins) {
        if (p->api && p->api->shutdown && p->ctx)
            p->api->shutdown(p->ctx);
        p->ctx = nullptr;
        p->api = nullptr;
        // Bibliotheken bleiben bis Programmende geladen (Rückrufe könnten noch laufen)
    }
}

QVariantMap PluginManager::mpvOptions() const
{
    QVariantMap o;
    QStringList scripts;
    for (const auto &p : m_plugins) {
        if (!p->loaded)
            continue;
        scripts += p->scripts;
        for (auto it = p->mpvOptions.cbegin(); it != p->mpvOptions.cend(); ++it)
            o.insert(it.key(), it.value());
    }
    if (!scripts.isEmpty())
        o.insert(QStringLiteral("scripts"), scripts.join(QDir::listSeparator()));
    return o;
}

void PluginManager::attach(mpv_handle *mpv)
{
    m_mpv = mpv;
    for (auto &p : m_plugins) {
        if (!p->loaded || !p->api || !p->api->schemes || !p->api->stream_open)
            continue;
        for (const char *const *s = p->api->schemes; *s; ++s) {
            if (mpv_stream_cb_add_ro(mpv, *s, p.get(), PluginHostImpl::streamOpen) < 0)
                qWarning("Lumen: Plugin %s: URL-Schema %s nicht registriert", qPrintable(p->id), *s);
        }
    }
}

void PluginManager::detach()
{
    m_mpv = nullptr;
}

void PluginManager::sendEvent(const QString &event, const QVariantMap &payload)
{
    const QByteArray name = event.toUtf8();
    const QByteArray json = QJsonDocument(QJsonObject::fromVariantMap(payload)).toJson(QJsonDocument::Compact);
    for (auto &p : m_plugins) {
        if (p->api && p->api->on_event)
            p->api->on_event(p->ctx, name.constData(), json.constData());
    }
}

QByteArray PluginManager::providedDcpKey(const QByteArray &keyId)
{
    if (keyId.size() != 16)
        return {};
    for (auto &p : m_plugins) {
        if (!p->api || !p->api->dcp_content_key)
            continue;
        uint8_t key[16] = {};
        if (p->api->dcp_content_key(p->ctx, reinterpret_cast<const uint8_t *>(keyId.constData()), key) == 1)
            return QByteArray(reinterpret_cast<const char *>(key), 16);
    }
    return {};
}

QVariantList PluginManager::plugins() const
{
    QVariantList out;
    for (const auto &p : m_plugins) {
        QVariantList actions;
        for (const Action &a : p->actions)
            actions << QVariantMap{{"id", a.id}, {"label", a.label}};
        out << QVariantMap{
            {"id", p->id}, {"name", p->name}, {"version", p->version}, {"description", p->description},
            {"author", p->author}, {"dir", QDir::toNativeSeparators(p->dir)}, {"enabled", p->enabled},
            {"loaded", p->loaded}, {"error", p->error}, {"status", p->status}, {"kinds", p->kinds},
            {"actions", actions}, {"pending", p->enabled != m_enabledAtStart.contains(p->id)},
        };
    }
    return out;
}

QStringList PluginManager::discLibraries() const
{
    return m_discLibs;
}

void PluginManager::setEnabled(const QString &id, bool enabled)
{
    QSettings settings;
    QStringList list = settings.value(QStringLiteral("plugins/enabled")).toStringList();
    list.removeAll(id);
    if (enabled)
        list << id;
    settings.setValue(QStringLiteral("plugins/enabled"), list);
    m_restartNeeded = false;
    for (auto &p : m_plugins) {
        if (p->id == id)
            p->enabled = enabled;
        if (p->enabled != m_enabledAtStart.contains(p->id))
            m_restartNeeded = true;
    }
    emit pluginsChanged();
}

void PluginManager::trigger(const QString &id, const QString &actionId)
{
    for (auto &p : m_plugins) {
        if (p->id == id && p->api && p->api->on_action)
            p->api->on_action(p->ctx, actionId.toUtf8().constData());
    }
}

void PluginManager::openUserDir()
{
    QDir().mkpath(userDir());
    QDesktopServices::openUrl(QUrl::fromLocalFile(userDir()));
}

void PluginManager::restartApp()
{
    QProcess::startDetached(QCoreApplication::applicationFilePath(), QCoreApplication::arguments().mid(1));
    QCoreApplication::quit();
}
