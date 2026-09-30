#pragma once

#include <QLibrary>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <memory>
#include <vector>

#include "lumen/plugin.h"

struct mpv_handle;

// Plugins: Ordner mit "plugin.json" (siehe plugins/README.md). Ein Plugin kann
//  - eine native Bibliothek (C-ABI, include/lumen/plugin.h) mitbringen: Ereignisse,
//    Schaltflächen, eigene URL-Schemata als Quellen, DCP-Schlüssel, mpv-Befehle,
//  - mpv-Skripte (Lua/JavaScript) und mpv-Optionen setzen,
//  - Umgebungsvariablen setzen,
//  - vom Nutzer bereitgestellte Disc-Bibliotheken (libaacs, libbdplus, libdvdcss)
//    für libbluray/libdvdread verfügbar machen.
// Lumen selbst enthält keine Umgehung von Kopierschutz; Plugins installiert und
// aktiviert ausschließlich der Nutzer. Aktivieren/Deaktivieren wirkt nach einem
// Neustart (native Bibliotheken lassen sich nicht sicher entladen).
class PluginManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList plugins READ plugins NOTIFY pluginsChanged)
    Q_PROPERTY(QString userDir READ userDir CONSTANT)
    Q_PROPERTY(bool restartNeeded READ restartNeeded NOTIFY pluginsChanged)
    Q_PROPERTY(QStringList discLibraries READ discLibraries NOTIFY pluginsChanged)

public:
    explicit PluginManager(QObject *parent = nullptr);
    ~PluginManager() override;

    // Suchordner: Nutzerordner, <Programm>/plugins, LUMEN_PLUGIN_PATH
    QStringList searchPaths() const;
    QString userDir() const;
    void discover();
    // Aktivierte Plugins laden – vor dem Start von mpv/libbluray aufrufen
    void loadEnabled();
    void unloadAll();

    // Für den Player: zusätzliche mpv-Optionen (Skripte, Optionen der Plugins)
    QVariantMap mpvOptions() const;
    // Nach mpv_initialize: URL-Schemata registrieren, Host-Befehle zulassen
    void attach(mpv_handle *mpv);
    void detach();

    void sendEvent(const QString &event, const QVariantMap &payload = {});

    QVariantList plugins() const;
    bool restartNeeded() const { return m_restartNeeded; }
    // Von Plugins bereitgestellte Disc-Bibliotheken ("aacs", "bdplus", "dvdcss")
    QStringList discLibraries() const;

    Q_INVOKABLE void setEnabled(const QString &id, bool enabled);
    Q_INVOKABLE void trigger(const QString &id, const QString &actionId);
    Q_INVOKABLE void openUserDir();
    Q_INVOKABLE void restartApp();

signals:
    void pluginsChanged();
    void openRequested(const QString &url);

private:
    struct Action { QString id, label; };
    struct Plugin {
        QString id, name, version, description, author, dir, error, status;
        QVariantMap manifest;
        bool enabled = false; // Einstellung
        bool loaded = false;  // in diesem Programmlauf aktiv
        QStringList scripts, kinds;
        QVariantMap mpvOptions;
        std::unique_ptr<QLibrary> library;
        std::vector<std::unique_ptr<QLibrary>> preloaded;
        const lumen_plugin *api = nullptr;
        void *ctx = nullptr;
        std::unique_ptr<lumen_host> host;
        QByteArray dirUtf8, configUtf8;
        QList<Action> actions;
        PluginManager *owner = nullptr;
    };

    void load(Plugin &p);
    QString resolveLibrary(const Plugin &p, const QString &base) const;
    static QByteArray libbluraySpec(const QString &file);
    void setupHost(Plugin &p);
    QByteArray providedDcpKey(const QByteArray &keyId);

    std::vector<std::unique_ptr<Plugin>> m_plugins;
    QStringList m_enabledAtStart;
    QStringList m_discLibs;
    mpv_handle *m_mpv = nullptr;
    bool m_restartNeeded = false;

    friend struct PluginHostImpl;
};
