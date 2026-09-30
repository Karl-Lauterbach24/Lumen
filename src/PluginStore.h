#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QVariantList>

class PluginManager;

// Plugin-Store: Quellen sind Verzeichnisse mit einer "index.json" (GitHub-Repository,
// eigener Webserver oder lokaler Ordner). Standard ist das offizielle Repository
// github.com/Karl-Lauterbach24/Lumen-Plugins; eigene Quellen kann der Nutzer
// hinzufügen ("owner/repo", GitHub-URL oder direkte URL zur index.json).
//
// index.json (Format 1):
//   { "format": 1, "name": "…",
//     "plugins": [ { "id", "name", "version", "description", "author", "path",
//                    "files": [ { "path", "sha256", "platform"? } ] } ] }
// Installiert wird nach <Nutzer-Pluginordner>/<id>/; jede Datei wird gegen ihre
// SHA-256-Summe geprüft. Neu installierte Plugins sind – wie alle – zunächst aus.
class PluginStore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList sources READ sources NOTIFY changed)
    Q_PROPERTY(QVariantList available READ available NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)

public:
    explicit PluginStore(PluginManager *plugins, QObject *parent = nullptr);

    static QString defaultSource();
    // "owner/repo", "https://github.com/owner/repo[/tree/branch]" oder URL/Pfad -> URL der index.json
    static QUrl indexUrl(const QString &source);

    QVariantList sources() const;
    QVariantList available() const { return m_available; }
    bool busy() const { return m_pending > 0; }
    QString status() const { return m_status; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE bool addSource(const QString &source);
    Q_INVOKABLE void removeSource(const QString &source);
    Q_INVOKABLE void install(const QString &source, const QString &id);
    Q_INVOKABLE bool uninstall(const QString &id);

signals:
    void changed();
    void installed(const QString &id, bool ok, const QString &message);

private:
    QStringList sourceList() const;
    void rebuild();
    QString installedVersion(const QString &id) const;
    bool storeInstalled(const QString &id) const;
    void setStatus(const QString &s);

    PluginManager *m_plugins;
    QNetworkAccessManager m_net;
    QHash<QString, QVariantMap> m_indexes;   // Quelle -> index.json
    QHash<QString, QString> m_errors;        // Quelle -> Fehler
    QVariantList m_available;
    QString m_status;
    int m_pending = 0;
};
