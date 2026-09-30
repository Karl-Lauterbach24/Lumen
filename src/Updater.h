#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QUrl>

// Update-Prüfung gegen die GitHub-Releases von Lumen und Ein-Klick-Aktualisierung:
//   Windows  Lumen-<ver>-windows-x64.zip  -> entpacken, nach dem Beenden ersetzen, neu starten
//   macOS    Lumen-<ver>-macos-arm64.dmg  -> einhängen, Lumen.app ersetzen, neu starten
//   sonst    Release-Seite im Browser
// Jede Datei wird gegen SHA256SUMS.txt des Releases geprüft.
class Updater : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY changed)
    Q_PROPERTY(QString notes READ notes NOTIFY changed)
    Q_PROPERTY(QString pageUrl READ pageUrl NOTIFY changed)
    Q_PROPERTY(bool canInstall READ canInstall NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool autoCheck READ autoCheck WRITE setAutoCheck NOTIFY changed)

public:
    explicit Updater(QObject *parent = nullptr);

    static QString repository() { return QStringLiteral("Karl-Lauterbach24/Lumen"); }
    static QString assetName(const QString &version);

    bool available() const { return m_available; }
    QString latestVersion() const { return m_latest; }
    QString notes() const { return m_notes; }
    QString pageUrl() const { return m_page; }
    bool canInstall() const { return m_available && m_asset.isValid(); }
    bool busy() const { return m_busy; }
    double progress() const { return m_progress; }
    QString status() const { return m_status; }
    bool autoCheck() const;
    void setAutoCheck(bool on);

    // höchstens einmal täglich (beim Start)
    void checkAutomatically();
    Q_INVOKABLE void check();
    Q_INVOKABLE void install();
    Q_INVOKABLE void openPage();

    // Für Tests: API-Adresse, Zielordner, kein Neustart
    void setApiUrl(const QUrl &url) { m_api = url; }
    void setInstallDir(const QString &dir) { m_installDir = dir; }
    void setDryRun(bool on) { m_dryRun = on; }
    QString preparedDir() const { return m_prepared; }

signals:
    void changed();
    void readyToRestart(); // Dry-Run: Update liegt entpackt bereit

private:
    void setStatus(const QString &s);
    void fail(const QString &message);
    void verifyAndApply(const QByteArray &data, const QByteArray &expectedSha);
    bool apply(const QString &archive);
    QString installDir() const;

    QNetworkAccessManager m_net;
    QUrl m_api;
    bool m_available = false;
    QString m_latest, m_notes, m_page;
    QUrl m_asset, m_sums;
    QString m_assetName;
    bool m_busy = false;
    double m_progress = 0;
    QString m_status;
    QString m_installDir;
    bool m_dryRun = false;
    QString m_prepared;
};
