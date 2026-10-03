#pragma once

#include "DcpCrypto.h"
#include "DcpPackage.h"
#include "DcpSubtitles.h"

#include <QElapsedTimer>
#include <QSet>
#include <QHash>
#include <QImage>
#include <QTimer>
#include <QObject>
#include <QUrl>
#include <QVariant>

#include <atomic>
#include <memory>

class DisplayManager;
class MpvController;

// Digital Cinema Packages abspielen (QML: "Dcp").
//
//   Paket öffnen ─> CPLs (Spielfilm, Trailer, …) mit Rollen, Markern, Status
//   Wiedergabe   ─> mpv-EDL über alle Rollen:  Bild (J2K, XYZ) | Ton (PCM)
//                   Untertitel -> ASS, Marker -> Kapitel, 3D -> zwei Bildspuren
//   Schlüssel    ─> KDMs für das Lumen-Zertifikat, entschlüsselt per RSA-OAEP;
//                   Inhalte werden erst beim Lesen (lumendcp://) entschlüsselt
//   Prüfung      ─> SHA-1 aller Spurdateien gegen die Packing List
class DcpManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool cryptoAvailable READ cryptoAvailable CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString root READ root NOTIFY packageChanged)
    Q_PROPERTY(QVariantList cpls READ cpls NOTIFY packageChanged)
    Q_PROPERTY(QString error READ error NOTIFY packageChanged)
    Q_PROPERTY(QVariantMap identity READ identity NOTIFY identityChanged)
    Q_PROPERTY(QVariantList kdms READ kdms NOTIFY keysChanged)
    Q_PROPERTY(int keyCount READ keyCount NOTIFY keysChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(QVariantMap current READ current NOTIFY activeChanged)
    Q_PROPERTY(double fader READ fader WRITE setFader NOTIFY faderChanged)
    Q_PROPERTY(QString audioRoute READ audioRoute WRITE setAudioRoute NOTIFY audioRouteChanged)
    Q_PROPERTY(QString decodeMode READ decodeMode WRITE setDecodeMode NOTIFY decodeModeChanged)
    // Immersive Audio (Atmos/IAB): Renderer vorhanden, Lautsprecherlayout, Spur aktiv
    Q_PROPERTY(bool iabAvailable READ iabAvailable CONSTANT)
    Q_PROPERTY(QVariantList iabLayouts READ iabLayouts CONSTANT)
    Q_PROPERTY(QString iabLayout READ iabLayout WRITE setIabLayout NOTIFY iabLayoutChanged)
    Q_PROPERTY(bool iabActive READ iabActive NOTIFY activeChanged)
    Q_PROPERTY(int reduction READ reduction NOTIFY activeChanged)
    Q_PROPERTY(int skipPlanes READ skipPlanes NOTIFY activeChanged)
    Q_PROPERTY(bool verifying READ verifying NOTIFY verifyChanged)
    Q_PROPERTY(double verifyProgress READ verifyProgress NOTIFY verifyChanged)
    Q_PROPERTY(QVariantMap verifyResult READ verifyResult NOTIFY verifyChanged)

public:
    DcpManager(MpvController *player, DisplayManager *displays, QObject *parent = nullptr);
    ~DcpManager() override;

    static bool isDcp(const QString &path);

    bool cryptoAvailable() const { return DcpCrypto::available(); }
    bool busy() const { return m_busy; }
    QString root() const { return m_package.root; }
    QVariantList cpls() const;
    QString error() const { return m_package.error; }
    QVariantMap identity() const;
    QVariantList kdms() const;
    int keyCount() const { return int(m_keys.size()); }
    QString status() const { return m_status; }
    bool active() const { return m_active; }
    QVariantMap current() const { return m_current; }
    double fader() const { return m_fader; }
    void setFader(double f);
    QString audioRoute() const { return m_route; }
    void setAudioRoute(const QString &r);
    QString decodeMode() const { return m_decodeMode; }
    void setDecodeMode(const QString &m);
    bool iabAvailable() const;
    QVariantList iabLayouts() const;
    QString iabLayout() const { return m_iabLayout; }
    void setIabLayout(const QString &layout);
    bool iabActive() const { return m_iabActive; }
    int reduction() const { return m_reduction; }
    // ausgelassene Bit-Ebenen je Codeblock (Entlastung des Decoders ohne Verlust an Auflösung)
    int skipPlanes() const { return m_skipPlanes; }
    bool verifying() const { return m_verifying; }
    double verifyProgress() const { return m_verifyProgress; }
    QVariantMap verifyResult() const { return m_verifyResult; }

    // Paket laden (Ordner, ASSETMAP oder CPL); autoplay: danach erste CPL abspielen
    Q_INVOKABLE void open(const QString &path, bool autoplay = false);
    Q_INVOKABLE void close();
    Q_INVOKABLE bool play(int index, double start = 0);
    // Schlüssel außerhalb ihres KDM-Zeitraums für diese Komposition trotzdem verwenden (bis zum
    // Ende der Sitzung). Nur für Schlüssel, die vorliegen – an einem fehlenden KDM ändert das nichts.
    Q_INVOKABLE void allowOutsideWindow(int index, bool allow);

    Q_INVOKABLE void loadKdm(const QUrl &file);
    Q_INVOKABLE void removeKdm(int index);
    // Schlüssel direkt eintragen (eigene DCPs): "Key-ID-UUID  32-stelliger-Hex-Schlüssel"
    Q_INVOKABLE bool addKey(const QString &keyId, const QString &keyHex);
    Q_INVOKABLE int importKeys(const QUrl &file);

    Q_INVOKABLE bool createIdentity(const QString &organisation);
    Q_INVOKABLE bool importIdentity(const QUrl &cert, const QUrl &key);
    Q_INVOKABLE bool exportCertificate(const QUrl &target, bool chain);

    Q_INVOKABLE void verify(int index);
    Q_INVOKABLE void cancelVerify();

    // Dolby/Cinema-Fader (7.0 = Referenzpegel) -> dB
    static double faderToDb(double fader);
    // pan-Filter für DCP-Kanalbelegungen (SMPTE 428-12)
    static QString routeFilter(int channels, const QString &route);

signals:
    void iabLayoutChanged();
    void busyChanged();
    void packageChanged();
    void identityChanged();
    void keysChanged();
    void statusChanged();
    void activeChanged();
    void faderChanged();
    void audioRouteChanged();
    void decodeModeChanged();
    void verifyChanged();

private:
    QString configDir() const;
    void loadStoredKdms();
    void rebuildKeys();
    void setStatus(const QString &s);
    QVariantMap keyStatus(const Dcp::Cpl &cpl) const;
    int chooseReduction(const Dcp::Cpl &cpl) const;
    int startRelief(const Dcp::Cpl &cpl) const;
    double pictureRate(const Dcp::Cpl &cpl) const;
    QString reliefKey(const Dcp::Cpl &cpl) const;
    QString decoderOptions() const;
    void setRelief(int level);
    void onDroppedFrames();
    void updateImageSubtitle();

    MpvController *m_player = nullptr;
    DisplayManager *m_displays = nullptr;
    Dcp::Package m_package;
    int m_generation = 0;
    bool m_busy = false;
    QString m_status;

    DcpCrypto::Identity m_identity;
    QList<DcpCrypto::Kdm> m_kdms;
    QHash<QString, DcpCrypto::ContentKey> m_keys;       // aus KDMs
    QHash<QString, DcpCrypto::ContentKey> m_manualKeys; // von Hand eingetragen
    QSet<QString> m_outsideWindow; // CPL-IDs: Schlüssel außerhalb des KDM-Zeitraums trotzdem verwenden

    bool m_active = false;
    QVariantMap m_current;
    int m_playing = -1;
    int m_reduction = 0;
    int m_baseReduction = 0; // Auflösungsstufe nach Ausgabe oder Wahl, ohne Entlastung
    int m_skipPlanes = 0;
    int m_relief = 0;        // Stufe der Entlastung (Tuning::j2kRelief), nur in der Automatik
    int m_channels = 0;
    QElapsedTimer m_playClock;
    // verlorene Bilder: Stand und Zeitpunkt, ab dem gezählt wird
    int m_dropMark = 0;
    qint64 m_dropMarkMs = 0;
    qint64 m_reliefHoldMs = 0;

    // Bilduntertitel (PNG) als Overlay
    QList<Dcp::ImageSub> m_imageSubs;
    QTimer m_imageClock;
    int m_imageShown = -1;
    int m_imageAlpha = -1;
    QImage m_imageCache;
    QSize m_pictureSize{2048, 1080};

    double m_fader = 7.0;
    QString m_route = QStringLiteral("auto");
    QString m_iabLayout;
    bool m_iabActive = false;
    QString m_decodeMode = QStringLiteral("auto");

    bool m_verifying = false;
    double m_verifyProgress = 0;
    QVariantMap m_verifyResult;
    std::shared_ptr<std::atomic_bool> m_verifyCancel;
};
