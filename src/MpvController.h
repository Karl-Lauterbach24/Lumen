#pragma once

#include <QObject>
#include <QUrl>
#include <QVariant>

struct mpv_handle;
struct mpv_event;
class BlurayNav;
class DisplayManager;
class PlayerWindow;

// Steuert eine libmpv-Instanz. Das Player-Fenster ist entweder mpv's eigenes
// natives Fenster (gpu-next / d3d11 / vulkan / wayland – volle HDR-Ausgabe) oder
// ein eingebettetes Qt-Fenster über die Render-API (macOS, oder per Profil).
class MpvController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool embedded READ embedded NOTIFY profileApplied)
    Q_PROPERTY(bool idle READ idle NOTIFY idleChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY pausedChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(double volume READ volume NOTIFY volumeChanged)
    Q_PROPERTY(double volumeMax READ volumeMax NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted NOTIFY mutedChanged)
    Q_PROPERTY(double speed READ speed NOTIFY speedChanged)
    Q_PROPERTY(QString mediaTitle READ mediaTitle NOTIFY mediaChanged)
    Q_PROPERTY(QString path READ path NOTIFY mediaChanged)
    Q_PROPERTY(bool isDisc READ isDisc NOTIFY mediaChanged)
    Q_PROPERTY(QString device READ device NOTIFY mediaChanged)
    Q_PROPERTY(QVariantList titles READ titles NOTIFY titlesChanged)
    Q_PROPERTY(int currentTitle READ currentTitle NOTIFY titlesChanged)
    Q_PROPERTY(QVariantList chapters READ chapters NOTIFY chaptersChanged)
    Q_PROPERTY(int currentChapter READ currentChapter NOTIFY currentChapterChanged)
    Q_PROPERTY(QVariantList audioTracks READ audioTracks NOTIFY tracksChanged)
    Q_PROPERTY(QVariantList subtitleTracks READ subtitleTracks NOTIFY tracksChanged)
    Q_PROPERTY(int audioId READ audioId NOTIFY tracksChanged)
    Q_PROPERTY(int subtitleId READ subtitleId NOTIFY tracksChanged)
    Q_PROPERTY(QVariantMap videoInfo READ videoInfo NOTIFY videoInfoChanged)
    Q_PROPERTY(QVariantMap audioInfo READ audioInfo NOTIFY audioInfoChanged)
    Q_PROPERTY(QVariantList audioDevices READ audioDevices NOTIFY audioDevicesChanged)
    Q_PROPERTY(double loopA READ loopA NOTIFY loopChanged)
    Q_PROPERTY(double loopB READ loopB NOTIFY loopChanged)
    Q_PROPERTY(bool fullscreen READ fullscreen NOTIFY fullscreenChanged)
    Q_PROPERTY(double audioDelay READ audioDelay NOTIFY delaysChanged)
    Q_PROPERTY(double subDelay READ subDelay NOTIFY delaysChanged)
    Q_PROPERTY(bool buffering READ buffering NOTIFY bufferingChanged)
    Q_PROPERTY(double cacheSeconds READ cacheSeconds NOTIFY bufferingChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QVariantMap profile READ profile NOTIFY profileApplied)
    Q_PROPERTY(QString stereoInput READ stereoInput WRITE setStereoInput NOTIFY stereoInputChanged)
    Q_PROPERTY(QString outputStatus READ outputStatus NOTIFY outputStatusChanged)
    // FFmpeg mit H.264/MVC-Decoder (FFmpeg-mvc) geladen?
    Q_PROPERTY(bool mvcCapable READ mvcCapable NOTIFY profileApplied)
    // Blu-ray 3D läuft gerade mit beiden Ansichten
    Q_PROPERTY(bool mvcActive READ mvcActive NOTIFY mvcActiveChanged)

public:
    explicit MpvController(DisplayManager *displays, BlurayNav *nav, QObject *parent = nullptr);
    ~MpvController() override;

    bool embedded() const { return m_window != nullptr; }
    // "auto" -> eingebettet nur dort, wo libmpv kein eigenes Fenster öffnen kann
    static bool wantsEmbedded(const QVariantMap &profile);

    bool initialize(const QVariantMap &profile);
    void shutdown();

    // Profil anwenden (Ausgabegerät, HDR, Audio-Passthrough, 3D …)
    Q_INVOKABLE void applyProfile(const QVariantMap &profile);

    // Quellen
    Q_INVOKABLE void openDisc(const QString &device, const QString &target = QStringLiteral("longest"));
    Q_INVOKABLE void openPlaylist(const QString &device, int playlist);
    // Mit Disc-Menü (libbluray-Navigation) starten
    Q_INVOKABLE void openDiscMenu(const QString &device);
    Q_INVOKABLE void openFile(const QUrl &url);
    // Kommandozeile: lokaler Pfad (Datei/ISO/Ordner/Laufwerk) oder mpv-URL
    void openLocation(const QString &location);

    // Transport
    Q_INVOKABLE void togglePause();
    Q_INVOKABLE void setPaused(bool p);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void seek(double seconds, bool relative = false);
    Q_INVOKABLE void seekExact(double seconds);
    Q_INVOKABLE void frameStep();
    Q_INVOKABLE void frameBackStep();
    Q_INVOKABLE void nextChapter();
    Q_INVOKABLE void prevChapter();
    Q_INVOKABLE void setChapter(int index);
    Q_INVOKABLE void setTitle(int index);
    Q_INVOKABLE void cycleAbLoop();
    Q_INVOKABLE void clearAbLoop();

    // Audio / Untertitel
    Q_INVOKABLE void setVolume(double v);
    Q_INVOKABLE void setMuted(bool m);
    Q_INVOKABLE void setSpeed(double s);
    Q_INVOKABLE void setAudioId(int id);
    Q_INVOKABLE void setSubtitleId(int id);
    Q_INVOKABLE void setAudioDelay(double s);
    Q_INVOKABLE void setSubDelay(double s);

    // Fenster / Bild
    Q_INVOKABLE void toggleFullscreen();
    Q_INVOKABLE void screenshot();
    Q_INVOKABLE void toggleStats();

    // Generischer Zugriff (Bild-Regler, Seitenverhältnis, Deinterlacing …)
    Q_INVOKABLE void setOption(const QString &name, const QVariant &value);
    Q_INVOKABLE QVariant getProperty(const QString &name) const;
    Q_INVOKABLE void command(const QStringList &args);
    Q_INVOKABLE void showText(const QString &text, int ms = 1500);

    bool idle() const { return m_idle; }
    bool paused() const { return m_paused; }
    double position() const { return m_position; }
    double duration() const { return m_duration; }
    double volume() const { return m_volume; }
    double volumeMax() const { return m_volumeMax; }
    bool muted() const { return m_muted; }
    double speed() const { return m_speed; }
    QString mediaTitle() const { return m_mediaTitle; }
    QString path() const { return m_path; }
    QString device() const { return m_currentDevice; }
    bool isDisc() const { return m_path.startsWith(QLatin1String("bd://")) || m_path.startsWith(QLatin1String("lumenbd://")); }
    QVariantList titles() const { return m_titles; }
    int currentTitle() const { return m_currentTitle; }
    QVariantList chapters() const { return m_chapters; }
    int currentChapter() const { return m_currentChapter; }
    QVariantList audioTracks() const { return m_audioTracks; }
    QVariantList subtitleTracks() const { return m_subtitleTracks; }
    int audioId() const { return m_aid; }
    int subtitleId() const { return m_sid; }
    QVariantMap videoInfo() const { return m_videoInfo; }
    QVariantMap audioInfo() const { return m_audioInfo; }
    QVariantList audioDevices() const { return m_audioDevices; }
    double loopA() const { return m_loopA; }
    double loopB() const { return m_loopB; }
    bool fullscreen() const { return m_fullscreen; }
    double audioDelay() const { return m_audioDelay; }
    double subDelay() const { return m_subDelay; }
    bool buffering() const { return m_buffering; }
    double cacheSeconds() const { return m_cacheSeconds; }
    QString lastError() const { return m_lastError; }
    QVariantMap profile() const { return m_profile; }
    QString stereoInput() const { return m_stereoIn; }
    void setStereoInput(const QString &format);
    QString outputStatus() const { return m_outputStatus; }
    bool mvcCapable() const { return m_mvcCapable; }
    bool mvcActive() const;
    // 3D-Ausgabe gewünscht und möglich (Profil + FFmpeg-mvc + libbluray)
    bool want3D() const;
    static QString stereoFilter(const QString &in, const QString &out);
    static QString stereoOutLabel(const QString &out);
    static bool isDisc3D(const QString &device);

signals:
    void mvcActiveChanged();
    void stereoInputChanged();
    void outputStatusChanged();
    void idleChanged();
    void pausedChanged();
    void positionChanged();
    void durationChanged();
    void volumeChanged();
    void mutedChanged();
    void speedChanged();
    void mediaChanged();
    void titlesChanged();
    void chaptersChanged();
    void currentChapterChanged();
    void tracksChanged();
    void videoInfoChanged();
    void audioInfoChanged();
    void audioDevicesChanged();
    void loopChanged();
    void fullscreenChanged();
    void delaysChanged();
    void bufferingChanged();
    void lastErrorChanged();
    void profileApplied();
    void fileLoaded();
    void shutdownRequested();

private slots:
    void onMpvEvents();
    void onMvcChanged();

private:
    bool create(const QVariantMap &options);
    void destroy();
    void restart(const QVariantMap &options);
    void observeAll();
    void handleEvent(mpv_event *ev);
    void handleProperty(quint64 id, int format, void *data);
    void loadFile(const QString &url, const QVariantMap &fileOptions = {});
    QVariantMap buildOptions(const QVariantMap &profile) const;
    void setOptionRaw(const QString &name, const QVariant &value, bool preInit);
    void rebuildTracks(const QVariantList &list);
    void updateVideoInfo();
    void onContentFormatKnown();
    void setError(const QString &e);
    QString writeInputConf() const;
    void handleClientMessage(const QStringList &args);
    void selectTrackByPid(const QString &type, int pid);
    int trackIdForPid(const QString &type, int pid) const;
    void openNavStream(const QString &device, const QString &mode, int playlist);
    void placeEmbeddedWindow();

    DisplayManager *m_displays = nullptr;
    BlurayNav *m_nav = nullptr;
    PlayerWindow *m_window = nullptr;
    mpv_handle *m_mpv = nullptr;
    QVariantList m_rawTracks;
    QString m_pendingUrl;          // wartet auf den Render-Kontext (eingebettetes Fenster)
    QVariantMap m_pendingOptions;
    int m_dvProfile = 0;
    double m_mouseX = 0;
    double m_mouseY = 0;
    bool m_quitting = false;

    QVariantMap m_profile;
    QVariantMap m_appliedOptions;
    QString m_currentDevice;

    bool m_idle = true;
    bool m_paused = false;
    double m_position = 0;
    int m_positionBucket = -1;
    double m_duration = 0;
    double m_volume = 100;
    double m_volumeMax = 130;
    bool m_muted = false;
    double m_speed = 1.0;
    QString m_mediaTitle;
    QString m_path;
    QVariantList m_titles;
    int m_currentTitle = -1;
    QVariantList m_chapters;
    int m_currentChapter = -1;
    QVariantList m_audioTracks;
    QVariantList m_subtitleTracks;
    int m_aid = 0;
    int m_sid = 0;
    QVariantMap m_videoParams;
    QString m_videoCodec;
    QString m_hwdec;
    double m_containerFps = 0;
    double m_displayFps = 0;
    QVariantMap m_videoInfo;
    QVariantMap m_audioInfo;
    QVariantList m_audioDevices;
    double m_loopA = -1;
    double m_loopB = -1;
    bool m_fullscreen = false;
    double m_audioDelay = 0;
    double m_subDelay = 0;
    bool m_buffering = false;
    double m_cacheSeconds = 0;
    QString m_lastError;
    QString m_stereoIn = QStringLiteral("none");
    bool m_autoStereo = false;   // Quellformat automatisch aus MVC gesetzt
    bool m_mvcCapable = false;
    QString m_outputStatus;
    double m_matchedFps = 0;
    int m_hdrState = -1; // -1 unbekannt, 0 SDR, 1 HDR
};
