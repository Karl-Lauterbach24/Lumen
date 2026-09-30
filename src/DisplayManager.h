#pragma once

#include <QHash>
#include <QObject>
#include <QSize>
#include <QVariant>

// Kennt alle Ausgabegeräte (Monitore, Beamer, TVs) und schaltet Bildwiederholrate
// und HDR-Modus des Ziels passend zum Inhalt.
//
//   Windows        : EnumDisplayMonitors (Reihenfolge = mpv --screen),
//                    Refresh via ChangeDisplaySettingsEx, HDR via DisplayConfig-API
//   Linux X11      : xrandr (Refresh)
//   Linux Wayland  : kscreen-doctor (KDE Plasma: Refresh + HDR); andere Compositoren: nur Anzeige
//   macOS          : CoreGraphics-Displaymodi (Refresh); HDR/EDR steuert macOS selbst
class DisplayManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList outputs READ outputs NOTIFY outputsChanged)
    Q_PROPERTY(bool canSwitchRefresh READ canSwitchRefresh CONSTANT)
    Q_PROPERTY(bool canSwitchHdr READ canSwitchHdr CONSTANT)
    Q_PROPERTY(QString backend READ backend CONSTANT)
    // Mehrere Bildschirme: Wiedergabe automatisch auf dem höchstauflösenden, Steuerung
    // auf dem kleinsten übrigen (Profil-Ausgabe "Automatisch")
    Q_PROPERTY(bool autoScreens READ autoScreens WRITE setAutoScreens NOTIFY outputsChanged)
    Q_PROPERTY(QString mainOutput READ mainOutput NOTIFY outputsChanged)
    Q_PROPERTY(QString controlOutput READ controlOutput NOTIFY outputsChanged)

public:
    explicit DisplayManager(QObject *parent = nullptr);
    ~DisplayManager() override;

    QVariantList outputs() const { return m_outputs; }
    bool canSwitchRefresh() const { return m_canRefresh; }
    bool canSwitchHdr() const { return m_canHdr; }
    QString backend() const { return m_backend; }

    Q_INVOKABLE void refresh();

    bool autoScreens() const { return m_autoScreens; }
    void setAutoScreens(bool on);
    // Ausgabe für "Automatisch" (leere Profil-Ausgabe) bzw. für das Steuerfenster (leer = keins)
    QString mainOutput() const;
    QString controlOutput() const;
    // Auswahlregeln (für Tests ohne echte Bildschirme)
    static QString pickMain(const QVariantList &outputs);
    static QString pickControl(const QVariantList &outputs, const QString &mainId);

    // Liefert die mpv-Optionen, die das native Player-Fenster auf dieses Gerät legen.
    QVariantMap mpvScreenOptions(const QString &outputId) const;
    QVariantMap output(const QString &outputId) const;

    // Stellt die Bildwiederholrate passend zur Quelle ein (z. B. 23.976 -> 23.976/24 Hz).
    // size: optionale Zielauflösung (z. B. 1920x2205 für HDMI Frame Packing),
    // sonst bleibt die aktuelle Auflösung.
    bool matchRefreshRate(const QString &outputId, double fps, QString *info = nullptr, const QSize &size = QSize());
    // Schaltet den HDR-Modus des Systems für dieses Gerät.
    bool setHdr(const QString &outputId, bool enabled);
    // Stellt alle geänderten Modi wieder her (beim Beenden / Gerätewechsel).
    void restoreAll();

    // Kleinstes ganzzahliges Vielfaches von fps unter den Raten (-1 = keins)
    static int pickRate(const QList<double> &rates, double fps);

signals:
    void outputsChanged();

private:
    QVariantList m_outputs;
    bool m_autoScreens = true;
    QString m_backend;
    bool m_canRefresh = false;
    bool m_canHdr = false;
    QHash<QString, bool> m_originalHdr;      // id -> ursprünglicher HDR-Zustand
    QHash<QString, QString> m_originalMode;  // id -> ursprünglicher Modus (plattformspezifisch)
};
