#pragma once

#include <QImage>
#include <QObject>
#include <QTimer>
#include <QVariant>
#include <atomic>
#include <mutex>

struct mpv_handle;

// Blu-ray-Wiedergabe über libbluray als mpv-Stream ("lumenbd://").
//
//   Menümodus  : libbluray führt das Menüprogramm aus (HDMV, BD-J mit Java)
//   Titelmodus : Playlist/Hauptfilm direkt – nötig für Blu-ray 3D, weil nur hier
//                die zweite Ansicht (MVC) zugemischt werden kann
//
//   libbluray (Basisansicht) ─┐
//   abhängige Ansicht (MVC) ──┴─ MvcMerger ─> TS-Strom ─> mpv/FFmpeg-mvc (SBS-Bild)
//   Menü-/Untertitelgrafik (ARGB) ─> overlay-add, im 3D-Modus einmal je Auge
//
// Die Disc wird ausschließlich über libbluray gelesen; eine ggf. nötige
// Entschlüsselung geschieht außerhalb (LibreDrive + externe AACS-Bibliothek).
class BlurayNav : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool active READ active NOTIFY stateChanged)
    Q_PROPERTY(bool menuMode READ menuMode NOTIFY stateChanged)
    Q_PROPERTY(bool menuVisible READ menuVisible NOTIFY stateChanged)
    Q_PROPERTY(bool popupAvailable READ popupAvailable NOTIFY stateChanged)
    Q_PROPERTY(bool still READ still NOTIFY stateChanged)
    Q_PROPERTY(int title READ title NOTIFY stateChanged)
    Q_PROPERTY(int playlist READ playlist NOTIFY stateChanged)
    Q_PROPERTY(int chapter READ chapter NOTIFY positionChanged)
    Q_PROPERTY(QVariantList chapters READ chapters NOTIFY stateChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY stateChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    // Das Menü ließ sich nicht starten, es läuft der Hauptfilm (status nennt den Grund)
    Q_PROPERTY(bool menuFallback READ menuFallback NOTIFY stateChanged)
    Q_PROPERTY(bool mvcActive READ mvcActive NOTIFY mvcChanged)
    Q_PROPERTY(bool baseViewRight READ baseViewRight NOTIFY mvcChanged)
    Q_PROPERTY(int subtitleDepth READ subtitleDepth WRITE setSubtitleDepth NOTIFY subtitleDepthChanged)

public:
    explicit BlurayNav(QObject *parent = nullptr);
    ~BlurayNav() override;

    static bool available();

    // Pro mpv-Instanz: Protokoll registrieren bzw. vor dem Zerstören lösen
    void attach(mpv_handle *mpv);
    void detach();
    // mode: "menu" | "main" | "playlist". Liefert die URL, die mpv laden soll.
    QString prepare(const QString &device, const QString &mode = QStringLiteral("menu"), int playlist = -1);
    // 3D: gewünscht? und welches Ausgabeformat (sbs2l, sbsl, ab2l, abl, fp, ml …)
    void setStereo(bool want3d, const QString &layout);

    bool active() const { return m_active; }
    bool menuMode() const { return m_mode == QLatin1String("menu"); }
    bool menuVisible() const { return m_menuVisible; }
    bool popupAvailable() const { return m_popupAvailable; }
    bool still() const { return m_still; }
    int title() const { return m_title; }
    int playlist() const { return m_playlist; }
    int chapter() const { return m_chapter; }
    QVariantList chapters() const { return m_chapters; }
    double position() const { return m_position; }
    double duration() const { return m_duration; }
    QString status() const { return m_status; }
    bool menuFallback() const { return m_menuFallback; }
    bool mvcActive() const { return m_mvcActive; }
    bool baseViewRight() const { return m_baseRight; }
    int subtitleDepth() const { return m_subtitleDepth; }
    void setSubtitleDepth(int px);

    // Fernbedienung: up, down, left, right, enter, menu, popup, 0-9
    Q_INVOKABLE bool key(const QString &name);
    Q_INVOKABLE void seek(double seconds);
    Q_INVOKABLE void seekRelative(double delta);
    Q_INVOKABLE void setChapter(int index);
    Q_INVOKABLE void nextChapter();
    Q_INVOKABLE void prevChapter();
    // 3D-Untertitel (von libbluray gerendert): Stream per PID wählen, 0 = aus
    void selectSubtitlePid(int pid);

    // Maus in OSD-Koordinaten des Player-Fensters
    void mouseMove(double x, double y);
    void mouseClick(double x, double y);
    // Videobereich im Player-Fenster (mpv "osd-dimensions")
    void setOsdDimensions(const QVariantMap &dims);

signals:
    void stateChanged();
    void positionChanged();
    void statusChanged();
    void mvcChanged();
    void subtitleDepthChanged();
    // Stream-Wahl aus dem Disc-Menü -> mpv-Spur mit dieser PID wählen
    void audioPidSelected(int pid);
    void subtitlePidSelected(int pid, bool enabled);

private:
    struct Session;
    friend struct Session;

    struct EyeRect { double x, y, w, h; };

    static int openStream(void *userData, char *uri, void *info);
    void closeSession(Session *s);
    void onOverlay(const QImage &plane, const QRect &box);
    void updateOverlay();
    QList<EyeRect> eyeRects() const;
    void setStatus(const QString &s);
    bool mapToPlane(double x, double y, int *px, int *py) const;
    void dropBuffers();
    void pollPosition();

    mpv_handle *m_mpv = nullptr;
    QString m_device;
    QString m_mode = QStringLiteral("menu");
    int m_requestedPlaylist = -1;

    std::mutex m_mutex; // schützt m_session gegen close aus dem mpv-Thread
    Session *m_session = nullptr;

    QTimer m_poll;
    bool m_active = false;
    bool m_menuVisible = false;
    bool m_popupAvailable = false;
    bool m_still = false;
    int m_title = -1;
    int m_playlist = -1;
    int m_chapter = -1;
    QVariantList m_chapters;
    double m_position = 0;
    double m_duration = 0;
    QString m_status;
    bool m_menuFallback = false;

    // 3D
    std::atomic_bool m_want3d{false};
    QString m_layout = QStringLiteral("none");
    bool m_mvcActive = false;
    bool m_baseRight = false;
    int m_subtitleDepth = 0;

    QImage m_overlay;       // aktuell angezeigt (mpv kopiert beim overlay-add)
    QRect m_overlayBox;     // in Plane-Koordinaten
    QSize m_planeSize{1920, 1080};
    QVariantMap m_osd;
};
