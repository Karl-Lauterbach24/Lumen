#pragma once

#include <QImage>
#include <QObject>
#include <QTimer>
#include <QVariant>

#include <atomic>
#include <memory>
#include <mutex>

struct mpv_handle;

// DVD-Video über libdvdnav als mpv-Stream ("lumendvd://") – mit Disc-Menüs.
//
//   libdvdnav (VM, Menüs, Zellen) ─> MPEG-PS ─> mpv/FFmpeg (Bild, Ton)
//                                 └─ Subpicture-Pakete ─> eigener SPU-Dekoder
//                                     (Palette aus der IFO, Button-Hervorhebung)
//                                     ─> overlay-add, zeitgenau zur Wiedergabe
//
// Die Subpicture-Pakete werden im Strom durch Füllpakete ersetzt: mpv kennt die
// DVD-Palette nicht, Lumen zeichnet Menüs und Untertitel selbst.
// Lumen umgeht keinen Kopierschutz; libdvdread liest die Disc mit der vom
// System bereitgestellten Umgebung.
class DvdNav : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool active READ active NOTIFY stateChanged)
    Q_PROPERTY(bool menuMode READ menuMode NOTIFY stateChanged)
    Q_PROPERTY(bool menuVisible READ menuVisible NOTIFY stateChanged)
    Q_PROPERTY(bool popupAvailable READ popupAvailable NOTIFY stateChanged)
    Q_PROPERTY(bool still READ still NOTIFY stateChanged)
    Q_PROPERTY(int title READ title NOTIFY stateChanged)
    Q_PROPERTY(int titles READ titles NOTIFY stateChanged)
    Q_PROPERTY(int playlist READ playlist CONSTANT)
    Q_PROPERTY(int chapter READ chapter NOTIFY positionChanged)
    Q_PROPERTY(QVariantList chapters READ chapters NOTIFY stateChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY stateChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString discTitle READ discTitle NOTIFY stateChanged)
    Q_PROPERTY(QVariantList audioStreams READ audioStreams NOTIFY streamsChanged)
    Q_PROPERTY(QVariantList subtitleStreams READ subtitleStreams NOTIFY streamsChanged)
    Q_PROPERTY(int subtitleStream READ subtitleStream NOTIFY streamsChanged)
    Q_PROPERTY(int angle READ angle NOTIFY streamsChanged)
    Q_PROPERTY(int angles READ angles NOTIFY streamsChanged)
    // Kompatibel zur Blu-ray-Navigation (QML nutzt beide gleich)
    Q_PROPERTY(bool mvcActive READ mvcActive CONSTANT)
    Q_PROPERTY(int subtitleDepth READ subtitleDepth CONSTANT)

public:
    explicit DvdNav(QObject *parent = nullptr);
    ~DvdNav() override;

    static bool available();
    // DVD-Struktur (VIDEO_TS) oder DVD-ISO?
    static bool isDvd(const QString &path);
    // Titelliste für die Anzeige (Hintergrund-Thread geeignet)
    static QVariantMap scan(const QString &device);

    void attach(mpv_handle *mpv);
    void detach();
    // mode: "menu" | "main" | "title". Liefert die URL, die mpv laden soll.
    QString prepare(const QString &device, const QString &mode = QStringLiteral("menu"), int title = -1);

    bool active() const { return m_active; }
    bool menuMode() const { return true; }
    bool menuVisible() const { return m_menuVisible; }
    bool popupAvailable() const { return m_active; }
    bool still() const { return m_still; }
    int title() const { return m_title; }
    int titles() const { return m_titleCount; }
    int playlist() const { return -1; }
    int chapter() const { return m_chapter; }
    QVariantList chapters() const { return m_chapters; }
    double position() const { return m_position; }
    double duration() const { return m_duration; }
    QString status() const { return m_status; }
    QString discTitle() const { return m_discTitle; }
    QVariantList audioStreams() const { return m_audioStreams; }
    QVariantList subtitleStreams() const { return m_subStreams; }
    int subtitleStream() const { return m_userSpu; }
    int angle() const { return m_angle; }
    int angles() const { return m_angles; }
    bool mvcActive() const { return false; }
    int subtitleDepth() const { return 0; }
    QString device() const { return m_device; }

    // Fernbedienung: up, down, left, right, enter, menu (Hauptmenü), popup (Titelmenü),
    // audio, subtitle, angle (DVD-Menüs), 0-9
    Q_INVOKABLE bool key(const QString &name);
    Q_INVOKABLE void seek(double seconds);
    Q_INVOKABLE void seekRelative(double delta);
    Q_INVOKABLE void setChapter(int index);
    Q_INVOKABLE void nextChapter();
    Q_INVOKABLE void prevChapter();
    Q_INVOKABLE void playTitle(int title);
    // physische Subpicture-Nummer (0..31), -1 = aus (erzwungene bleiben sichtbar)
    Q_INVOKABLE void selectSubtitle(int physical);
    // mpv-src-id der Tonspur -> DVD-Tonspur (für Menü/SPRM konsistent halten)
    Q_INVOKABLE void noteAudioSelected(int srcId);
    Q_INVOKABLE void setAngle(int angle);
    // Beschriftung einer Tonspur (Sprache/Format) aus der IFO
    QString audioLabel(int srcId) const;

    void mouseMove(double x, double y);
    void mouseClick(double x, double y);
    void setOsdDimensions(const QVariantMap &dims);

signals:
    void stateChanged();
    void positionChanged();
    void statusChanged();
    void streamsChanged();
    // Wahl im Disc-Menü -> mpv-Spur mit dieser src-id wählen
    void audioPidSelected(int srcId);

private:
    struct Session;
    friend struct Session;
    struct Spu;

    static int openStream(void *userData, char *uri, void *info);
    void closeSession(Session *s);
    void setStatus(const QString &s);
    void dropBuffers();
    void pollPosition();
    void refreshStreams();
    void updateHighlight();
    void onSpu(const std::shared_ptr<Spu> &spu);
    void tick();
    void render();
    void pushOverlay(const QImage &img);
    bool mapToVideo(double x, double y, int *px, int *py) const;

    mpv_handle *m_mpv = nullptr;
    QString m_device;
    QString m_mode = QStringLiteral("menu");
    int m_requestedTitle = -1;

    std::mutex m_mutex;
    Session *m_session = nullptr;

    QTimer m_poll;
    QTimer m_clock;
    bool m_active = false;
    bool m_menuVisible = false;
    bool m_menuDomain = false;
    bool m_still = false;
    int m_title = -1;
    int m_titleCount = 0;
    int m_chapter = -1;
    QVariantList m_chapters;
    double m_position = 0;
    double m_duration = 0;
    QString m_status;
    QString m_discTitle;
    QVariantList m_audioStreams;
    QVariantList m_subStreams;
    int m_userSpu = -1;
    int m_angle = 1;
    int m_angles = 1;

    // Subpicture / Hervorhebung
    quint32 m_clut[16] = {};
    std::shared_ptr<Spu> m_shown;       // aktuell sichtbar
    QList<std::shared_ptr<Spu>> m_queue; // wartet auf seinen Zeitpunkt
    struct Highlight { bool on = false; int sx = 0, sy = 0, ex = 0, ey = 0; quint32 palette = 0; };
    Highlight m_hl;
    QSize m_videoSize{720, 576};
    QImage m_overlay;
    QVariantMap m_osd;
    bool m_overlayVisible = false;
};
