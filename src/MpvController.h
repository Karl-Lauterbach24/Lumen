#pragma once

#include "CastOutput.h"
#include "Tuning.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QVariant>

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

struct mpv_handle;
struct mpv_event;
class BitmapSubs;
class BlurayNav;
class CastRenderer;
class DisplayManager;
class DvdNav;
class VcdNav;
class QImage;
class PlayerWindow;
class QThread;

// Steuert eine libmpv-Instanz. Das Player-Fenster ist entweder mpv's eigenes
// natives Fenster (gpu-next / d3d11 / vulkan / wayland – volle HDR-Ausgabe) oder
// ein eingebettetes Qt-Fenster über die Render-API (macOS, oder per Profil).
class MpvController : public QObject, public CastOutput
{
    Q_OBJECT
    Q_PROPERTY(bool embedded READ embedded NOTIFY profileApplied)
    // Ausgabe geht an einen Empfänger im Netz statt in ein Fenster
    Q_PROPERTY(bool casting READ casting NOTIFY profileApplied)
    Q_PROPERTY(bool idle READ idle NOTIFY idleChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY pausedChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(double volume READ volume NOTIFY volumeChanged)
    Q_PROPERTY(double volumeMax READ volumeMax NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted NOTIFY mutedChanged)
    Q_PROPERTY(double speed READ speed NOTIFY speedChanged)
    // Nachtmodus: Dynamik komprimieren (leise Stellen lauter, laute leiser); bleibt gespeichert
    Q_PROPERTY(bool nightMode READ nightMode WRITE setNightMode NOTIFY nightModeChanged)
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
    // 3D-Quellformat selbst erkennen (Angaben in der Datei, Dateiname, Bildvergleich); eine Wahl
    // von Hand schaltet das ab
    Q_PROPERTY(bool stereoAuto READ stereoAuto WRITE setStereoAuto NOTIFY stereoInputChanged)
    // Ergebnis der Erkennung als Text für die Oberfläche
    Q_PROPERTY(QString stereoStatus READ stereoStatus NOTIFY stereoInputChanged)
    // Datei mit zwei Ansichten (H.264/MVC, z. B. MKV von einer Blu-ray 3D) läuft in 3D
    Q_PROPERTY(bool fileMvc READ fileMvc NOTIFY stereoInputChanged)
    // Was die Laufzeit-Anpassung zurückgenommen hat (leer: nichts)
    Q_PROPERTY(QString tuningStatus READ tuningStatus NOTIFY tuningChanged)
    // Erkannte Grafikhardware: renderer, class (software | integrated | discrete | unknown), cores
    Q_PROPERTY(QVariantMap hardware READ hardwareInfo CONSTANT)
    Q_PROPERTY(QString outputStatus READ outputStatus NOTIFY outputStatusChanged)
    // FFmpeg mit H.264/MVC-Decoder (FFmpeg-mvc) geladen?
    Q_PROPERTY(bool mvcCapable READ mvcCapable NOTIFY profileApplied)
    Q_PROPERTY(bool ytdlAvailable READ ytdlAvailable CONSTANT)
    // Blu-ray 3D läuft gerade mit beiden Ansichten
    Q_PROPERTY(bool mvcActive READ mvcActive NOTIFY mvcActiveChanged)
    // Art der Quelle: file, bluray, dvd, hddvd, vcd, svcd, cdda, dcp
    Q_PROPERTY(QString sourceKind READ sourceKind NOTIFY mediaChanged)
    Q_PROPERTY(int droppedFrames READ droppedFrames NOTIFY droppedFramesChanged)
    // Text-Untertitel einer 3D-Datei werden gerade je Auge gezeichnet (Tiefe einstellbar)
    Q_PROPERTY(bool stereoSubtitles READ stereoSubtitles NOTIFY stereoSubtitlesChanged)
    // Vorführprogramm (Show): Quellen nacheinander abspielen
    Q_PROPERTY(QVariantList queue READ queue NOTIFY queueChanged)
    Q_PROPERTY(int queueIndex READ queueIndex NOTIFY queueChanged)
    Q_PROPERTY(bool queueActive READ queueActive NOTIFY queueChanged)

public:
    explicit MpvController(DisplayManager *displays, BlurayNav *nav, QObject *parent = nullptr);
    ~MpvController() override;

    bool embedded() const { return m_window != nullptr; }
    // Player-Fenster eines Profils: "embedded"/"native" wie gewählt, "auto" nach embeddedByDefault()
    static bool wantsEmbedded(const QVariantMap &profile);
    // Auf diesem System gibt es nur das eingebettete Player-Fenster (macOS)
    // LumenOS: Das Player-Fenster füllt den Bildschirm und ist nur da, solange etwas läuft – davor und
    // danach gehört der Bildschirm der Oberfläche. Vor initialize() setzen.
    void setKiosk(bool on) { m_kiosk = on; }
    bool kiosk() const { return m_kiosk; }
    Q_INVOKABLE static bool embeddedOnly();
    // "Automatisch" heißt hier eingebettet (macOS, Wayland-Sitzungen)
    Q_INVOKABLE static bool embeddedByDefault();

    bool casting() const { return m_castEncoder != nullptr; }
    void setCastOutput(CastEncoder *encoder, const QString &pcmPath = {}) override;
    QString castTitle() const override { return m_mediaTitle; }

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
    // Netzwerk-Stream (http(s), HLS, DASH, rtsp, rtmp, srt, udp, Medienserver …); title/start optional
    Q_INVOKABLE void openStream(const QString &url, const QString &title = {}, double start = 0);
    bool ytdlAvailable() const { return !m_ytdl.isEmpty(); }
    // Beliebige Quelle erkennen und öffnen. mode: "auto" | "menu" | "main" | "title";
    // title: Titel/Playlist/CPL-Index (-1 = Hauptfilm)
    Q_INVOKABLE void openSource(const QString &path, const QString &mode = QStringLiteral("auto"), int title = -1);
    // DVD über libdvdnav (mode "menu", "main", "title"; title 1-basiert)
    Q_INVOKABLE void openDvd(const QString &device, const QString &mode = QStringLiteral("menu"), int title = -1);
    // Vorbereitete URL (EDL o. Ä.) mit Datei-Optionen laden
    Q_INVOKABLE void openPrepared(const QString &url, const QVariantMap &options, const QString &kind,
                                  const QString &device, const QString &stereoIn = QStringLiteral("none"));
    // Quelle erkennen: bluray, dvd, hddvd, vcd, svcd, cdda, dcp oder file
    Q_INVOKABLE static QString detectKind(const QString &path);

    // Vorführprogramm
    Q_INVOKABLE void queueAdd(const QString &path, const QString &label, int title = -1);
    Q_INVOKABLE void queueRemove(int index);
    Q_INVOKABLE void queueMove(int index, int delta);
    Q_INVOKABLE void queueClear();
    Q_INVOKABLE void queueStart(int index = 0);
    Q_INVOKABLE void queueStop();

    // Zusätzliche mpv-Protokolle (DCP, VCD …), je mpv-Instanz angemeldet
    void addProtocol(std::function<void(mpv_handle *)> attach);
    // Zusätzliche mpv-Optionen (Plugins), gelesen bei jedem Start der mpv-Instanz
    void addOptionProvider(std::function<QVariantMap()> provider);
    // Eigene Bild-Overlays (z. B. DCP-Bilduntertitel); img = ARGB32_Premultiplied
    void setOverlay(int id, const QImage &img, int x, int y, int w, int h);
    void removeOverlay(int id);
    QVariantMap osdDimensions() const { return m_osdDims; }
    void setDvdNav(DvdNav *dvd);
    void setVcdNav(VcdNav *vcd);
    void onOutputsChanged();

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
    bool nightMode() const { return m_nightMode; }
    void setNightMode(bool on);
    // Untertiteldatei (SRT, ASS, SUP …) zur laufenden Wiedergabe laden und auswählen
    Q_INVOKABLE void addSubtitleFile(const QUrl &file);
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
    // die aktuelle Datei ist geladen (zwischen "Datei geladen" und ihrem Ende)
    bool fileReady() const { return m_fileReady; }
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
    bool isDisc() const;
    QString sourceKind() const { return m_sourceKind; }
    int droppedFrames() const { return m_droppedFrames; }
    bool stereoSubtitles() const { return m_stereoSubs; }
    QVariantList queue() const { return m_queue; }
    int queueIndex() const { return m_queueIndex; }
    bool queueActive() const { return m_queueActive; }
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
    bool stereoAuto() const { return m_stereoAuto; }
    void setStereoAuto(bool on);
    QString stereoStatus() const;
    bool fileMvc() const { return m_fileMvc; }
    static QString stereoInLabel(const QString &format);
    QString tuningStatus() const { return m_tuningStatus; }
    QVariantMap hardwareInfo() const;
    QString outputStatus() const { return m_outputStatus; }
    bool mvcCapable() const { return m_mvcCapable; }
    bool mvcActive() const;
    // 3D-Ausgabe gewünscht und möglich (Profil + FFmpeg-mvc + libbluray)
    bool want3D() const;
    static QString stereoOutLabel(const QString &out);
    static bool isDisc3D(const QString &device);

signals:
    void mpvDestroying();
    void osdDimensionsChanged();
    void droppedFramesChanged();
    void stereoSubtitlesChanged();
    void queueChanged();
    // DCP-Paket öffnen (DcpManager), cpl < 0 = erste/Spielfilm-CPL
    void dcpRequested(const QString &path, int cpl);
    void mvcActiveChanged();
    void stereoInputChanged();
    void tuningChanged();
    void outputStatusChanged();
    void idleChanged();
    void pausedChanged();
    void positionChanged();
    void durationChanged();
    void volumeChanged();
    void mutedChanged();
    void speedChanged();
    void nightModeChanged();
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
    // script-message "lumen-plugin" … von einem mpv-Skript (Plugin-System)
    void pluginMessage(const QStringList &args);
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
    void loadFileNow(const QString &url, const QVariantMap &options);
    void releaseBdOpenLock();
    QVariantMap buildOptions(const QVariantMap &profile) const;
    void setOptionRaw(const QString &name, const QVariant &value, bool preInit);
    void rebuildTracks(const QVariantList &list);
    void updateVideoInfo();
    void onContentFormatKnown();
    void setError(const QString &e);
    QString writeInputConf() const;
    void handleClientMessage(const QStringList &args);
    void selectTrackByPid(const QString &type, int pid);
    void syncDiscTracks();
    int trackIdForPid(const QString &type, int pid) const;
    void openNavStream(const QString &device, const QString &mode, int playlist);
    void placeEmbeddedWindow();
    void resetAutoStereo();
    // laufende mpv-Instanz auf den Stand von buildOptions() bringen (ohne Neustart)
    void syncOptions();
    Tuning::Context tuningContext() const;
    // automatische 3D-Erkennung
    void tryStereoDetection();
    void startStereoDetection();
    void runStereoAnalysis(const QString &path, const QString &url, const QVariantMap &fileOptions);
    void continueStereoDetection();
    void cancelStereoDetection();
    void applyDetectedStereo(const QString &format, const QString &source);
    // Laufzeit-Anpassung
    void tuneTick();
    void holdGovernor(double seconds);
    void updateTuningStatus();
    void advanceQueue();

    DisplayManager *m_displays = nullptr;
    BlurayNav *m_nav = nullptr;
    DvdNav *m_dvd = nullptr;
    VcdNav *m_vcd = nullptr;
    QString m_ytdl; // yt-dlp (Webseiten wie YouTube), falls installiert
    void updateVcdKeys();
    std::vector<std::function<void(mpv_handle *)>> m_protocols;
    std::vector<std::function<QVariantMap()>> m_optionProviders;
    QString m_sourceKind;
    QVariantMap m_osdDims;
    QString m_snapshotFile;        // Entwickler-Hilfe LUMEN_PLAYER_SNAPSHOT=<datei>@<sekunden>
    double m_snapshotAt = -1;
    QString m_lastUrl;             // für Neustarts der mpv-Instanz
    QVariantMap m_lastOptions;
    QString m_dvdMode = QStringLiteral("menu");
    int m_dvdTitle = -1;
    int m_droppedFrames = 0;
    int m_vo_drops = 0;
    int m_dec_drops = 0;
    int m_dropBase = 0;            // Stand der Zähler beim eigentlichen Start der Wiedergabe
    bool m_eof = false;
    QVariantList m_queue;
    int m_queueIndex = -1;
    bool m_queueActive = false;
    PlayerWindow *m_window = nullptr;
    CastEncoder *m_castEncoder = nullptr;
    CastRenderer *m_castRenderer = nullptr;
    QString m_castPcm;
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
    bool m_nightMode = false;
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
    QVariantMap m_decParams;     // video-dec-params: was der Decoder liefert, vor den Filtern
    qint64 m_delayedFrames = 0;
    qint64 m_mistimedFrames = 0;
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
    bool m_kiosk = false;
    bool m_kioskShow = false; // etwas wird geladen oder läuft: Player-Fenster zeigen
    bool m_kioskLoading = false; // loadfile ist abgeschickt, mpv hat noch nicht geantwortet
    bool m_cacheKnown = false; // mpv hat für die laufende Quelle einen Puffer gemeldet
    QString m_lastError;
    QString m_stereoIn = QStringLiteral("none");
    bool m_autoStereo = false;   // Quellformat automatisch aus MVC gesetzt
    bool m_mvcCapable = false;
    bool m_stereoAuto = true;
    QString m_stereoSource;      // woran erkannt: metadata | name | picture | size | mvc
    bool m_fileMvc = false;      // Datei mit zwei Ansichten wird mit beiden dekodiert
    qint64 m_loadedAt = 0;       // Zeitpunkt von "Datei geladen" (m_clock, ms)
    bool m_bdOpenStarted = false; // mpv hat mit der bd://-Disc begonnen (Ende der vorigen Datei ist vorbei)
    bool m_bdOpenLocked = false; // blurayOpenMutex gehalten, solange mpv eine bd://-Disc öffnet
    bool m_primedStart = false;  // Datei wurde angehalten geladen und läuft nach dem ersten Bild los
    bool m_mvcStream = false;    // der Videostrom der Datei ist H.264/MVC (auch wenn nur eine Ansicht läuft)
    bool m_mvcDemuxer = false;   // die laufende Datei wurde dafür mit FFmpegs Demuxer geladen
    bool m_detectWanted = false; // Datei geladen, wartet auf die Spurliste
    bool m_detectPending = false; // wartet auf die Bildgröße
    int m_detectGeneration = 0;
    QString m_detectPath;        // Datei, für die die Erkennung läuft
    std::shared_ptr<std::atomic_bool> m_detectCancel;
    QPointer<QThread> m_detectThread;
    // Erkennung vor dem Laden (3D-Profil oder "3D" im Namen): die Datei startet gleich im richtigen Format
    bool m_preloadWaiting = false;
    // Die Bildprüfung läuft neben dem Laden her (lokale Datei, Profil ohne 3D-Ausgabe)
    bool m_earlyAnalysis = false;
    bool m_detectPassed = false; // der Ablauf nach dem Laden wartet nur noch auf ihr Ergebnis
    void finishPrimedStart(int waitedMs);
    // Untertitel einer 3D-Datei je Auge (StereoSubs): Text von mpv, Bilder selbst gelesen (BitmapSubs)
    void updateStereoSubs();
    void drawStereoSubs();
    void placeEyeOverlays(const QImage &img, const QSizeF &canvas, const QRectF &where);
    QString m_subText;
    bool m_fileReady = false;
    bool m_endFileError = false; // die Meldung zum Abbruch steht; spätere Log-Zeilen überschreiben sie nicht
    bool m_stereoSubs = false;
    bool m_stereoBitmap = false;
    bool m_forcedSubsOnly = false;
    BitmapSubs *m_bitmapSubs = nullptr;
    double m_bitmapShown = -2; // Anfang des gezeigten Untertitelbilds (-1: keines)
    double m_demuxStart = -1;  // Anfangszeit der Datei, die mpv von allen Zeitstempeln abzieht
    bool m_havePreResult = false;
    QString m_preFormat, m_preSource;
    Tuning::Governor m_governor;
    QTimer m_tuneTimer;
    QElapsedTimer m_clock;
    QString m_tuningStatus;
    bool m_learnedLoaded = false;
    double m_tickPosition = -1;
    QString m_outputStatus;
    double m_matchedFps = 0;
    int m_hdrState = -1; // -1 unbekannt, 0 SDR, 1 HDR
};
