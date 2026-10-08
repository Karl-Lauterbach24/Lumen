#include "MpvController.h"
#include "Tr.h"

#include "BitmapSubs.h"
#include "BlurayNav.h"
#include "CastRenderer.h"
#include "DcpPackage.h"
#include "DisplayManager.h"
#include "DvdNav.h"
#include "OpticalMedia.h"
#include "PathUtil.h"
#include "PlayerWindow.h"
#include "Stereo3D.h"
#include "StereoDetect.h"
#include "StereoSubs.h"
#include "VcdNav.h"

extern "C" {
#include <libavutil/avutil.h>
}

#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include "ProfileManager.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QGuiApplication>
#include <QImage>
#include <QScreen>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QLocale>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>
#include <QUrl>
#include <QtDebug>

#include <mpv/client.h>

#include <cmath>
#include <utility>
#include <vector>

namespace {
// Nachtmodus: Lautheit angleichen (FFmpeg dynaudnorm); wirkt nicht bei Bitstream-Ausgabe
const char kNightFilter[] = "@lumen-night:lavfi=[dynaudnorm=f=250:g=13:p=0.75]";

enum PropId : quint64 {
    P_PAUSE = 1, P_TIMEPOS, P_DURATION, P_VOLUME, P_VOLMAX, P_MUTE, P_SPEED,
    P_MEDIATITLE, P_PATH, P_IDLE, P_DISCTITLES, P_DISCTITLE, P_CHAPTERLIST,
    P_CHAPTER, P_TRACKLIST, P_AID, P_SID, P_VIDEOPARAMS, P_VIDEOFORMAT, P_FPS, P_DECPARAMS, P_DELAYED, P_MISTIMED,
    P_HWDEC, P_DISPLAYFPS, P_AUDIOOUTPARAMS, P_AUDIOCODEC, P_AUDIODEVICES,
    P_ABA, P_ABB, P_FULLSCREEN, P_AUDIODELAY, P_SUBDELAY, P_CACHEPAUSE, P_CACHEDUR,
    P_OSDDIMS, P_MOUSEPOS, P_VODROPS, P_DECDROPS, P_EOF, P_WINDOWID, P_SUBTEXT, P_DEMUXSTART,
};

struct Observed
{
    const char *name;
    mpv_format format;
    PropId id;
};

const Observed kObserved[] = {
    {"pause", MPV_FORMAT_FLAG, P_PAUSE},
    {"time-pos", MPV_FORMAT_DOUBLE, P_TIMEPOS},
    {"duration", MPV_FORMAT_DOUBLE, P_DURATION},
    {"volume", MPV_FORMAT_DOUBLE, P_VOLUME},
    {"volume-max", MPV_FORMAT_DOUBLE, P_VOLMAX},
    {"mute", MPV_FORMAT_FLAG, P_MUTE},
    {"speed", MPV_FORMAT_DOUBLE, P_SPEED},
    {"media-title", MPV_FORMAT_STRING, P_MEDIATITLE},
    {"path", MPV_FORMAT_STRING, P_PATH},
    {"idle-active", MPV_FORMAT_FLAG, P_IDLE},
    {"disc-titles", MPV_FORMAT_NODE, P_DISCTITLES},
    {"disc-title", MPV_FORMAT_INT64, P_DISCTITLE},
    {"chapter-list", MPV_FORMAT_NODE, P_CHAPTERLIST},
    {"chapter", MPV_FORMAT_INT64, P_CHAPTER},
    {"track-list", MPV_FORMAT_NODE, P_TRACKLIST},
    {"aid", MPV_FORMAT_NODE, P_AID},
    {"sid", MPV_FORMAT_NODE, P_SID},
    {"video-params", MPV_FORMAT_NODE, P_VIDEOPARAMS},
    // für die Laufzeit-Anpassung: beobachtet statt abgefragt – eine Abfrage aus dem GUI-Thread
    // hält im eingebetteten Fenster den Renderer auf (er läuft in demselben Thread)
    {"video-dec-params", MPV_FORMAT_NODE, P_DECPARAMS},
    {"vo-delayed-frame-count", MPV_FORMAT_INT64, P_DELAYED},
    {"mistimed-frame-count", MPV_FORMAT_INT64, P_MISTIMED},
    {"video-format", MPV_FORMAT_STRING, P_VIDEOFORMAT},
    {"container-fps", MPV_FORMAT_DOUBLE, P_FPS},
    {"hwdec-current", MPV_FORMAT_STRING, P_HWDEC},
    {"display-fps", MPV_FORMAT_DOUBLE, P_DISPLAYFPS},
    {"audio-out-params", MPV_FORMAT_NODE, P_AUDIOOUTPARAMS},
    {"audio-codec-name", MPV_FORMAT_STRING, P_AUDIOCODEC},
    {"audio-device-list", MPV_FORMAT_NODE, P_AUDIODEVICES},
    {"ab-loop-a", MPV_FORMAT_NODE, P_ABA},
    {"ab-loop-b", MPV_FORMAT_NODE, P_ABB},
    {"fullscreen", MPV_FORMAT_FLAG, P_FULLSCREEN},
    {"audio-delay", MPV_FORMAT_DOUBLE, P_AUDIODELAY},
    {"sub-delay", MPV_FORMAT_DOUBLE, P_SUBDELAY},
    {"paused-for-cache", MPV_FORMAT_FLAG, P_CACHEPAUSE},
    {"demuxer-cache-duration", MPV_FORMAT_DOUBLE, P_CACHEDUR},
    {"osd-dimensions", MPV_FORMAT_NODE, P_OSDDIMS},
    {"mouse-pos", MPV_FORMAT_NODE, P_MOUSEPOS},
    {"frame-drop-count", MPV_FORMAT_INT64, P_VODROPS},
    {"decoder-frame-drop-count", MPV_FORMAT_INT64, P_DECDROPS},
    {"eof-reached", MPV_FORMAT_FLAG, P_EOF},
    {"window-id", MPV_FORMAT_INT64, P_WINDOWID},
    {"sub-text", MPV_FORMAT_STRING, P_SUBTEXT},
    {"demuxer-start-time", MPV_FORMAT_DOUBLE, P_DEMUXSTART},
};

// Optionen, die ein neues Player-Fenster / einen neuen Renderer erfordern
const QSet<QString> kRestartKeys = {
    QStringLiteral("vo"), QStringLiteral("gpu-api"), QStringLiteral("gpu-context"),
    QStringLiteral("screen"), QStringLiteral("fs-screen"),
    QStringLiteral("screen-name"), QStringLiteral("fs-screen-name"),
};

QVariant nodeToVariant(const mpv_node *n)
{
    switch (n->format) {
    case MPV_FORMAT_STRING: return QString::fromUtf8(n->u.string);
    case MPV_FORMAT_FLAG: return bool(n->u.flag);
    case MPV_FORMAT_INT64: return qlonglong(n->u.int64);
    case MPV_FORMAT_DOUBLE: return n->u.double_;
    case MPV_FORMAT_NODE_ARRAY: {
        QVariantList l;
        for (int i = 0; i < n->u.list->num; ++i)
            l.append(nodeToVariant(&n->u.list->values[i]));
        return l;
    }
    case MPV_FORMAT_NODE_MAP: {
        QVariantMap m;
        for (int i = 0; i < n->u.list->num; ++i)
            m.insert(QString::fromUtf8(n->u.list->keys[i]), nodeToVariant(&n->u.list->values[i]));
        return m;
    }
    default: return {};
    }
}

QString valueToString(const QVariant &v)
{
    switch (v.typeId()) {
    case QMetaType::Bool: return v.toBool() ? QStringLiteral("yes") : QStringLiteral("no");
    // Stringlisten sind bei mpv Pfadlisten (sub-files, glsl-shaders): Trenner ';' (Windows) bzw. ':'
    case QMetaType::QStringList: return v.toStringList().join(QDir::listSeparator());
    case QMetaType::QVariantList: {
        QStringList parts;
        for (const auto &x : v.toList())
            parts << x.toString();
        return parts.join(QLatin1Char(','));
    }
    case QMetaType::Double: return QString::number(v.toDouble(), 'g', 10);
    default: return v.toString();
    }
}

QString prettyCodec(const QString &c, const QString &profile)
{
    if (!profile.isEmpty() && (c == QLatin1String("dts") || c == QLatin1String("truehd")))
        return profile;
    static const QHash<QString, QString> names = {
        {"truehd", "TrueHD"}, {"eac3", "E-AC-3"}, {"ac3", "AC-3"}, {"dts", "DTS"},
        {"pcm_bluray", "LPCM"}, {"flac", "FLAC"}, {"aac", "AAC"}, {"opus", "Opus"},
        {"hdmv_pgs_subtitle", "PGS"}, {"subrip", "SRT"}, {"ass", "ASS"}, {"dvd_subtitle", "VobSub"},
        {"hevc", "HEVC"}, {"wrapped_avframe", "RAW"}, {"h264", "AVC"}, {"vc1", "VC-1"}, {"mpeg2video", "MPEG-2"}, {"av1", "AV1"},
        {"mpeg1video", "MPEG-1"}, {"jpeg2000", "JPEG 2000"}, {"pcm_s24le", "PCM"}, {"pcm_s16le", "PCM"},
        {"pcm_dvd", "LPCM"}, {"mp2", "MPEG Audio"}, {"mp1", "MPEG Audio"},
    };
    return names.value(c, c.toUpper());
}

QString channelLabel(int n)
{
    switch (n) {
    case 0: return {};
    case 1: return LTR("Mono");
    case 2: return QStringLiteral("2.0");
    case 6: return QStringLiteral("5.1");
    case 8: return QStringLiteral("7.1");
    case 10: return QStringLiteral("5.1.4");
    case 12: return QStringLiteral("7.1.4");
    default: return QStringLiteral("%1 ch").arg(n);
    }
}

double toTime(const QVariant &v)
{
    bool ok = false;
    const double d = v.toDouble(&ok);
    return (ok && v.typeId() != QMetaType::QString) ? d : -1.0;
}

void wakeup(void *ctx)
{
    QMetaObject::invokeMethod(static_cast<MpvController *>(ctx), "onMpvEvents", Qt::QueuedConnection);
}

} // namespace

MpvController::MpvController(DisplayManager *displays, BlurayNav *nav, QObject *parent)
    : QObject(parent)
    , m_displays(displays)
    , m_nav(nav)
{
    // yt-dlp neben Lumen oder im PATH: Webseiten-Links (YouTube, Vimeo, Mediatheken …)
    m_ytdl = QStandardPaths::findExecutable(QStringLiteral("yt-dlp"), {QCoreApplication::applicationDirPath()});
    if (m_ytdl.isEmpty())
        m_ytdl = QStandardPaths::findExecutable(QStringLiteral("yt-dlp"));
    m_stereoAuto = QSettings().value(QStringLiteral("video/stereoAuto"), true).toBool();
    m_clock.start();
    m_tuneTimer.setInterval(1000);
    connect(&m_tuneTimer, &QTimer::timeout, this, &MpvController::tuneTick);
    // Untertitel je Auge hängen an Format, Profil, Spurwahl und Fenstergröße
    for (auto signal : {&MpvController::stereoInputChanged, &MpvController::profileApplied, &MpvController::tracksChanged,
                        &MpvController::osdDimensionsChanged, &MpvController::idleChanged})
        connect(this, signal, this, &MpvController::updateStereoSubs);
    if (m_nav)
        connect(m_nav, &BlurayNav::subtitleDepthChanged, this, &MpvController::updateStereoSubs);
    const QString snap = qEnvironmentVariable("LUMEN_PLAYER_SNAPSHOT");
    if (snap.contains(QLatin1Char('@'))) {
        m_snapshotFile = snap.section(QLatin1Char('@'), 0, -2);
        const QString when = snap.section(QLatin1Char('@'), -1);
        if (when.startsWith(QLatin1Char('+'))) {
            // <datei>@+<sekunden>[,+<sekunden>…]: so lange nach dem Start, ohne anzuhalten (Disc-Menüs:
            // dort zählt jede Playlist ihre Zeit von vorn). Weitere Zeiten: <datei>-2.png, -3 …
            m_snapshotAt = -1;
            const QStringList times = when.split(QLatin1Char(','), Qt::SkipEmptyParts);
            for (int i = 0; i < times.size(); ++i) {
                QString file = m_snapshotFile;
                if (i > 0) {
                    const int dot = file.lastIndexOf(QLatin1Char('.'));
                    file.insert(dot < 0 ? file.size() : dot, QStringLiteral("-%1").arg(i + 1));
                }
                QTimer::singleShot(int(times.at(i).mid(1).toDouble() * 1000), this, [this, file] {
                    if (m_window)
                        m_window->grabFramebuffer().save(file);
                });
            }
        } else {
            m_snapshotAt = when.toDouble();
        }
    }
    if (m_nav) {
        connect(m_nav, &BlurayNav::audioPidSelected, this, [this](int pid) { selectTrackByPid(QStringLiteral("audio"), pid); });
        // ein Clip mit anderen Strömen: Spurlisten neu filtern, verwaiste Auswahl umstellen
        connect(m_nav, &BlurayNav::streamsChanged, this, [this] { rebuildTracks(m_rawTracks); });
        connect(m_nav, &BlurayNav::subtitlePidSelected, this, [this](int pid, bool on) {
            if (m_nav->mvcActive()) {
                // 3D: libbluray rendert die Untertitel selbst – nur die Anzeige nachführen
                m_sid = on ? trackIdForPid(QStringLiteral("sub"), pid) : 0;
                emit tracksChanged();
            } else if (on) {
                selectTrackByPid(QStringLiteral("sub"), pid);
            } else {
                setSubtitleId(0);
            }
        });
        connect(m_nav, &BlurayNav::mvcChanged, this, &MpvController::onMvcChanged);
    }
}

void MpvController::onMvcChanged()
{
    if (m_nav->mvcActive()) {
        // FFmpeg-mvc liefert ein Side-by-Side-Vollbild: Basisansicht links
        m_autoStereo = true;
        setStereoInput(m_nav->baseViewRight() ? QStringLiteral("sbsr") : QStringLiteral("sbsl"));
        // Untertitel rendert libbluray (je Auge platziert), nicht mpv
        const int sid = m_sid;
        setOptionRaw(QStringLiteral("sid"), QStringLiteral("no"), false);
        m_sid = sid;
        m_outputStatus = LTR("Blu-ray 3D (MVC) → %1").arg(stereoOutLabel(m_profile.value("stereoOut").toString()));
        emit outputStatusChanged();
    } else if (m_autoStereo) {
        m_autoStereo = false;
        setStereoInput(QStringLiteral("none"));
    }
    emit mvcActiveChanged();
    m_matchedFps = 0; // Frame Packing braucht ggf. einen anderen Anzeigemodus
    onContentFormatKnown();
}

QString MpvController::stereoOutLabel(const QString &out)
{
    static const QHash<QString, const char *> labels = {
        {"none", "2D"}, {"sbs2l", QT_TRANSLATE_NOOP("Lumen", "Side-by-Side Half")}, {"sbsl", QT_TRANSLATE_NOOP("Lumen", "Side-by-Side Full")},
        {"ab2l", QT_TRANSLATE_NOOP("Lumen", "Top-and-Bottom Half")}, {"abl", QT_TRANSLATE_NOOP("Lumen", "Top-and-Bottom Full")},
        {"fp", QT_TRANSLATE_NOOP("Lumen", "HDMI Frame Packing 1080p")}, {"irl", QT_TRANSLATE_NOOP("Lumen", "Zeilenverschachtelt")},
        {"arcd", QT_TRANSLATE_NOOP("Lumen", "Anaglyph")}, {"seq", QT_TRANSLATE_NOOP("Lumen", "Bildfolge (Shutterbrille)")},
    };
    return labels.contains(out) ? LTR(labels.value(out)) : out;
}

bool MpvController::want3D() const
{
    return m_mvcCapable && m_nav && BlurayNav::available() && !m_castEncoder
           && m_profile.value("stereoOut", "none").toString() != QLatin1String("none");
}

bool MpvController::mvcActive() const
{
    return m_nav && m_nav->mvcActive();
}

int MpvController::trackIdForPid(const QString &type, int pid) const
{
    for (const auto &v : m_rawTracks) {
        const QVariantMap t = v.toMap();
        if (t.value("type").toString() == type && t.value("src-id").toInt() == pid)
            return t.value("id").toInt();
    }
    return 0;
}

bool MpvController::embeddedOnly()
{
#ifdef Q_OS_MACOS
    // libmpv kann unter macOS kein eigenes Fenster öffnen
    return true;
#else
    // Entwickler-Hilfe: LUMEN_PLAYER_WINDOW=native|embedded erzwingt die Art des Player-Fensters
    return qgetenv("LUMEN_PLAYER_WINDOW") == "embedded";
#endif
}

bool MpvController::embeddedByDefault()
{
    if (embeddedOnly())
        return true;
#if defined(Q_OS_LINUX) || defined(Q_OS_FREEBSD)
    // Wayland-Sitzung (auch wenn Qt selbst über XWayland läuft: mpv nähme für sein Fenster Wayland).
    // mpvs eigenes Fenster hängt dort davon ab, dass mpv selbst einen Grafikkontext bekommt; gelang
    // das nicht, endete das Programm bis 1.3.2 beim Start (mpv 0.41 räumt den Wayland-Zustand nicht
    // ab, tools/patches/mpv-wayland-egl-uninit.patch). Das Qt-Fenster mit der Render-API läuft
    // überall, wo das Steuerfenster läuft – deshalb ist es dort die Vorgabe; "nativ" im Profil
    // nimmt mpvs Fenster (HDR-Durchleitung).
    return QGuiApplication::platformName().startsWith(QLatin1String("wayland")) || qEnvironmentVariableIsSet("WAYLAND_DISPLAY");
#else
    return false;
#endif
}

bool MpvController::wantsEmbedded(const QVariantMap &profile)
{
    // gilt auch, wenn ein (z. B. unter Windows angelegtes) Profil "nativ" verlangt
    if (embeddedOnly())
        return true;
#ifndef Q_OS_MACOS
    if (qgetenv("LUMEN_PLAYER_WINDOW") == "native")
        return false;
#endif
    const QString mode = profile.value("playerWindow", "auto").toString();
    if (mode == QLatin1String("embedded"))
        return true;
    if (mode == QLatin1String("native"))
        return false;
    return embeddedByDefault();
}

MpvController::~MpvController()
{
    cancelStereoDetection();
    if (m_detectThread)
        m_detectThread->wait(6000);
    shutdown();
}

bool MpvController::initialize(const QVariantMap &profile)
{
    m_profile = profile;
    m_appliedOptions = buildOptions(profile);
    const bool ok = create(m_appliedOptions);
    emit profileApplied();
    return ok;
}

void MpvController::shutdown()
{
    m_quitting = true;
    destroy();
}

QString MpvController::writeInputConf() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    const QString file = dir + QStringLiteral("/input.conf");
    QFile f(file);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        // Ergänzt/überschreibt mpv's Standardbelegung im Player-Fenster
        f.write("q            stop\n"
                "CLOSE_WIN    quit\n"
                "ESC          set fullscreen no\n"
                "MBTN_LEFT_DBL cycle fullscreen\n"
                "MBTN_RIGHT   cycle pause\n"
                "PGUP         add chapter 1\n"
                "PGDWN        add chapter -1\n"
                // Pfeiltasten/Enter/Klick gehen an Lumen: im Disc-Menü Navigation,
                // sonst Spulen bzw. Vollbild
                "UP           script-message lumen-key up\n"
                "DOWN         script-message lumen-key down\n"
                "LEFT         script-message lumen-key left\n"
                "RIGHT        script-message lumen-key right\n"
                "ENTER        script-message lumen-key enter\n"
                "KP_ENTER     script-message lumen-key enter\n"
                "MBTN_LEFT    script-message lumen-click\n"
                "HOME         script-message lumen-key menu\n"
                "MENU         script-message lumen-key popup\n"
                "END          script-message lumen-key popup\n"
                "i            script-binding stats/display-stats-toggle\n"
                "I            script-binding stats/display-stats\n"
                "l            ab-loop\n"
                "s            screenshot video\n");
    }
    return file;
}

bool MpvController::create(const QVariantMap &options)
{
    m_mpv = mpv_create();
    if (!m_mpv) {
        setError(LTR("libmpv konnte nicht initialisiert werden"));
        return false;
    }

    const QString pics = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + QStringLiteral("/Lumen");
    const QString watchLater = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/watch_later");
    QDir().mkpath(watchLater);

    // Blu-rays nutzen ISO 639-2/B ("ger"), andere Container oft 639-2/T ("deu") oder 639-1 ("de")
    const QLocale::Language lang = QLocale().language();
    QStringList langList{QLocale::languageToCode(lang, QLocale::ISO639Part2B),
                         QLocale::languageToCode(lang, QLocale::ISO639Part2T),
                         QLocale::languageToCode(lang, QLocale::ISO639Part1),
                         QStringLiteral("eng"), QStringLiteral("en")};
    langList.removeAll(QString());
    langList.removeDuplicates();
    const QString langs = langList.join(QLatin1Char(','));

    const QVariantMap base = {
        {"terminal", "no"},
        {"config", "no"},
        {"idle", "yes"},
        {"force-window", "yes"},
        {"keep-open", "yes"},
        {"osc", "no"},
        {"input-default-bindings", "yes"},
        {"input-vo-keyboard", "yes"},
        {"input-conf", writeInputConf()},
        {"osd-level", "1"},
        {"osd-bar", "yes"},
        {"osd-font", "Inter"},
        {"osd-font-size", "30"},
        {"osd-border-size", "1.5"},
        {"osd-bar-h", "1.2"},
        {"osd-duration", "1200"},
        {"title", "${?media-title:${media-title} – }Lumen"},
        {"cursor-autohide", "800"},
        {"screenshot-directory", pics},
        {"screenshot-format", "png"},
        {"watch-later-dir", watchLater},
        {"volume-max", "150"},
        {"cache", "yes"},
        {"demuxer-max-bytes", "400MiB"},
        {"demuxer-readahead-secs", "20"},
        {"ytdl", m_ytdl.isEmpty() ? "no" : "yes"},
        {"alang", langs},
        {"slang", langs},
        {"sub-auto", "fuzzy"},
        {"hr-seek-framedrop", "no"},
        // Halbbild-Material (DVD, 1080i) erkennt mpv am Bild selbst
        {"deinterlace", "auto"},
    };
    for (auto it = base.cbegin(); it != base.cend(); ++it)
        setOptionRaw(it.key(), it.value(), true);
    // Lautstärke und Nachtmodus der letzten Sitzung
    {
        QSettings s;
        const double volume = s.value(QStringLiteral("audio/volume"), 100.0).toDouble();
        setOptionRaw(QStringLiteral("volume"), QString::number(qBound(0.0, volume, 150.0), 'f', 1), true);
        m_nightMode = s.value(QStringLiteral("audio/nightMode"), false).toBool();
        if (m_nightMode)
            setOptionRaw(QStringLiteral("af"), QString::fromLatin1(kNightFilter), true);
    }
    if (!m_ytdl.isEmpty())
        setOptionRaw(QStringLiteral("script-opts"), QStringLiteral("ytdl_hook-ytdl_path=") + m_ytdl, true);
    for (const auto &provider : m_optionProviders) {
        const QVariantMap extra = provider();
        for (auto it = extra.cbegin(); it != extra.cend(); ++it)
            setOptionRaw(it.key(), it.value(), true);
    }
    for (auto it = options.cbegin(); it != options.cend(); ++it)
        setOptionRaw(it.key(), it.value(), true);

    // Entwickler-Hilfe: LUMEN_MPV_LOG=warn|info|v|debug gibt mpv-Meldungen aus
    const QByteArray logLevel = qgetenv("LUMEN_MPV_LOG");
    mpv_request_log_messages(m_mpv, logLevel.isEmpty() ? "error" : logLevel.constData());

    if (mpv_initialize(m_mpv) < 0) {
        mpv_terminate_destroy(m_mpv);
        m_mpv = nullptr;
        setError(LTR("mpv-Initialisierung fehlgeschlagen (Renderer/Optionen prüfen)"));
        return false;
    }
    observeAll();
    if (m_stereoSubs) { // ein neuer mpv-Kern zeigt Untertitel wieder selbst
        m_stereoSubs = false;
        emit stereoSubtitlesChanged();
    }
    m_subText.clear();
    // FFmpeg-mvc meldet sich mit "-mvc" in der Versionskennung (z. B. n9.0.2-mvc8)
    // (nicht mpv fragen: die Antwort käme erst, wenn der Kern fertig eingerichtet ist)
    m_mvcCapable = QString::fromLatin1(av_version_info()).contains(QLatin1String("mvc"), Qt::CaseInsensitive);
    if (m_nav) {
        m_nav->attach(m_mpv);
        m_nav->setStereo(want3D(), m_profile.value("stereoOut").toString());
    }
    if (m_dvd)
        m_dvd->attach(m_mpv);
    Optical::attachProtocol(m_mpv);
    for (const auto &attach : m_protocols)
        attach(m_mpv);

    if (m_castEncoder) {
        m_castRenderer = new CastRenderer(m_mpv, m_castEncoder, this);
        if (!m_castRenderer->ready())
            setError(LTR("Übertragung: OpenGL steht nicht zur Verfügung"));
    } else if (options.value("vo").toString() == QLatin1String("libmpv")) {
        m_window = new PlayerWindow(m_mpv);
        connect(m_window, &PlayerWindow::closeRequested, this, [this] {
            if (!m_quitting)
                emit shutdownRequested();
        });
        connect(m_window, &PlayerWindow::renderReady, this, [this] {
            if (!m_pendingUrl.isEmpty()) {
                const QString url = std::exchange(m_pendingUrl, QString());
                loadFile(url, std::exchange(m_pendingOptions, QVariantMap()));
            }
        });
        placeEmbeddedWindow();
    }

    mpv_set_wakeup_callback(m_mpv, wakeup, this);
    onMpvEvents();
    return true;
}

void MpvController::placeEmbeddedWindow()
{
    if (!m_window)
        return;
    // Zielbildschirm aus dem Profil -> QScreen (Windows: Monitor-Ursprung in
    // physischen Pixeln entspricht dem Ursprung der Qt-Screen-Geometrie)
    QScreen *target = QGuiApplication::primaryScreen();
    if (m_displays) {
        const QVariantMap o = m_displays->output(m_profile.value("output").toString());
        const auto screens = QGuiApplication::screens();
        for (QScreen *s : screens) {
            if (s->name() == o.value("id").toString()
                || (o.contains("x") && s->geometry().topLeft() == QPoint(o.value("x").toInt(), o.value("y").toInt()))) {
                target = s;
                break;
            }
        }
    }
    Qt::WindowFlags flags = Qt::Window;
    if (!m_profile.value("border", true).toBool())
        flags |= Qt::FramelessWindowHint;
    if (m_profile.value("ontop").toBool())
        flags |= Qt::WindowStaysOnTopHint;
    m_window->setFlags(flags);
    m_window->place(target, m_profile.value("fullscreen").toBool());
}

void MpvController::releaseBdOpenLock()
{
    if (m_bdOpenLocked) {
        m_bdOpenLocked = false;
        blurayOpenMutex().unlock();
    }
}

void MpvController::destroy()
{
    releaseBdOpenLock();
    if (!m_mpv)
        return;
    emit mpvDestroying();
    mpv_handle *h = m_mpv;
    m_mpv = nullptr;
    mpv_set_wakeup_callback(h, nullptr, nullptr);
    // Render-Kontext muss vor dem mpv-Kern freigegeben werden
    if (m_window)
        m_window->releaseRenderContext();
    if (m_castRenderer)
        m_castRenderer->releaseRenderContext();
    mpv_terminate_destroy(h);
    if (m_nav)
        m_nav->detach();
    if (m_dvd)
        m_dvd->detach();
    delete m_window;
    m_window = nullptr;
    delete m_castRenderer;
    m_castRenderer = nullptr;
}

void MpvController::setCastOutput(CastEncoder *encoder, const QString &pcmPath)
{
    if (encoder == m_castEncoder && pcmPath == m_castPcm)
        return;
    m_castEncoder = encoder;
    m_castPcm = encoder ? pcmPath : QString();
    m_appliedOptions = buildOptions(m_profile);
    if (!m_quitting)
        restart(m_appliedOptions);
    m_hdrState = -1;
    m_matchedFps = 0;
    emit profileApplied();
}

void MpvController::restart(const QVariantMap &options)
{
    const QString path = m_idle ? QString() : m_path;
    const double pos = m_position;
    const bool paused = m_paused;

    const QString lastUrl = m_lastUrl;
    QVariantMap lastOptions = m_lastOptions;
    destroy();
    if (!create(options))
        return;
    if (path.startsWith(QLatin1String("lumenbd://"))) {
        openDiscMenu(m_currentDevice); // Menü-Sitzung startet neu
    } else if (path.startsWith(QLatin1String("lumendvd://"))) {
        openDvd(m_currentDevice, m_dvdMode, m_dvdTitle);
    } else if (!path.isEmpty()) {
        if (path.startsWith(QLatin1String("bd://")) && !m_currentDevice.isEmpty())
            setOptionRaw(QStringLiteral("bluray-device"), m_currentDevice, false);
        // Datei-Optionen der Quelle (EDL, DCP, VCD …) wiederherstellen
        if (lastUrl != path)
            lastOptions.clear();
        lastOptions["start"] = QString::number(pos, 'f', 3);
        lastOptions["pause"] = paused ? "yes" : "no";
        loadFile(path, lastOptions);
    }
}

void MpvController::observeAll()
{
    for (const auto &o : kObserved)
        mpv_observe_property(m_mpv, o.id, o.name, o.format);
}

void MpvController::setOptionRaw(const QString &name, const QVariant &value, bool preInit)
{
    if (!m_mpv)
        return;
    const QByteArray n = name.toUtf8();
    const QByteArray v = valueToString(value).toUtf8();
    const int err = preInit ? mpv_set_option_string(m_mpv, n.constData(), v.constData())
                            : mpv_set_property_string(m_mpv, n.constData(), v.constData());
    if (err < 0)
        qWarning().noquote() << "mpv:" << name << "=" << v << "->" << mpv_error_string(err);
}

void MpvController::setOption(const QString &name, const QVariant &value)
{
    setOptionRaw(name, value, false);
    if (name == QLatin1String("sub-forced-events-only")) {
        m_forcedSubsOnly = value.toBool();
        m_bitmapShown = -2;
        drawStereoSubs();
    }
}

QVariant MpvController::getProperty(const QString &name) const
{
    if (!m_mpv)
        return {};
    mpv_node node;
    if (mpv_get_property(m_mpv, name.toUtf8().constData(), MPV_FORMAT_NODE, &node) < 0)
        return {};
    const QVariant v = nodeToVariant(&node);
    mpv_free_node_contents(&node);
    return v;
}

void MpvController::command(const QStringList &args)
{
    if (!m_mpv || args.isEmpty())
        return;
    std::vector<QByteArray> bufs;
    bufs.reserve(args.size());
    for (const auto &a : args)
        bufs.push_back(a.toUtf8());
    std::vector<const char *> argv;
    for (const auto &b : bufs)
        argv.push_back(b.constData());
    argv.push_back(nullptr);
    mpv_command_async(m_mpv, 0, argv.data());
}

void MpvController::loadFile(const QString &url, const QVariantMap &fileOptions)
{
    if (!m_mpv)
        return;
    if (m_window && !m_window->ready()) {
        // vo=libmpv kann erst nach dem Render-Kontext ein Bild ausgeben
        m_pendingUrl = url;
        m_pendingOptions = fileOptions;
        return;
    }
    m_lastUrl = url;
    m_lastOptions = fileOptions;
    m_eof = false;
    // Was nur für die vorige Datei galt: beide MVC-Ansichten, zurückgenommene Stufen
    cancelStereoDetection();
    m_fileMvc = false;
    m_mvcStream = false;
    m_decParams.clear();
    m_rawTracks.clear();
    m_detectWanted = false;
    m_learnedLoaded = false;
    m_governor.reset();
    m_governor.setLimits(Tuning::maxRenderLevel(m_profile, Tuning::hardware()), Tuning::kMaxDecodeLevel);
    updateTuningStatus();
    syncOptions();
    m_preloadWaiting = false;
    m_havePreResult = false;
    m_earlyAnalysis = false;
    m_detectPassed = false;

    // 3D vor dem Start erkennen, wo es darauf ankommt: das Profil gibt 3D aus oder der Name sagt
    // "3D". Sonst würde das Bild nach einer Sekunde umspringen. Begrenzte Wartezeit; reicht sie
    // nicht, geht es nach dem Laden weiter (startStereoDetection).
    const QFileInfo fi(url);
    if (m_stereoAuto && !m_autoStereo && m_sourceKind == QLatin1String("file") && fi.isFile()
        && ((!m_castEncoder && m_profile.value("stereoOut", "none").toString() != QLatin1String("none"))
            || StereoDetect::fromName(fi.fileName()).is3d)) {
        m_preloadWaiting = true;
        const int generation = m_detectGeneration + 1; // runStereoAnalysis zählt weiter
        runStereoAnalysis(fi.absoluteFilePath(), url, fileOptions);
        QTimer::singleShot(2500, this, [this, generation, url, fileOptions] {
            if (m_preloadWaiting && generation == m_detectGeneration) {
                m_preloadWaiting = false;
                loadFileNow(url, fileOptions);
            }
        });
        return;
    }
    // Sonst läuft die Bildprüfung neben dem Laden her, statt erst danach zu beginnen: Ihr Ergebnis
    // liegt dann vor, wenn das erste Bild steht, und eine 3D-Datei springt auf einem 2D-Profil
    // nicht mehr nach einer Sekunde auf ein Auge um.
    if (m_stereoAuto && !m_autoStereo && m_sourceKind == QLatin1String("file") && fi.isFile()) {
        runStereoAnalysis(fi.absoluteFilePath(), {}, {});
        m_earlyAnalysis = true;
    }
    loadFileNow(url, fileOptions);
}

void MpvController::loadFileNow(const QString &url, const QVariantMap &options)
{
    if (!m_mpv)
        return;
    // bd://: mpv öffnet die Disc selbst mit libbluray, und das liest dabei die Disc-Metadaten und
    // räumt libxml2 global auf – wie bd_open() in der Disc-Übersicht (siehe blurayOpenMutex).
    // Läuft die gerade, kurz warten; die Sperre bleibt, bis mpv die Disc geöffnet hat.
    if (url.startsWith(QLatin1String("bd://")) && !m_bdOpenLocked) {
        if (!blurayOpenMutex().tryLock()) {
            const int generation = m_detectGeneration;
            QTimer::singleShot(40, this, [this, url, options, generation] {
                if (generation == m_detectGeneration && m_lastUrl == url)
                    loadFileNow(url, options);
            });
            return;
        }
        m_bdOpenLocked = true;
        m_bdOpenStarted = false;
        // für den Fall, dass mpv sich nie meldet: die Disc-Übersicht nicht auf Dauer aussperren
        QTimer::singleShot(15000, this, [this] { releaseBdOpenLock(); });
    }
    QVariantMap fileOptions = options;
    // Zwei Ansichten (MVC): mpvs eigener Matroska-Leser reicht die zweite nicht vollständig durch,
    // der von FFmpeg schon
    if (m_fileMvc)
        fileOptions.insert(QStringLiteral("demuxer"), QStringLiteral("lavf"));
    m_mvcDemuxer = m_fileMvc;
    // Angehalten laden und erst loslaufen, wenn das erste Bild steht (MPV_EVENT_PLAYBACK_RESTART):
    // Decoder, Filter und Shader sind dann eingerichtet. Sonst verwirft mpv die ersten Bilder,
    // weil der Ton schon läuft – bei 60 Bildern je Sekunde ein halbes Dutzend, bei JPEG 2000 mehr.
    // Auch für Blu-ray und DVD (Hauptfilm wie Menü – das erste Bild eines Menüs steht genauso).
    // Nicht für Video-CD und Audio-CD: dort führt die Wiedergabesteuerung die Pause.
    m_primedStart = false;
    static const QStringList primed = {QStringLiteral("file"), QStringLiteral("dcp"), QStringLiteral("bluray"), QStringLiteral("dvd"),
                                       QStringLiteral("hddvd")};
    if (primed.contains(m_sourceKind) && !fileOptions.contains(QStringLiteral("pause"))
        && !m_paused && !qEnvironmentVariableIsSet("LUMEN_NO_PRIMED_START")) {
        fileOptions.insert(QStringLiteral("pause"), QStringLiteral("yes"));
        m_primedStart = true;
    }
    // Benannte Argumente: unabhängig von der loadfile-Signatur der mpv-Version
    QByteArray bUrl = url.toUtf8();
    QByteArray bName("loadfile"), bFlags("replace");
    std::vector<QByteArray> okeys, ovals;
    for (auto it = fileOptions.cbegin(); it != fileOptions.cend(); ++it) {
        okeys.push_back(it.key().toUtf8());
        ovals.push_back(valueToString(it.value()).toUtf8());
    }
    std::vector<mpv_node> ovalNodes(okeys.size());
    std::vector<char *> okeyPtrs(okeys.size());
    for (size_t i = 0; i < okeys.size(); ++i) {
        ovalNodes[i].format = MPV_FORMAT_STRING;
        ovalNodes[i].u.string = ovals[i].data();
        okeyPtrs[i] = okeys[i].data();
    }
    mpv_node_list optList{int(okeys.size()), ovalNodes.data(), okeyPtrs.data()};

    char kName[] = "name", kUrl[] = "url", kFlags[] = "flags", kOptions[] = "options";
    char *topKeys[] = {kName, kUrl, kFlags, kOptions};
    mpv_node topVals[4];
    topVals[0].format = MPV_FORMAT_STRING; topVals[0].u.string = bName.data();
    topVals[1].format = MPV_FORMAT_STRING; topVals[1].u.string = bUrl.data();
    topVals[2].format = MPV_FORMAT_STRING; topVals[2].u.string = bFlags.data();
    topVals[3].format = MPV_FORMAT_NODE_MAP; topVals[3].u.list = &optList;
    mpv_node_list topList{4, topVals, topKeys};
    mpv_node top;
    top.format = MPV_FORMAT_NODE_MAP;
    top.u.list = &topList;

    m_lastError.clear();
    emit lastErrorChanged();
    mpv_command_node_async(m_mpv, 0, &top); // kopiert die Argumente
}

// --------------------------------------------------------------------------
// Quellen
// --------------------------------------------------------------------------

void MpvController::openDisc(const QString &device, const QString &target)
{
    if (!m_mpv || device.isEmpty())
        return;
    const QString t = target.isEmpty() ? QStringLiteral("longest") : target;
    // 3D: nur der libbluray-Strom kann die zweite Ansicht (MVC) zumischen
    if (want3D()) {
        if (t.startsWith(QLatin1String("mpls/")))
            openNavStream(device, QStringLiteral("playlist"), t.mid(5).toInt());
        else
            openNavStream(device, QStringLiteral("main"), -1);
        return;
    }
    m_currentDevice = QDir::toNativeSeparators(device);
    m_sourceKind = QStringLiteral("bluray");
    resetAutoStereo();
    setOptionRaw(QStringLiteral("bluray-device"), blurayPath(m_currentDevice), false);
    // Resume ist für bd:// nicht disc-spezifisch (gleicher Pfad für alle Discs) -> aus
    loadFile(QStringLiteral("bd://") + t, {{"resume-playback", "no"}});
}

void MpvController::openPlaylist(const QString &device, int playlist)
{
    openDisc(device, QStringLiteral("mpls/%1").arg(playlist));
}

void MpvController::openDiscMenu(const QString &device)
{
    openNavStream(device, QStringLiteral("menu"), -1);
}

void MpvController::openNavStream(const QString &device, const QString &mode, int playlist)
{
    if (!m_mpv || !m_nav || !BlurayNav::available() || device.isEmpty())
        return;
    m_currentDevice = QDir::toNativeSeparators(device);
    m_sourceKind = QStringLiteral("bluray");
    const bool stereo = want3D();
    if (!stereo)
        resetAutoStereo();
    m_nav->setStereo(stereo, m_profile.value("stereoOut").toString());

    // Linearer Strom: kleiner Puffer, damit Menüeingaben sofort wirken;
    // Spulen übernimmt libbluray (bd_seek_time + drop-buffers).
    QVariantMap opts{
        {"resume-playback", "no"},
        {"cache", "no"},
        {"demuxer-readahead-secs", "0.4"},
        {"demuxer-max-bytes", "48MiB"},
        {"demuxer-lavf-format", "mpegts"},
        {"force-seekable", "no"},
    };
    if (stereo) {
        // FFmpeg-mvc: beide Ansichten zu einem Side-by-Side-Bild dekodieren.
        // Für 2D-Discs wirkungslos (nur eine Ansicht vorhanden).
        opts["vd-lavc-o"] = QStringLiteral("view_ids=-1");
        // MVC dekodiert keine GPU – bei erkannter 3D-Disc gleich in Software
        // (2D-/UHD-Discs behalten die Hardware-Dekodierung)
        if (isDisc3D(m_currentDevice))
            opts["hwdec"] = QStringLiteral("no");
    }
    // Titel statt interner URL anzeigen
    const QString label = QFileInfo(QDir::cleanPath(QDir::fromNativeSeparators(device))).fileName();
    opts["force-media-title"] = label.isEmpty() ? QStringLiteral("Blu-ray") : label;
    loadFile(m_nav->prepare(m_currentDevice, mode, playlist), opts);
}

// 3D-Disc? index.bdmv: AppInfo-Bit "content_exist_flag", alternativ SSIF-Ordner.
// (ISOs/ungemountete Geräte: unbekannt -> false; FFmpeg-mvc fällt dann selbst zurück)
bool MpvController::isDisc3D(const QString &device)
{
    const QString root = QDir::fromNativeSeparators(device);
    if (QFileInfo::exists(root + QStringLiteral("/BDMV/STREAM/SSIF")))
        return true;
    QFile index(root + QStringLiteral("/BDMV/index.bdmv"));
    if (!index.open(QIODevice::ReadOnly))
        return false;
    const QByteArray head = index.read(46);
    return head.size() >= 45 && head.startsWith("INDX") && (uchar(head.at(44)) & 0x20);
}

void MpvController::selectTrackByPid(const QString &type, int pid)
{
    for (const auto &v : m_rawTracks) {
        const QVariantMap t = v.toMap();
        if (t.value("type").toString() == type && t.value("src-id").toInt() == pid) {
            if (type == QLatin1String("audio"))
                setAudioId(t.value("id").toInt());
            else if (type == QLatin1String("video"))
                setOptionRaw(QStringLiteral("vid"), QString::number(t.value("id").toInt()), false);
            else
                setSubtitleId(t.value("id").toInt());
            return;
        }
    }
}

// Disc über libbluray: Die Playlists einer Disc bringen verschiedene Ströme mit. Zeigt die gewählte
// Spur auf einen Strom, den der laufende Clip nicht hat – nach dem Menü beginnt der Film, sein Ton
// hat ein anderes Format und darum eine eigene Spur –, die Spur wählen, die die Disc vorsieht.
// Eine Spur, die es im Clip gibt, bleibt gewählt: die Wahl des Nutzers geht vor.
void MpvController::syncDiscTracks()
{
    if (!m_nav || !m_nav->active())
        return;
    const QSet<int> live = m_nav->livePids();
    if (live.isEmpty())
        return;
    for (const QString &type : {QStringLiteral("video"), QStringLiteral("audio"), QStringLiteral("sub")}) {
        // 3D: Untertitel zeichnet libbluray selbst
        if (type == QLatin1String("sub") && m_nav->mvcActive())
            continue;
        int selected = -1;
        for (const auto &v : std::as_const(m_rawTracks)) {
            const QVariantMap t = v.toMap();
            if (t.value("type").toString() == type && t.value("selected").toBool() && !t.value("external").toBool())
                selected = t.value("src-id").toInt();
        }
        // nichts gewählt (Ton oder Untertitel aus) bleibt so; die Spur des Clips ebenfalls
        if (selected < 0 || live.contains(selected))
            continue;
        const int want = m_nav->discPid(type);
        // die neue Spur erscheint mit der Programmtabelle des Clips: dann ruft rebuildTracks wieder
        if (want > 0 && trackIdForPid(type, want) > 0)
            selectTrackByPid(type, want);
    }
}

void MpvController::handleClientMessage(const QStringList &args)
{
    if (m_vcd && m_vcd->active()) {
        if (args.value(0) != QLatin1String("lumen-key"))
            return;
        const QString k = args.value(1);
        if (!m_vcd->key(k) && k == QLatin1String("enter"))
            toggleFullscreen();
        return;
    }
    if (m_dvd && m_dvd->active()) {
        const bool dvdMenu = m_dvd->menuVisible();
        if (args.value(0) == QLatin1String("lumen-click")) {
            if (dvdMenu)
                m_dvd->mouseClick(m_mouseX, m_mouseY);
            return;
        }
        if (args.value(0) != QLatin1String("lumen-key"))
            return;
        const QString k = args.value(1);
        if ((dvdMenu || k == QLatin1String("menu") || k == QLatin1String("popup")) && m_dvd->key(k))
            return;
        static const QHash<QString, double> dvdSeeks = {{"left", -10}, {"right", 10}, {"up", 60}, {"down", -60}};
        if (dvdSeeks.contains(k))
            m_dvd->seekRelative(dvdSeeks.value(k));
        else if (k == QLatin1String("enter"))
            toggleFullscreen();
        return;
    }
    const bool nav = m_nav && m_nav->active();
    const bool menu = nav && m_nav->menuVisible();
    if (args.value(0) == QLatin1String("lumen-click")) {
        if (menu)
            m_nav->mouseClick(m_mouseX, m_mouseY);
        return;
    }
    if (args.value(0) != QLatin1String("lumen-key"))
        return;

    const QString k = args.value(1);
    if (menu && m_nav->key(k))
        return;
    if (nav && (k == QLatin1String("menu") || k == QLatin1String("popup"))) {
        m_nav->key(k);
        return;
    }
    static const QHash<QString, double> seeks = {{"left", -10}, {"right", 10}, {"up", 60}, {"down", -60}};
    if (seeks.contains(k)) {
        if (nav)
            m_nav->seekRelative(seeks.value(k));
        else
            seek(seeks.value(k), true);
    } else if (k == QLatin1String("enter")) {
        toggleFullscreen();
    }
}

void MpvController::openFile(const QUrl &url)
{
    if (!url.isLocalFile()) {
        m_sourceKind = QStringLiteral("file");
        m_currentDevice.clear();
        resetAutoStereo();
        loadFile(url.toString());
        return;
    }
    openSource(url.toLocalFile());
}

void MpvController::openStream(const QString &url, const QString &title, double start)
{
    m_sourceKind = QStringLiteral("file");
    m_currentDevice.clear();
    resetAutoStereo();
    QVariantMap opts;
    if (!title.isEmpty())
        opts["force-media-title"] = title;
    if (start > 0)
        opts["start"] = QString::number(start, 'f', 3);
    loadFile(url.trimmed(), opts);
}

void MpvController::openLocation(const QString &location)
{
    if (QFileInfo::exists(location)) {
        openSource(QFileInfo(location).absoluteFilePath());
        return;
    }
    // "schema://…" (mpv-Protokolle wie av://lavfi:…, Plugin-Schemata) unverändert
    // an mpv – QUrl::fromUserInput machte daraus sonst eine http-Adresse
    static const QRegularExpression scheme(QStringLiteral("^[A-Za-z][A-Za-z0-9+.-]+://"));
    if (scheme.match(location).hasMatch() && !location.startsWith(QLatin1String("file://"), Qt::CaseInsensitive)) {
        m_sourceKind = QStringLiteral("file");
        m_currentDevice.clear();
        resetAutoStereo();
        loadFile(location);
        return;
    }
    openFile(QUrl::fromUserInput(location));
}

// ISO-Abbild: Verzeichnisnamen im Dateisystem-Kopf suchen (UDF/ISO 9660 speichern
// sie als ASCII bzw. OSTA-CS0 mit 8-Bit-Zeichen)
static QString isoKind(const QString &file)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return QStringLiteral("bluray");
    const QByteArray head = f.read(8 * 1024 * 1024);
    if (head.contains("HVDVD_TS"))
        return QStringLiteral("hddvd");
    if (head.contains("BDMV"))
        return QStringLiteral("bluray");
    if (head.contains("VIDEO_TS"))
        return QStringLiteral("dvd");
    return QStringLiteral("bluray");
}

QString MpvController::detectKind(const QString &path)
{
    const QFileInfo fi(path);
    if (fi.isDir()) {
        if (QFileInfo::exists(path + QStringLiteral("/BDMV/index.bdmv")))
            return QStringLiteral("bluray");
        if (DvdNav::isDvd(path))
            return QStringLiteral("dvd");
        if (Dcp::isDcp(path))
            return QStringLiteral("dcp");
        const QString other = Optical::detect(path);
        if (!other.isEmpty())
            return other;
        return QStringLiteral("file");
    }
#ifdef Q_OS_LINUX
    if (path.startsWith(QLatin1String("/dev/sr")))
        return QStringLiteral("bluray"); // ungemountet: libbluray prüft beim Öffnen
#endif
    const QString name = fi.fileName();
    const QString suffix = fi.suffix().toLower();
    if (suffix == QLatin1String("iso"))
        return isoKind(path);
    if (name.compare(QLatin1String("index.bdmv"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("bluray");
    if (DvdNav::isDvd(path))
        return QStringLiteral("dvd");
    if (name.startsWith(QLatin1String("ASSETMAP"), Qt::CaseInsensitive)
        || (suffix == QLatin1String("xml") && Dcp::isDcp(fi.absolutePath())))
        return QStringLiteral("dcp");
    if (Optical::isImage(path)) {
        const QString k = Optical::detect(path);
        return k.isEmpty() ? QStringLiteral("file") : k;
    }
    return QStringLiteral("file");
}

void MpvController::openSource(const QString &path, const QString &mode, int title)
{
    if (!m_mpv || path.isEmpty())
        return;
    const QString kind = detectKind(path);
    const QFileInfo fi(path);
    // Datei innerhalb einer Disc-Struktur -> Wurzel der Disc
    QString root = path;
    if (fi.isFile() && fi.fileName().compare(QLatin1String("index.bdmv"), Qt::CaseInsensitive) == 0)
        root = QDir::cleanPath(fi.dir().absolutePath() + QStringLiteral("/.."));
    else if (fi.isFile() && kind == QLatin1String("dvd") && fi.suffix().compare(QLatin1String("iso"), Qt::CaseInsensitive) != 0)
        root = fi.dir().dirName().compare(QLatin1String("VIDEO_TS"), Qt::CaseInsensitive) == 0
                   ? QDir::cleanPath(fi.dir().absolutePath() + QStringLiteral("/..")) : fi.dir().absolutePath();

    if (kind == QLatin1String("bluray")) {
        if (mode == QLatin1String("menu"))
            openDiscMenu(root);
        else if (mode == QLatin1String("title") && title >= 0)
            openPlaylist(root, title);
        else
            openDisc(root, QStringLiteral("longest"));
    } else if (kind == QLatin1String("dvd")) {
        if (mode == QLatin1String("title") && title >= 0)
            openDvd(root, QStringLiteral("title"), title + 1);
        else if (mode == QLatin1String("main"))
            openDvd(root, QStringLiteral("main"));
        else
            openDvd(root, QStringLiteral("menu"));
    } else if (kind == QLatin1String("dcp")) {
        emit dcpRequested(fi.isDir() ? path : fi.absolutePath(), title);
    } else if (kind == QLatin1String("file")) {
        m_sourceKind = QStringLiteral("file");
        m_currentDevice.clear();
        resetAutoStereo();
        loadFile(path);
    } else if ((kind == QLatin1String("vcd") || kind == QLatin1String("svcd")) && mode == QLatin1String("menu") && m_vcd
               && VcdNav::hasPbc(path)) {
        // Video-CD 2.0 mit PBC: Menüs der Disc (Auswahl per Zifferntasten)
        m_sourceKind = kind;
        m_currentDevice = path;
        resetAutoStereo();
        m_vcd->start(path);
    } else {
        const Optical::Prepared p = Optical::prepare(path, kind, mode == QLatin1String("title") ? title : -1);
        if (p.url.isEmpty()) {
            setError(p.error);
            return;
        }
        openPrepared(p.url, p.options, kind, path);
        if (!p.error.isEmpty())
            setError(p.error);
    }
}

void MpvController::openDvd(const QString &device, const QString &mode, int title)
{
    if (!m_mpv || device.isEmpty())
        return;
    m_currentDevice = QDir::toNativeSeparators(device);
    m_sourceKind = QStringLiteral("dvd");
    m_dvdMode = mode;
    m_dvdTitle = title;
    resetAutoStereo();
    const QString label = QFileInfo(QDir::cleanPath(QDir::fromNativeSeparators(device))).fileName();
    if (!m_dvd || !DvdNav::available()) {
        // Ohne libdvdnav-Integration: mpv's eigener DVD-Zugriff (ohne Menüs)
        setOptionRaw(QStringLiteral("dvd-device"), m_currentDevice, false);
        loadFile(title > 0 ? QStringLiteral("dvd://%1").arg(title - 1) : QStringLiteral("dvd://"),
                 {{"resume-playback", "no"}, {"force-media-title", label.isEmpty() ? QStringLiteral("DVD") : label}});
        return;
    }
    // Linearer Strom wie bei Blu-ray-Menüs: kleiner Puffer für direkte Menüreaktion
    const QVariantMap opts{
        {"resume-playback", "no"},
        {"cache", "no"},
        {"demuxer-readahead-secs", "0.4"},
        {"demuxer-max-bytes", "32MiB"},
        {"demuxer-lavf-format", "mpeg"},
        {"demuxer-lavf-probesize", "65536"},
        {"demuxer-lavf-analyzeduration", "0.4"},
        {"force-seekable", "no"},
        {"sid", "no"}, // Untertitel/Menügrafik zeichnet Lumen (DVD-Palette)
        {"force-media-title", label.isEmpty() ? QStringLiteral("DVD") : label},
    };
    loadFile(m_dvd->prepare(m_currentDevice, mode, title), opts);
}

void MpvController::openPrepared(const QString &url, const QVariantMap &options, const QString &kind,
                                 const QString &device, const QString &stereoIn)
{
    if (!m_mpv)
        return;
    m_sourceKind = kind;
    m_currentDevice = QDir::toNativeSeparators(device);
    if (stereoIn != QLatin1String("none")) {
        setStereoInput(stereoIn);
        m_autoStereo = true;
    } else {
        resetAutoStereo();
    }
    loadFile(url, options);
}

void MpvController::resetAutoStereo()
{
    if (m_autoStereo && !mvcActive()) {
        m_autoStereo = false;
        setStereoInput(QStringLiteral("none"));
    }
}

bool MpvController::isDisc() const
{
    static const QStringList kinds = {"bluray", "dvd", "hddvd", "vcd", "svcd", "cdda"};
    return kinds.contains(m_sourceKind) || m_path.startsWith(QLatin1String("bd://")) || m_path.startsWith(QLatin1String("lumenbd://"));
}

void MpvController::addOptionProvider(std::function<QVariantMap()> provider)
{
    m_optionProviders.push_back(std::move(provider));
}

void MpvController::addProtocol(std::function<void(mpv_handle *)> attach)
{
    if (m_mpv)
        attach(m_mpv);
    m_protocols.push_back(std::move(attach));
}

void MpvController::setOverlay(int id, const QImage &img, int x, int y, int w, int h)
{
    if (!m_mpv || img.isNull())
        return;
    const QByteArray sid = QByteArray::number(id);
    const QByteArray addr = "&" + QByteArray::number(quintptr(img.constBits()));
    const QByteArray bx = QByteArray::number(x), by = QByteArray::number(y);
    const QByteArray bw = QByteArray::number(img.width()), bh = QByteArray::number(img.height());
    const QByteArray stride = QByteArray::number(img.bytesPerLine());
    const QByteArray dw = QByteArray::number(qMax(1, w)), dh = QByteArray::number(qMax(1, h));
    const char *args[] = {"overlay-add", sid.constData(), bx.constData(), by.constData(), addr.constData(), "0", "bgra",
                          bw.constData(), bh.constData(), stride.constData(), dw.constData(), dh.constData(), nullptr};
    mpv_command(m_mpv, args); // synchron: mpv kopiert die Pixel
}

void MpvController::removeOverlay(int id)
{
    if (!m_mpv)
        return;
    const QByteArray sid = QByteArray::number(id);
    const char *args[] = {"overlay-remove", sid.constData(), nullptr};
    mpv_command_async(m_mpv, 0, args);
}

// Bildschirme geändert: Profil-Ausgabe "Automatisch" neu anwenden
void MpvController::onOutputsChanged()
{
    if (!m_mpv || !m_displays || !m_profile.value("output").toString().isEmpty())
        return;
    if (m_window) {
        placeEmbeddedWindow();
        return;
    }
    const QVariantMap screen = m_displays->mpvScreenOptions(QString());
    for (auto it = screen.cbegin(); it != screen.cend(); ++it)
        setOptionRaw(it.key(), it.value(), false);
}

void MpvController::setVcdNav(VcdNav *vcd)
{
    m_vcd = vcd;
    if (!m_vcd)
        return;
    connect(m_vcd, &VcdNav::playRequested, this, [this](const QString &url, bool) {
        // keep-open pausiert am Dateiende – jedes Element der Steuerung läuft los
        setOptionRaw(QStringLiteral("pause"), false, false);
        loadFile(url);
    });
    connect(m_vcd, &VcdNav::stateChanged, this, &MpvController::updateVcdKeys);
}

// Zifferntasten gehören nur während der VCD-Steuerung der Auswahl (eigene
// mpv-Eingabesektion; sonst bleiben die normalen Belegungen)
void MpvController::updateVcdKeys()
{
    if (!m_mpv)
        return;
    if (m_vcd && m_vcd->active()) {
        QString section;
        for (int i = 0; i <= 9; ++i)
            section += QStringLiteral("%1 script-message lumen-key %1\nKP%1 script-message lumen-key %1\n").arg(i);
        section += QStringLiteral("PGUP script-message lumen-key prev\nPGDWN script-message lumen-key next\n"
                                  "BS script-message lumen-key return\nESC script-message lumen-key return\n");
        command({"define-section", "lumen-vcd", section, "force"});
        command({"enable-section", "lumen-vcd"});
    } else {
        command({"disable-section", "lumen-vcd"});
    }
}

void MpvController::setDvdNav(DvdNav *dvd)
{
    m_dvd = dvd;
    if (!m_dvd)
        return;
    if (m_mpv)
        m_dvd->attach(m_mpv);
    connect(m_dvd, &DvdNav::audioPidSelected, this, [this](int id) { selectTrackByPid(QStringLiteral("audio"), id); });
    connect(m_dvd, &DvdNav::streamsChanged, this, [this] { rebuildTracks(m_rawTracks); });
}

// --------------------------------------------------------------------------
// Vorführprogramm (Show): Werbung, Trailer, Hauptfilm … nacheinander
// --------------------------------------------------------------------------

void MpvController::queueAdd(const QString &path, const QString &label, int title)
{
    m_queue.append(QVariantMap{{"path", path}, {"label", label.isEmpty() ? QFileInfo(path).fileName() : label},
                               {"title", title}, {"kind", detectKind(path)}});
    emit queueChanged();
}

void MpvController::queueRemove(int index)
{
    if (index < 0 || index >= m_queue.size())
        return;
    m_queue.removeAt(index);
    if (m_queueIndex >= index)
        --m_queueIndex;
    emit queueChanged();
}

void MpvController::queueMove(int index, int delta)
{
    const int to = index + delta;
    if (index < 0 || index >= m_queue.size() || to < 0 || to >= m_queue.size())
        return;
    m_queue.move(index, to);
    if (m_queueIndex == index)
        m_queueIndex = to;
    else if (m_queueIndex == to)
        m_queueIndex = index;
    emit queueChanged();
}

void MpvController::queueClear()
{
    m_queue.clear();
    m_queueIndex = -1;
    m_queueActive = false;
    emit queueChanged();
}

void MpvController::queueStart(int index)
{
    if (index < 0 || index >= m_queue.size())
        return;
    m_queueIndex = index;
    const QVariantMap item = m_queue[index].toMap();
    const int title = item.value("title").toInt();
    openSource(item.value("path").toString(), title >= 0 ? QStringLiteral("title") : QStringLiteral("main"), title);
    m_queueActive = true;
    emit queueChanged();
}

void MpvController::queueStop()
{
    m_queueActive = false;
    emit queueChanged();
}

void MpvController::advanceQueue()
{
    if (!m_queueActive)
        return;
    if (m_queueIndex + 1 >= m_queue.size()) {
        m_queueActive = false;
        emit queueChanged();
        showText(LTR("Programm beendet"), 3000);
        return;
    }
    queueStart(m_queueIndex + 1);
}

// --------------------------------------------------------------------------
// Transport
// --------------------------------------------------------------------------

void MpvController::togglePause() { command({"cycle", "pause"}); }
void MpvController::setPaused(bool p) { setOptionRaw(QStringLiteral("pause"), p, false); }

void MpvController::stop()
{
    if (!m_idle && m_sourceKind == QLatin1String("file"))
        command({"write-watch-later-config"});
    if (m_queueActive) {
        m_queueActive = false;
        emit queueChanged();
    }
    cancelStereoDetection();
    m_preloadWaiting = false;
    command({"stop"});
}

void MpvController::seek(double seconds, bool relative)
{
    holdGovernor(3);
    command({"osd-msg-bar", "seek", QString::number(seconds, 'f', 3), relative ? "relative" : "absolute"});
}

void MpvController::seekExact(double seconds)
{
    holdGovernor(3);
    command({"osd-msg-bar", "seek", QString::number(seconds, 'f', 3), "absolute+exact"});
}

void MpvController::frameStep() { command({"frame-step"}); }
void MpvController::frameBackStep() { command({"frame-back-step"}); }
void MpvController::nextChapter() { command({"osd-msg", "add", "chapter", "1"}); }
void MpvController::prevChapter() { command({"osd-msg", "add", "chapter", "-1"}); }
void MpvController::setChapter(int index) { setOptionRaw(QStringLiteral("chapter"), index, false); }
void MpvController::setTitle(int index) { setOptionRaw(QStringLiteral("disc-title"), index, false); }
void MpvController::cycleAbLoop() { command({"osd-msg", "ab-loop"}); }

void MpvController::clearAbLoop()
{
    setOptionRaw(QStringLiteral("ab-loop-a"), QStringLiteral("no"), false);
    setOptionRaw(QStringLiteral("ab-loop-b"), QStringLiteral("no"), false);
}

void MpvController::setVolume(double v)
{
    setOptionRaw(QStringLiteral("volume"), v, false);
    QSettings().setValue(QStringLiteral("audio/volume"), v);
}

void MpvController::setNightMode(bool on)
{
    if (on == m_nightMode)
        return;
    m_nightMode = on;
    QSettings().setValue(QStringLiteral("audio/nightMode"), on);
    command({QStringLiteral("af"), on ? QStringLiteral("add") : QStringLiteral("remove"),
             on ? QString::fromLatin1(kNightFilter) : QStringLiteral("@lumen-night")});
    emit nightModeChanged();
}

void MpvController::addSubtitleFile(const QUrl &file)
{
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    if (!m_idle && !path.isEmpty())
        command({QStringLiteral("sub-add"), path, QStringLiteral("select")});
}
void MpvController::setMuted(bool m) { setOptionRaw(QStringLiteral("mute"), m, false); }
void MpvController::setSpeed(double s) { setOptionRaw(QStringLiteral("speed"), s, false); }

void MpvController::setAudioId(int id)
{
    if (m_dvd && m_dvd->active()) {
        for (const auto &v : m_rawTracks) {
            const QVariantMap t = v.toMap();
            if (t.value("type").toString() == QLatin1String("audio") && t.value("id").toInt() == id)
                m_dvd->noteAudioSelected(t.value("src-id").toInt());
        }
    }
    setOptionRaw(QStringLiteral("aid"), id > 0 ? QString::number(id) : QStringLiteral("no"), false);
}

void MpvController::setSubtitleId(int id)
{
    if (mvcActive()) {
        // 3D: libbluray rendert PG-Untertitel als Grafik je Auge (mit Tiefe)
        int pid = 0;
        for (const auto &v : m_rawTracks) {
            const QVariantMap t = v.toMap();
            if (t.value("type").toString() == QLatin1String("sub") && t.value("id").toInt() == id)
                pid = t.value("src-id").toInt();
        }
        m_nav->selectSubtitlePid(pid);
        m_sid = pid ? id : 0;
        emit tracksChanged();
        return;
    }
    setOptionRaw(QStringLiteral("sid"), id > 0 ? QString::number(id) : QStringLiteral("no"), false);
}

void MpvController::setAudioDelay(double s) { setOptionRaw(QStringLiteral("audio-delay"), s, false); }
void MpvController::setSubDelay(double s) { setOptionRaw(QStringLiteral("sub-delay"), s, false); }
void MpvController::toggleFullscreen() { command({"cycle", "fullscreen"}); }
void MpvController::screenshot() { command({"osd-msg", "screenshot", "video"}); }
void MpvController::toggleStats() { command({"script-binding", "stats/display-stats-toggle"}); }

void MpvController::showText(const QString &text, int ms)
{
    command({"show-text", text, QString::number(ms)});
}

// --------------------------------------------------------------------------
// Profile / Ausgabe
// --------------------------------------------------------------------------

QVariantMap MpvController::buildOptions(const QVariantMap &profile) const
{
    // 3D: Quellformat (Laufzeit) -> Geräteformat (Profil). Ohne 3D-Gerät wird
    // eine 3D-Quelle auf das linke Auge (2D) reduziert.
    // Übertragung: immer 2D
    const QString out = m_castEncoder ? QStringLiteral("none") : profile.value("stereoOut", "none").toString();
    QVariantMap stereoProfile = profile;
    stereoProfile["stereoOut"] = out;
    const QString stereoFilter = Stereo3D::filter(m_stereoIn, stereoProfile);

    // Skalierungsstufe und Decoder-Weg nach Maschine und Lage (siehe Tuning.h)
    Tuning::Context context = tuningContext();
    context.cpuFilter = !stereoFilter.isEmpty();
    context.sequential = out == QLatin1String("seq") && m_stereoIn != QLatin1String("none");
    const QVariantMap resolved = Tuning::resolve(profile, Tuning::hardware(), context);
    QVariantMap o = ProfileManager::toMpvOptions(resolved);
    const QVariantMap relief = Tuning::reliefOptions(resolved, context);
    for (auto it = relief.cbegin(); it != relief.cend(); ++it)
        o.insert(it.key(), it.value());
    // Datei mit zwei Ansichten (H.264/MVC): FFmpeg-mvc liefert beide nebeneinander
    o["vd-lavc-o"] = m_fileMvc ? QStringLiteral("view_ids=-1") : QString();
    if (m_castEncoder) {
        // Geräte-Einstellungen des Profils ruhen, solange übertragen wird
        for (const char *key : {"gpu-api", "border", "ontop", "fullscreen", "audio-device", "icc-profile", "target-lut", "glsl-shaders"})
            o.remove(QLatin1String(key));
        const QVariantMap cast = castMpvOptions(m_castPcm);
        for (auto it = cast.cbegin(); it != cast.cend(); ++it)
            o.insert(it.key(), it.value());
    } else if (wantsEmbedded(profile)) {
        // Eigenes Qt-Fenster: Platzierung/Rahmen übernimmt Qt, Bild über die Render-API
        o["vo"] = QStringLiteral("libmpv");
        o["force-window"] = QStringLiteral("no"); // das Qt-Fenster ist immer sichtbar
        o.remove("gpu-api");
        o.remove("border");
        o.remove("ontop");
    } else if (m_displays) {
        const QVariantMap screen = m_displays->mpvScreenOptions(profile.value("output").toString());
        for (auto it = screen.cbegin(); it != screen.cend(); ++it)
            o.insert(it.key(), it.value());
    }

    // Eigenes mpv-Fenster: fällt der neue Renderer aus (alter Treiber), nimmt mpv den bewährten
    if (!m_castEncoder && !wantsEmbedded(profile) && o.value("vo").toString() == QLatin1String("gpu-next"))
        o["vo"] = QStringLiteral("gpu-next,gpu");

    o["vf"] = stereoFilter;
    // Im eingebetteten Fenster kennt mpv die Bildwiederholrate nicht (0 = unbekannt)
    o["display-fps-override"] = QStringLiteral("0");
    if (out == QLatin1String("seq") && m_stereoIn != QLatin1String("none")) {
        // Bildfolge: jedes Bild genau einen Bildwechsel lang, also streng im Takt des Bildschirms
        o["video-sync"] = QStringLiteral("display-resample");
        o["interpolation"] = QStringLiteral("no");
        // … und der Takt ist die Rate des Profils: Ohne diese Angabe richtet sich mpv im
        // eingebetteten Fenster nach dem Ton und lässt Bilder aus, sobald eines zu spät kommt
        if (wantsEmbedded(profile))
            o["display-fps-override"] = QString::number(qBound(48, profile.value("seqRate", 120).toInt(), 480));
    }
    if (m_nav) {
        m_nav->setStereo(m_mvcCapable && BlurayNav::available() && out != QLatin1String("none"), out);
        m_nav->setSubtitleDepth(profile.value("subtitleDepth").toInt());
    }
    return o;
}

void MpvController::setStereoInput(const QString &format)
{
    if (format == m_stereoIn)
        return;
    m_stereoIn = format;
    syncOptions(); // Filterkette, Abgleich und Decoder-Weg (kopierend, wenn ein Filter rechnet)
    emit stereoInputChanged();
    m_matchedFps = 0; // Bildfolge braucht ggf. eine andere Bildwiederholrate
    onContentFormatKnown();
}

void MpvController::applyProfile(const QVariantMap &profile)
{
    // Vorherige Geräteänderungen zurücknehmen, falls das Ziel wechselt
    if (m_displays && profile.value("output") != m_profile.value("output"))
        m_displays->restoreAll();

    // Zurückgenommene Stufen gelten für das alte Profil: erst in dessen Normalzustand zurück,
    // damit der Vergleich unten nur die Unterschiede der Profile sieht
    m_governor.reset();
    updateTuningStatus();
    syncOptions();

    m_profile = profile;
    const QVariantMap opts = buildOptions(profile);

    bool needRestart = false;
    for (auto it = m_appliedOptions.cbegin(); it != m_appliedOptions.cend(); ++it) {
        if (!opts.contains(it.key()) || (kRestartKeys.contains(it.key()) && opts.value(it.key()) != it.value())) {
            needRestart = true;
            break;
        }
    }
    for (const auto &k : kRestartKeys)
        needRestart |= opts.contains(k) && !m_appliedOptions.contains(k);

    if (!m_mpv) {
        m_appliedOptions = opts;
    } else if (needRestart) {
        m_appliedOptions = opts;
        restart(opts);
    } else {
        for (auto it = opts.cbegin(); it != opts.cend(); ++it) {
            if (m_appliedOptions.value(it.key()) != it.value())
                setOptionRaw(it.key(), it.value(), false);
        }
        m_appliedOptions = opts;
        placeEmbeddedWindow();
    }

    m_hdrState = -1;
    m_matchedFps = 0;
    m_outputStatus = LTR("Profil „%1“ aktiv").arg(profile.value("name").toString());
    emit outputStatusChanged();
    emit profileApplied();
    onContentFormatKnown();
    m_governor.setLimits(Tuning::maxRenderLevel(m_profile, Tuning::hardware()), Tuning::kMaxDecodeLevel);
    // MVC-Datei: die zweite Ansicht wird nur für ein 3D-Profil dekodiert. (Alle anderen Formate
    // hängen nicht vom Profil ab – sie neu zu prüfen, ließe das Bild kurz umspringen.)
    if (m_stereoAuto && !m_idle && m_mvcStream)
        startStereoDetection();
}

void MpvController::onContentFormatKnown()
{
    if (!m_displays || m_idle)
        return;
    const QString out = m_profile.value("output").toString();
    QStringList status;

    // Bildfolge (Shutterbrille): der Bildschirm muss genau mit der gewählten Rate laufen
    const bool sequential = m_stereoIn != QLatin1String("none") && !m_castEncoder
                            && m_profile.value("stereoOut").toString() == QLatin1String("seq");
    if (sequential && m_profile.value("seqSwitchMode").toBool()) {
        const double rate = qBound(48, m_profile.value("seqRate", 120).toInt(), 480);
        if (std::abs(rate - m_matchedFps) > 0.01) {
            QString info;
            const QStringList wh = m_profile.value("seqResolution").toString().split(QLatin1Char('x'));
            const QSize size = wh.size() == 2 ? QSize(wh[0].toInt(), wh[1].toInt()) : QSize();
            if (m_displays->matchRefreshRate(out, rate, &info, size.isValid() && !size.isEmpty() ? size : QSize())) {
                m_matchedFps = rate;
                status << LTR("3D-Bildfolge %1").arg(info);
            } else if (!info.isEmpty()) {
                status << info;
            }
        }
    }

    // HDMI Frame Packing braucht den Modus 1920x2205 (bei 3D immer, sonst optional Bildrate)
    const bool framePacking = mvcActive() && m_profile.value("stereoOut").toString() == QLatin1String("fp");
    if (!sequential && (m_profile.value("matchRefreshRate").toBool() || framePacking) && m_containerFps > 1
        && std::abs(m_containerFps - m_matchedFps) > 0.01) {
        QString info;
        if (m_displays->matchRefreshRate(out, m_containerFps, &info, framePacking ? QSize(1920, 2205) : QSize())) {
            m_matchedFps = m_containerFps;
            status << LTR("Bildrate %1").arg(info);
        } else if (!info.isEmpty()) {
            status << info;
        }
    }

    if (m_profile.value("osHdrSwitch").toBool() && !m_videoParams.isEmpty()) {
        const QString gamma = m_videoParams.value("gamma").toString();
        const bool hdr = gamma == QLatin1String("pq") || gamma == QLatin1String("hlg");
        if (int(hdr) != m_hdrState) {
            m_hdrState = hdr;
            if (m_displays->setHdr(out, hdr))
                status << (hdr ? LTR("System-HDR an") : LTR("System-HDR aus"));
        }
    }

    if (!status.isEmpty()) {
        m_outputStatus = status.join(QStringLiteral(" · "));
        emit outputStatusChanged();
    }
}

// --------------------------------------------------------------------------
// Automatik: Optionen nachführen, 3D erkennen, Leistung anpassen
// --------------------------------------------------------------------------

void MpvController::syncOptions()
{
    if (!m_mpv)
        return;
    const QVariantMap opts = buildOptions(m_profile);
    bool changed = false;
    for (auto it = opts.cbegin(); it != opts.cend(); ++it) {
        if (kRestartKeys.contains(it.key()) || m_appliedOptions.value(it.key()) == it.value())
            continue;
        m_appliedOptions[it.key()] = it.value();
        setOptionRaw(it.key(), it.value(), false);
        changed = true;
    }
    // Was eine Stufe zusätzlich gesetzt hatte und jetzt nicht mehr vorkommt: zurück auf mpvs Vorgabe
    for (const QString &key : m_appliedOptions.keys()) {
        if (opts.contains(key) || kRestartKeys.contains(key))
            continue;
        const QVariant def = getProperty(QStringLiteral("option-info/%1/default-value").arg(key));
        if (def.isValid())
            setOptionRaw(key, def, false);
        m_appliedOptions.remove(key);
        changed = true;
    }
    if (changed)
        holdGovernor(3); // der Umbau selbst kostet Bilder
}

Tuning::Context MpvController::tuningContext() const
{
    Tuning::Context c;
    // auch für eine Ansicht: Hardware-Decoder scheitern an MVC-Strömen. Bei der Disc mischt Lumen
    // die zweite Ansicht selbst zu (MvcMerger) – das Profil im Strom nennt sie nicht
    c.mvc = m_mvcStream || mvcActive();
    if (m_profile.value("adaptive", true).toBool()) {
        c.renderLevel = m_governor.renderLevel();
        c.decodeLevel = m_governor.decodeLevel();
    }
    return c;
}

QVariantMap MpvController::hardwareInfo() const
{
    const Tuning::Hardware &hw = Tuning::hardware();
    return {{"renderer", hw.renderer}, {"vendor", hw.vendor}, {"class", Tuning::gpuClassName(hw.gpu)}, {"cores", hw.cores}};
}

QString MpvController::stereoInLabel(const QString &format)
{
    if (format.isEmpty() || format == QLatin1String("none"))
        return QStringLiteral("2D");
    const bool rightFirst = format.endsWith(QLatin1Char('r'));
    const QString layout = format.left(format.size() - 1);
    QString label = format;
    if (layout == QLatin1String("sbs2"))
        label = LTR("Side-by-Side Half");
    else if (layout == QLatin1String("sbs"))
        label = LTR("Side-by-Side Full");
    else if (layout == QLatin1String("ab2"))
        label = LTR("Top-and-Bottom Half");
    else if (layout == QLatin1String("ab"))
        label = LTR("Top-and-Bottom Full");
    else if (layout == QLatin1String("ir"))
        label = LTR("Zeilenverschachtelt");
    else if (layout == QLatin1String("a"))
        label = LTR("Bildwechsel");
    return rightFirst ? LTR("%1, rechtes Auge zuerst").arg(label) : label;
}

QString MpvController::stereoStatus() const
{
    if (!m_stereoAuto || m_idle || mvcActive())
        return {};
    if (m_stereoSource == QLatin1String("mvc"))
        return m_fileMvc ? LTR("Erkannt: zwei Ansichten (MVC)") : LTR("Erkannt: zwei Ansichten (MVC) – gezeigt wird eine");
    if (m_stereoSource.isEmpty() || m_stereoIn == QLatin1String("none"))
        return m_detectThread ? LTR("Bild wird geprüft …") : LTR("Kein 3D erkannt");
    const QString by = m_stereoSource == QLatin1String("metadata") ? LTR("Angabe in der Datei")
                       : m_stereoSource == QLatin1String("name")   ? LTR("Dateiname")
                       : m_stereoSource == QLatin1String("picture") ? LTR("Bildvergleich")
                                                                    : LTR("Bildgröße");
    return LTR("Erkannt: %1 (%2)").arg(stereoInLabel(m_stereoIn), by);
}

void MpvController::setStereoAuto(bool on)
{
    if (on == m_stereoAuto)
        return;
    m_stereoAuto = on;
    QSettings().setValue(QStringLiteral("video/stereoAuto"), on);
    if (on) {
        startStereoDetection();
    } else {
        cancelStereoDetection();
        m_stereoSource.clear();
        if (m_fileMvc) {
            m_fileMvc = false;
            syncOptions();
        }
    }
    emit stereoInputChanged();
}

void MpvController::cancelStereoDetection()
{
    ++m_detectGeneration;
    m_detectPending = false;
    if (m_detectCancel)
        m_detectCancel->store(true);
    m_detectCancel.reset();
}

// Nach "Datei geladen": warten, bis die Spurliste der neuen Datei eine gewählte Videospur nennt.
// (Die Liste wird nicht abgefragt, sondern beobachtet: eine Abfrage aus dem GUI-Thread hält im
// eingebetteten Fenster den Renderer auf und kostet beim Start Bilder.)
void MpvController::tryStereoDetection()
{
    for (const auto &v : std::as_const(m_rawTracks)) {
        const QVariantMap t = v.toMap();
        if (t.value("type").toString() != QLatin1String("video") || !t.value("selected").toBool())
            continue;
        const QString profile = t.value("codec-profile").toString();
        const bool mvc = profile == QLatin1String("Stereo High") || profile == QLatin1String("Multiview High");
        // Das Codec-Profil kann erst mit einer späteren Fassung der Liste kommen (wenn der
        // Decoder geöffnet ist): dann noch einmal, falls sich damit etwas ändert
        if (m_detectWanted || (mvc && !m_mvcStream && m_stereoAuto && !m_idle)) {
            m_detectWanted = false;
            startStereoDetection();
        }
        return;
    }
}

void MpvController::startStereoDetection()
{
    // (die neben dem Laden gestartete Bildprüfung dieser Datei läuft weiter)
    if (!(m_earlyAnalysis && m_detectThread))
        cancelStereoDetection();
    if (!m_stereoAuto || !m_mpv)
        return;
    // Blu-ray 3D meldet sich selbst; ein Öffner (Plugin, DCP) hat das Format vorgegeben
    if (mvcActive() || m_autoStereo)
        return;
    const bool wasMvc = m_fileMvc;
    m_fileMvc = false;
    // die Datei, die Lumen geladen hat (die Änderungsmeldung für "path" kann später eintreffen)
    m_detectPath = m_lastUrl;
    if (m_sourceKind != QLatin1String("file") || m_detectPath.isEmpty()) {
        if (wasMvc)
            syncOptions();
        applyDetectedStereo(QStringLiteral("none"), {});
        return;
    }

    // 1. Zwei Ansichten in einem Strom (H.264/MVC, z. B. MKV von einer Blu-ray 3D): FFmpeg-mvc
    //    nennt das Profil; die zweite Ansicht wird nur dekodiert, wenn das Profil 3D ausgibt
    for (const auto &v : std::as_const(m_rawTracks)) {
        const QVariantMap t = v.toMap();
        if (t.value("type").toString() != QLatin1String("video") || !t.value("selected").toBool())
            continue;
        const QString profile = t.value("codec-profile").toString();
        // m_mvcStream: schon vor dem Laden an der Datei selbst erkannt
        if (profile != QLatin1String("Stereo High") && profile != QLatin1String("Multiview High") && !m_mvcStream)
            break;
        m_fileMvc = m_mvcCapable && !m_castEncoder && m_profile.value("stereoOut", "none").toString() != QLatin1String("none");
        const bool wasStream = m_mvcStream;
        m_mvcStream = true;
        if (m_fileMvc != wasMvc || !wasStream)
            syncOptions();
        if (m_fileMvc && !m_mvcDemuxer) {
            // erst jetzt bemerkt (z. B. Profil auf 3D umgestellt): an derselben Stelle neu laden,
            // diesmal mit dem Matroska-Leser von FFmpeg
            m_stereoSource = QStringLiteral("mvc");
            setStereoInput(QStringLiteral("sbsl"));
            QVariantMap options = m_lastOptions;
            options["start"] = QString::number(m_position, 'f', 3);
            // (läuft der vorbereitete Start noch, ist die Pause seine – dann gilt sie nicht)
            if (!m_primedStart)
                options["pause"] = m_paused ? "yes" : "no";
            m_paused = m_paused && !m_primedStart;
            loadFileNow(m_lastUrl, options);
            return;
        }
        // FFmpeg-mvc setzt die Basisansicht nach links
        applyDetectedStereo(m_fileMvc ? QStringLiteral("sbsl") : QStringLiteral("none"), QStringLiteral("mvc"));
        return;
    }
    if (wasMvc)
        syncOptions();
    m_detectPending = true;
    continueStereoDetection();
}

void MpvController::continueStereoDetection()
{
    if (!m_detectPending)
        return;
    // Was der Decoder liefert – vor der 3D-Filterkette
    const QVariantMap dec = m_decParams;
    // Größe, wie sie gezeigt wird: danach richtet sich, ob eine Bildhälfte gestaucht ist
    const int w = dec.value("dw", dec.value("w")).toInt(), h = dec.value("dh", dec.value("h")).toInt();
    if (w <= 0 || h <= 0)
        return; // noch kein Bild: weiter, sobald video-params kommt
    m_detectPending = false;
    if (m_mvcStream) {
        // vor dem Laden als MVC erkannt, läuft mit einer Ansicht (Profil gibt 2D aus)
        applyDetectedStereo(QStringLiteral("none"), QStringLiteral("mvc"));
        return;
    }

    // 2. Angabe im Container oder Strom
    const StereoDetect::Hint meta = StereoDetect::fromMetadata(dec.value("stereo-in").toString());
    if (meta.decided()) {
        applyDetectedStereo(StereoDetect::format(meta, w, h), QStringLiteral("metadata"));
        return;
    }

    // schon vor dem Laden geprüft (Name und Bild)
    if (m_havePreResult) {
        m_havePreResult = false;
        applyDetectedStereo(m_preFormat, m_preSource);
        return;
    }

    // 3. Dateiname – sofort, wenn er eindeutig ist; das Bild kann ihn danach noch berichtigen
    const bool local = QFileInfo(m_detectPath).isFile();
    const QString fileName = local ? QFileInfo(m_detectPath).fileName() : QUrl(m_detectPath).fileName();
    const StereoDetect::Hint name = StereoDetect::fromName(fileName);
    const bool nameLayout = (name.layout == StereoDetect::SideBySide || name.layout == StereoDetect::TopBottom)
                            && (name.tagged3d || name.half >= 0);
    if (nameLayout) {
        applyDetectedStereo(StereoDetect::format(name, w, h), QStringLiteral("name"));
    } else if (!local && name.tagged3d && StereoDetect::fromSize(w, h).decided()) {
        applyDetectedStereo(StereoDetect::format(StereoDetect::fromSize(w, h), w, h), QStringLiteral("size"));
    } else {
        applyDetectedStereo(QStringLiteral("none"), {});
    }
    if (!local)
        return; // einen Netzwerkstrom ein zweites Mal zu öffnen, lohnt nicht

    // 4. Das Bild selbst – die Prüfung läuft schon seit dem Laden, oder sie beginnt jetzt
    if (m_earlyAnalysis && m_detectThread) {
        m_detectPassed = true;
        return;
    }
    runStereoAnalysis(m_detectPath, {}, {});
}

// Datei in einem eigenen Thread öffnen und Bilder vergleichen (StereoDetect::detectFile).
// url nicht leer: die Datei wartet auf das Ergebnis und wird danach geladen.
void MpvController::runStereoAnalysis(const QString &path, const QString &url, const QVariantMap &fileOptions)
{
    cancelStereoDetection();
    auto cancel = std::make_shared<std::atomic_bool>(false);
    m_detectCancel = cancel;
    const int generation = m_detectGeneration;
    QPointer<MpvController> self(this);
    QThread *thread = QThread::create([self, cancel, generation, path, url, fileOptions] {
        const StereoDetect::Result r = StereoDetect::detectFile(path, cancel.get());
        if (cancel->load() || !self)
            return;
        QMetaObject::invokeMethod(self.data(), [self, r, generation, url, fileOptions] {
            if (!self || generation != self->m_detectGeneration || !self->m_stereoAuto)
                return;
            self->m_detectThread = nullptr;
            if (self->m_preloadWaiting && !url.isEmpty()) {
                // noch nicht geladen: Format setzen, dann starten
                self->m_preloadWaiting = false;
                self->m_havePreResult = true;
                self->m_preFormat = r.format;
                self->m_preSource = r.source;
                self->m_stereoSource = r.found() || r.mvc ? r.source : QString();
                if (qEnvironmentVariableIsSet("LUMEN_STEREO_DEBUG"))
                    qWarning().noquote() << "Lumen: 3D-Erkennung vor dem Laden" << r.format << r.source << "MVC" << r.mvc;
                if (r.mvc) {
                    self->m_mvcStream = true;
                    // zwei Ansichten: den Decoder gleich mit beiden öffnen, wenn das Profil 3D ausgibt
                    self->m_fileMvc = self->m_mvcCapable && !self->m_castEncoder
                                      && self->m_profile.value("stereoOut", "none").toString() != QLatin1String("none");
                    self->m_havePreResult = false; // nach dem Laden entscheidet die Spurliste
                    self->setStereoInput(self->m_fileMvc ? QStringLiteral("sbsl") : QStringLiteral("none"));
                    self->syncOptions();
                } else {
                    self->setStereoInput(r.format);
                }
                self->loadFileNow(url, fileOptions);
            } else if (url.isEmpty() && !r.mvc) {
                if (self->m_earlyAnalysis && !self->m_detectPassed) {
                    // Die Datei lädt noch: für den Ablauf nach dem Laden aufheben (er fragt erst
                    // den Container und nimmt sonst dieses Ergebnis) und schon jetzt einstellen
                    self->m_havePreResult = true;
                    self->m_preFormat = r.format;
                    self->m_preSource = r.source;
                }
                self->applyDetectedStereo(r.format, r.source);
            } else if (url.isEmpty() && r.mvc && !self->m_mvcStream) {
                // Zwei Ansichten, die erst das Lesen der Datei verraten hat (H.264/MVC in MPEG-TS):
                // Kein Hardware-Decoder kann den Strom, auch nicht seine Basisansicht – auf den
                // Software-Decoder wechseln, sonst bliebe das Bild schwarz
                self->m_mvcStream = true;
                self->syncOptions();
                self->applyDetectedStereo(QStringLiteral("none"), QStringLiteral("mvc"));
            }
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    m_detectThread = thread;
    thread->start(url.isEmpty() ? QThread::LowPriority : QThread::InheritPriority);
    emit stereoInputChanged(); // "Bild wird geprüft …"
}

void MpvController::applyDetectedStereo(const QString &format, const QString &source)
{
    const QString newSource = format == QLatin1String("none") && source != QLatin1String("mvc") ? QString() : source;
    // Entwickler-Hilfe: LUMEN_STEREO_DEBUG=1 nennt jede Entscheidung der 3D-Erkennung
    if (qEnvironmentVariableIsSet("LUMEN_STEREO_DEBUG"))
        qWarning().noquote() << "Lumen: 3D-Erkennung" << format << (source.isEmpty() ? QStringLiteral("-") : source) << "MVC" << m_fileMvc;
    m_stereoSource = newSource;
    if (format == m_stereoIn) {
        emit stereoInputChanged(); // Status-Text
        return;
    }
    setStereoInput(format);
    if (format != QLatin1String("none"))
        showText(LTR("3D erkannt: %1").arg(source == QLatin1String("mvc") ? QStringLiteral("MVC") : stereoInLabel(format)), 2500);
}

// Text-Untertitel einer 3D-Datei: Gibt das Profil jedes Auge in einem eigenen Bereich aus
// (Side-by-Side, Top-Bottom, Frame Packing), zeichnet Lumen den Text einmal je Auge und blendet
// mpvs eigene Darstellung aus, die ihn quer über beide Augen legen würde. Die Tiefe aus dem
// Profil (subtitleDepth) holt ihn vor die Leinwand. Bild-Untertitel (PGS, VobSub) liefern keinen
// Text; dort bleibt es bei mpvs Darstellung. Blu-ray 3D führt ihre Untertitel selbst (BlurayNav).
void MpvController::updateStereoSubs()
{
    if (!m_mpv)
        return;
    const QString out = m_profile.value("stereoOut", "none").toString();
    bool want = !m_castEncoder && !m_idle && m_stereoIn != QLatin1String("none") && StereoSubs::splitsEyes(out)
                && !mvcActive() && m_sid > 0;
    bool bitmap = false;
    QString codec, subFile;
    int ffIndex = -1;
    if (want) {
        want = false;
        for (const auto &v : std::as_const(m_rawTracks)) {
            const QVariantMap t = v.toMap();
            if (t.value("type").toString() != QLatin1String("sub") || !t.value("selected").toBool())
                continue;
            codec = t.value("codec").toString();
            bitmap = StereoSubs::isBitmapCodec(codec);
            // Bilder liest Lumen selbst: aus der Datei oder aus der geladenen Untertiteldatei
            if (t.value("external").toBool()) {
                subFile = t.value("external-filename").toString();
            } else {
                subFile = m_sourceKind == QLatin1String("file") ? m_lastUrl : QString();
                ffIndex = t.value("ff-index", -1).toInt();
            }
            if (subFile.startsWith(QLatin1String("file://")))
                subFile = QUrl(subFile).toLocalFile();
            want = !bitmap || QFileInfo(subFile).isFile();
            break;
        }
    }
    if (want != m_stereoSubs) {
        m_stereoSubs = want;
        setOptionRaw(QStringLiteral("sub-visibility"), want ? "no" : "yes", false);
        emit stereoSubtitlesChanged();
    }
    m_stereoBitmap = want && bitmap;
    if (m_stereoBitmap) {
        if (!m_bitmapSubs) {
            m_bitmapSubs = new BitmapSubs(this);
            // neu bewerten: das gerade nötige Bild kann eben erst gelesen sein
            connect(m_bitmapSubs, &BitmapSubs::eventsChanged, this, &MpvController::drawStereoSubs);
        }
        if (!m_bitmapSubs->active(subFile, ffIndex)) {
            // eine geladene Untertiteldatei läuft auf der Zeitachse der Filmdatei
            m_bitmapSubs->start(subFile, ffIndex, codec, ffIndex < 0 ? std::max(0.0, m_demuxStart) : m_demuxStart);
            m_bitmapSubs->setPosition(m_position - m_subDelay);
        }
    } else if (m_bitmapSubs) {
        m_bitmapSubs->stop();
    }
    m_bitmapShown = -2;
    drawStereoSubs();
}

void MpvController::drawStereoSubs()
{
    if (!m_stereoSubs) {
        if (m_bitmapShown != -1) {
            removeOverlay(60);
            removeOverlay(61);
            m_bitmapShown = -1;
        }
        return;
    }
    const QString out = m_profile.value("stereoOut", "none").toString();
    const QList<StereoSubs::Eye> eyes = StereoSubs::eyes(out, m_osdDims);
    if (m_stereoBitmap) {
        // mpvs Untertitel-Verzögerung: positiv = später
        const BitmapSubs::Event e = m_bitmapSubs->at(m_position - m_subDelay, m_forcedSubsOnly);
        const double key = e.image.isNull() || eyes.isEmpty() ? -1 : e.start;
        if (key == m_bitmapShown)
            return;
        m_bitmapShown = key;
        if (qEnvironmentVariableIsSet("LUMEN_STEREO_DEBUG"))
            qWarning().noquote() << "Lumen: Untertitelbild je Auge" << (key < 0 ? QStringLiteral("aus") : QString::number(key, 'f', 3))
                                 << "bei" << QString::number(m_position, 'f', 3);
        if (key < 0) {
            removeOverlay(60);
            removeOverlay(61);
            return;
        }
        placeEyeOverlays(e.image, e.canvas, e.rect);
        return;
    }
    const QImage img = eyes.isEmpty() || m_subText.trimmed().isEmpty() ? QImage() : StereoSubs::render(m_subText, eyes.first().logical);
    if (img.isNull()) {
        removeOverlay(60);
        removeOverlay(61);
        return;
    }
    // Text: unten in der Mitte des Auges, 4,5 % über dem Rand
    const QSizeF logical = eyes.first().logical;
    const double bottom = logical.height() * (1 - 0.045);
    placeEyeOverlays(img, logical, QRectF(0, bottom - img.height(), img.width(), img.height()));
}

// Ein Untertitelbild in jedes Auge setzen. canvas: die Fläche, auf die sich where bezieht (das
// ganze Bild eines Auges, wie es am Ende gezeigt wird).
void MpvController::placeEyeOverlays(const QImage &img, const QSizeF &canvas, const QRectF &where)
{
    const QString out = m_profile.value("stereoOut", "none").toString();
    const QList<StereoSubs::Eye> eyes = StereoSubs::eyes(out, m_osdDims);
    // die Tiefe aus dem Profil; der Regler im Reiter „Untertitel" stellt sie während der Wiedergabe
    const double depth = m_nav ? m_nav->subtitleDepth() : m_profile.value("subtitleDepth").toDouble();
    for (int i = 0; i < eyes.size() && i < 2; ++i) {
        const StereoSubs::Eye &e = eyes.at(i);
        const double sx = e.rect.width() / canvas.width(), sy = e.rect.height() / canvas.height();
        // Tiefe in Bildpunkten eines 1920 breiten Bilds: linkes Auge nach rechts, rechtes nach links
        const double shift = (e.left ? depth : -depth) * e.rect.width() / 1920.0;
        setOverlay(60 + i, img, qRound(e.rect.x() + where.x() * sx + shift), qRound(e.rect.y() + where.y() * sy),
                   qRound(where.width() * sx), qRound(where.height() * sy));
    }
}

// Der vorbereitete Start läuft los. Prüft die 3D-Erkennung das Bild noch, kurz auf sie warten:
// Sonst begänne die Wiedergabe mit beiden Bildhälften und spränge gleich darauf um.
void MpvController::finishPrimedStart(int waitedMs)
{
    if (!m_mpv || m_idle)
        return;
    if (m_earlyAnalysis && m_detectThread && waitedMs < 500) {
        QTimer::singleShot(25, this, [this, waitedMs] { finishPrimedStart(waitedMs + 25); });
        return;
    }
    if (waitedMs > 0 && qEnvironmentVariableIsSet("LUMEN_PERF_LOG"))
        qWarning().noquote() << "Lumen: perf start: 3D-Erkennung abgewartet," << waitedMs << "ms";
    // Gezählt wird ab jetzt: Was mpv beim Einrichten (erstes Bild, Shader) als verworfen führt,
    // hat niemand vermisst
    m_dropBase = m_vo_drops + m_dec_drops;
    if (m_droppedFrames != 0) {
        m_droppedFrames = 0;
        emit droppedFramesChanged();
    }
    setOptionRaw(QStringLiteral("pause"), false, false);
}

void MpvController::holdGovernor(double seconds)
{
    m_governor.hold(seconds, m_clock.elapsed() / 1000.0);
}

void MpvController::updateTuningStatus()
{
    QStringList parts;
    if (m_governor.renderLevel() > 0)
        parts << LTR("Skalierung −%1").arg(m_governor.renderLevel());
    if (m_governor.decodeLevel() > 0)
        parts << LTR("Decoder entlastet");
    const QString status = parts.join(QStringLiteral(" · "));
    if (status != m_tuningStatus) {
        m_tuningStatus = status;
        emit tuningChanged();
    }
}

// Einmal je Sekunde: verlorene Bilder zählen und, wenn es zu viele werden, eine Stufe zurücknehmen
void MpvController::tuneTick()
{
    if (!m_mpv || m_idle)
        return;
    const bool adaptive = m_profile.value("adaptive", true).toBool();
    const bool sequential = m_stereoIn != QLatin1String("none") && m_profile.value("stereoOut").toString() == QLatin1String("seq");
    const bool advancing = m_position != m_tickPosition;
    m_tickPosition = m_position;

    const QVariantMap dec = m_decParams;
    const QString learnKey = QStringLiteral("tuning/%1/%2").arg(m_profile.value("id").toString(),
                                                                 Tuning::loadClass(dec.value("w").toInt(), dec.value("h").toInt(), m_containerFps));
    // Was diese Maschine bei solchem Material schon einmal nicht geschafft hat, gleich so beginnen
    if (adaptive && !m_learnedLoaded && dec.value("w").toInt() > 0 && m_containerFps > 1) {
        m_learnedLoaded = true;
        const QStringList learned = QSettings().value(learnKey).toString().split(QLatin1Char(';'));
        const qint64 day = QDateTime::currentSecsSinceEpoch() / 86400;
        if (learned.size() == 2 && day - learned.at(1).toLongLong() <= 14 && learned.at(0).toInt() > 0) {
            m_governor.setLevels(learned.at(0).toInt(), 0);
            syncOptions();
            updateTuningStatus();
        }
    }

    Tuning::Governor::Sample s;
    s.time = m_clock.elapsed() / 1000.0;
    s.voDrops = m_vo_drops;
    s.decoderDrops = m_dec_drops;
    s.delayed = m_delayedFrames;
    s.fps = m_containerFps;
    s.software = m_hwdec.isEmpty() || m_hwdec == QLatin1String("no");
    s.pixelRate = dec.value("w").toDouble() * dec.value("h").toDouble() * m_containerFps;
    s.steady = adaptive && advancing && !m_paused && !m_buffering && std::abs(m_speed - 1.0) < 0.01 && !m_castEncoder && !sequential;
    // Der Renderer misst seine Durchgänge selbst. Nur im eigenen Fenster von mpv fragen: im
    // eingebetteten rendert dieser Thread, und die Frage bliebe hängen, bis mpv das Bild verwirft.
    if (s.steady && !m_window && !m_castRenderer) {
        double ns = 0;
        for (const auto &pass : getProperty(QStringLiteral("vo-passes")).toMap().value("fresh").toList())
            ns += pass.toMap().value("avg").toDouble();
        if (ns > 0)
            s.renderMs = ns / 1e6;
    }

    // Entwickler-Hilfe: LUMEN_PERF_LOG=1 schreibt je Sekunde den Stand der Wiedergabe
    if (qEnvironmentVariableIsSet("LUMEN_PERF_LOG")) {
        if (m_window) {
            static qint64 lastPaints = 0;
            const qint64 paints = m_window->paintCount();
            qWarning().noquote() << "Lumen: perf fenster gezeichnet/s" << paints - lastPaints << "bildschirm"
                                 << (m_window->screen() ? m_window->screen()->refreshRate() : 0.0) << "Hz";
            lastPaints = paints;
        }
        qWarning().noquote() << QStringLiteral("Lumen: perf pos=%1 fps=%2 verworfen=%3 decoder=%4 verspaetet=%5 falscher-takt=%6 hwdec=%7 render=%8ms stufen=%9/%10 %11x%12")
                                    .arg(m_position, 0, 'f', 1)
                                    .arg(m_containerFps, 0, 'f', 2)
                                    .arg(std::max(0, m_vo_drops + m_dec_drops - m_dropBase))
                                    .arg(m_dec_drops)
                                    .arg(s.delayed)
                                    .arg(m_mistimedFrames)
                                    .arg(m_hwdec.isEmpty() ? QStringLiteral("-") : m_hwdec)
                                    .arg(s.renderMs, 0, 'f', 1)
                                    .arg(m_governor.renderLevel())
                                    .arg(m_governor.decodeLevel())
                                    .arg(dec.value("w").toInt())
                                    .arg(dec.value("h").toInt());
    }

    const Tuning::Governor::Action action = m_governor.feed(s);
    if (action == Tuning::Governor::None)
        return;
    syncOptions();
    updateTuningStatus();
    showText(LTR("Leistung angepasst: %1").arg(m_tuningStatus), 2500);
    if (action == Tuning::Governor::LowerRender)
        QSettings().setValue(learnKey, QStringLiteral("%1;%2").arg(m_governor.renderLevel()).arg(QDateTime::currentSecsSinceEpoch() / 86400));
}

// --------------------------------------------------------------------------
// Events
// --------------------------------------------------------------------------

void MpvController::onMpvEvents()
{
    while (m_mpv) {
        mpv_event *ev = mpv_wait_event(m_mpv, 0);
        if (ev->event_id == MPV_EVENT_NONE)
            break;
        handleEvent(ev);
    }
}

void MpvController::setError(const QString &e)
{
    m_lastError = e;
    emit lastErrorChanged();
}

void MpvController::handleEvent(mpv_event *ev)
{
    switch (ev->event_id) {
    case MPV_EVENT_PROPERTY_CHANGE: {
        auto *p = static_cast<mpv_event_property *>(ev->data);
        handleProperty(ev->reply_userdata, p->format, p->format == MPV_FORMAT_NONE ? nullptr : p->data);
        break;
    }
    case MPV_EVENT_LOG_MESSAGE: {
        auto *m = static_cast<mpv_event_log_message *>(ev->data);
        const QString text = QStringLiteral("%1: %2").arg(QString::fromUtf8(m->prefix), QString::fromUtf8(m->text).trimmed());
        qWarning().noquote() << "mpv" << text;
        // Einzelne Decoder-Fehler (Einstieg mitten in einem Frame, Sprungstellen) sind
        // vorübergehend und keine Störung der Wiedergabe
        const QByteArray prefix(m->prefix);
        // … ebenso wenig Meldungen von FFmpeg ohne Bezug zu einer Spur (Geräte, die es nicht gibt)
        const bool transient = prefix == "ad" || prefix == "vd" || prefix == "ffmpeg" || prefix.startsWith("ffmpeg/");
        if (m->log_level <= MPV_LOG_LEVEL_ERROR && !transient && !m_endFileError)
            setError(text);
        break;
    }
    case MPV_EVENT_START_FILE:
        m_bdOpenStarted = true;
        m_fileReady = false;
        m_endFileError = false;
        break;
    case MPV_EVENT_END_FILE: {
        m_primedStart = false;
        m_fileReady = false;
        // das Ende der vorigen Datei kommt vor dem Start der neuen: dann läuft das Öffnen noch
        if (m_bdOpenStarted)
            releaseBdOpenLock();
        auto *e = static_cast<mpv_event_end_file *>(ev->data);
        if (e->reason == MPV_END_FILE_REASON_ERROR) {
            // die häufigsten Gründe in der Sprache der Oberfläche, dazu der Name der Datei
            QString reason;
            switch (e->error) {
            case MPV_ERROR_UNKNOWN_FORMAT: reason = LTR("Dateiformat nicht erkannt"); break;
            case MPV_ERROR_LOADING_FAILED: reason = LTR("Datei ließ sich nicht öffnen"); break;
            case MPV_ERROR_NOTHING_TO_PLAY: reason = LTR("keine abspielbare Spur"); break;
            default: reason = QString::fromUtf8(mpv_error_string(e->error)); break;
            }
            const QString name = m_sourceKind == QLatin1String("file") && !m_lastUrl.isEmpty()
                                     ? QFileInfo(QUrl(m_lastUrl).isLocalFile() ? QUrl(m_lastUrl).toLocalFile() : m_lastUrl).fileName()
                                     : QString();
            QString msg = LTR("Wiedergabe fehlgeschlagen: %1").arg(name.isEmpty() ? reason : name + QStringLiteral(" – ") + reason);
            if (m_sourceKind == QLatin1String("bluray"))
                msg += LTR(" – ist die Disc für libbluray lesbar (LibreDrive-Laufwerk + externe AACS-Bibliothek)?");
            else if (m_sourceKind == QLatin1String("dvd"))
                msg += LTR(" – ist die DVD für libdvdread lesbar?");
            else if (m_sourceKind == QLatin1String("dcp"))
                msg += LTR(" – Spurdateien vollständig und Schlüssel (KDM) passend?");
            setError(msg);
            m_endFileError = true;
        }
        break;
    }
    case MPV_EVENT_CLIENT_MESSAGE: {
        auto *m = static_cast<mpv_event_client_message *>(ev->data);
        QStringList args;
        for (int i = 0; i < m->num_args; ++i)
            args << QString::fromUtf8(m->args[i]);
        if (args.value(0) == QLatin1String("lumen-plugin"))
            emit pluginMessage(args);
        else
            handleClientMessage(args);
        break;
    }
    case MPV_EVENT_FILE_LOADED:
        m_fileReady = true;
        m_vo_drops = m_dec_drops = 0;
        m_dropBase = 0;
        m_matchedFps = 0;
        m_hdrState = -1;
        // VCD-Steuerung: die Pause, die keep-open am Ende des vorigen Elements setzt, kann erst nach
        // dem Laden des nächsten ankommen – dann stünde es und die Steuerung liefe nie weiter
        if (m_vcd && m_vcd->active())
            setOptionRaw(QStringLiteral("pause"), false, false);
        holdGovernor(5);
        releaseBdOpenLock();
        m_loadedAt = m_clock.elapsed();
        // 3D-Erkennung, sobald die Spurliste der neuen Datei da ist (rebuildTracks)
        m_detectWanted = true;
        tryStereoDetection();
        emit fileLoaded();
        break;
    case MPV_EVENT_PLAYBACK_RESTART:
        if (m_primedStart) {
            m_primedStart = false;
            // Das erste Bild ist an den Renderer übergeben. Ihm Zeit zum Zeichnen lassen – und dem
            // Decoder so lange, wie er für das erste Bild gebraucht hat: Ein Software-Decoder mit
            // vielen Threads (JPEG 2000 in 4K: eine halbe Sekunde je Bild) hat die nächsten Bilder
            // in Arbeit; liefe die Uhr sofort los, kämen sie alle zu spät.
            // (Ob in Hardware dekodiert wird, meldet mpv erst kurz danach: nach 60 ms nachsehen.)
            const qint64 firstFrame = m_clock.elapsed() - m_loadedAt;
            QTimer::singleShot(60, this, [this, firstFrame] {
                const bool software = m_hwdec.isEmpty() || m_hwdec == QLatin1String("no");
                const int more = software ? int(std::clamp<qint64>(firstFrame, 60, 1500)) - 60 : 0;
                if (qEnvironmentVariableIsSet("LUMEN_PERF_LOG"))
                    qWarning().noquote() << "Lumen: perf start: erstes Bild nach" << firstFrame << "ms, warte" << 60 + more << "ms";
                QTimer::singleShot(more, this, [this] { finishPrimedStart(0); });
            });
        }
        break;
    case MPV_EVENT_SHUTDOWN:
        // Player-Fenster wurde geschlossen -> ganze App beenden
        destroy();
        if (!m_quitting)
            emit shutdownRequested();
        break;
    default:
        break;
    }
}

void MpvController::handleProperty(quint64 id, int format, void *data)
{
    auto flag = [&] { return data && format == MPV_FORMAT_FLAG ? *static_cast<int *>(data) != 0 : false; };
    auto dbl = [&](double def = 0) { return data && format == MPV_FORMAT_DOUBLE ? *static_cast<double *>(data) : def; };
    auto i64 = [&](qint64 def = -1) { return data && format == MPV_FORMAT_INT64 ? *static_cast<int64_t *>(data) : def; };
    auto str = [&] { return data && format == MPV_FORMAT_STRING ? QString::fromUtf8(*static_cast<char **>(data)) : QString(); };
    auto node = [&] { return data && format == MPV_FORMAT_NODE ? nodeToVariant(static_cast<mpv_node *>(data)) : QVariant(); };

    switch (PropId(id)) {
    case P_PAUSE:
        m_paused = flag();
        holdGovernor(2);
        emit pausedChanged();
        break;
    case P_TIMEPOS: {
        m_position = dbl();
        if (m_stereoBitmap) {
            m_bitmapSubs->setPosition(m_position - m_subDelay);
            drawStereoSubs();
        }
        if (m_snapshotAt >= 0 && m_position >= m_snapshotAt && !m_idle) {
            m_snapshotAt = -1;
            setOptionRaw(QStringLiteral("pause"), true, false);
            if (m_window) {
                // vo=libmpv kennt keinen Fenster-Screenshot: das Qt-Fenster selbst
                // auslesen (prüft den Render-Pfad, u. a. unter macOS)
                QTimer::singleShot(400, this, [this] {
                    if (m_window)
                        m_window->grabFramebuffer().save(m_snapshotFile);
                });
            } else {
                command({"screenshot-to-file", m_snapshotFile, "window"});
            }
        }
        const int bucket = int(m_position * 4); // max. 4 UI-Updates pro Sekunde
        if (bucket != m_positionBucket) {
            m_positionBucket = bucket;
            emit positionChanged();
        }
        break;
    }
    case P_DURATION: m_duration = dbl(); emit durationChanged(); break;
    case P_VOLUME: m_volume = dbl(100); emit volumeChanged(); break;
    case P_VOLMAX: m_volumeMax = dbl(130); emit volumeChanged(); break;
    case P_MUTE: m_muted = flag(); emit mutedChanged(); break;
    case P_SPEED: m_speed = dbl(1); emit speedChanged(); break;
    case P_MEDIATITLE: m_mediaTitle = str(); emit mediaChanged(); break;
    case P_PATH:
        m_path = str();
        if (m_vcd && m_vcd->active() && !m_path.isEmpty() && !m_path.startsWith(QLatin1String("lumenvcd://")))
            m_vcd->stop();
        emit mediaChanged();
        break;
    case P_IDLE:
        m_idle = flag();
        if (m_idle)
            m_tuneTimer.stop();
        else
            m_tuneTimer.start();
        if (m_idle) {
            m_position = 0;
            m_positionBucket = -1;
            emit positionChanged();
        }
        emit idleChanged();
        break;
    case P_DISCTITLES: {
        m_titles.clear();
        int i = 0;
        for (const auto &t : node().toList()) {
            const QVariantMap m = t.toMap();
            m_titles.append(QVariantMap{{"index", i++}, {"id", m.value("id")}, {"duration", m.value("length", -1)}});
        }
        emit titlesChanged();
        break;
    }
    case P_DISCTITLE: m_currentTitle = int(i64()); emit titlesChanged(); break;
    case P_CHAPTERLIST: {
        m_chapters.clear();
        int i = 0;
        for (const auto &c : node().toList()) {
            const QVariantMap m = c.toMap();
            QString title = m.value("title").toString();
            if (title.isEmpty() || title.startsWith(QLatin1String("Chapter")))
                title = LTR("Kapitel %1").arg(i + 1);
            m_chapters.append(QVariantMap{{"index", i++}, {"title", title}, {"time", m.value("time").toDouble()}});
        }
        emit chaptersChanged();
        break;
    }
    case P_CHAPTER: m_currentChapter = int(i64()); emit currentChapterChanged(); break;
    case P_TRACKLIST:
        rebuildTracks(node().toList());
        tryStereoDetection();
        break;
    case P_AID: {
        const QVariant v = node();
        m_aid = v.typeId() == QMetaType::LongLong ? v.toInt() : 0;
        emit tracksChanged();
        break;
    }
    case P_SID: {
        if (mvcActive())
            break; // 3D: Untertitelwahl führt libbluray, mpv-sid bleibt "no"
        const QVariant v = node();
        m_sid = v.typeId() == QMetaType::LongLong ? v.toInt() : 0;
        emit tracksChanged();
        break;
    }
    case P_VIDEOPARAMS:
        m_videoParams = node().toMap();
        updateVideoInfo();
        onContentFormatKnown();
        break;
    case P_DECPARAMS:
        m_decParams = node().toMap();
        continueStereoDetection();
        break;
    case P_DELAYED: m_delayedFrames = std::max<qint64>(0, i64(0)); break;
    case P_MISTIMED: m_mistimedFrames = std::max<qint64>(0, i64(0)); break;
    case P_VIDEOFORMAT: m_videoCodec = str(); updateVideoInfo(); break;
    case P_FPS:
        m_containerFps = dbl();
        updateVideoInfo();
        onContentFormatKnown();
        break;
    case P_HWDEC: m_hwdec = str(); updateVideoInfo(); break;
    case P_DISPLAYFPS: m_displayFps = dbl(); updateVideoInfo(); break;
    case P_AUDIOOUTPARAMS: {
        const QVariantMap m = node().toMap();
        const QString fmt = m.value("format").toString();
        m_audioInfo["passthrough"] = fmt.startsWith(QLatin1String("spdif"));
        m_audioInfo["format"] = fmt;
        m_audioInfo["samplerate"] = m.value("samplerate");
        m_audioInfo["channels"] = channelLabel(m.value("channel-count").toInt());
        emit audioInfoChanged();
        break;
    }
    case P_AUDIOCODEC: m_audioInfo["codec"] = prettyCodec(str(), {}); emit audioInfoChanged(); break;
    case P_AUDIODEVICES: m_audioDevices = node().toList(); emit audioDevicesChanged(); break;
    case P_ABA: m_loopA = toTime(node()); emit loopChanged(); break;
    case P_ABB: m_loopB = toTime(node()); emit loopChanged(); break;
    case P_FULLSCREEN:
        m_fullscreen = flag();
        holdGovernor(3);
        if (m_window) // vo=libmpv: Vollbild setzt das Qt-Fenster um
            m_window->setFullscreen(m_fullscreen);
        emit fullscreenChanged();
        break;
    case P_OSDDIMS:
        m_osdDims = node().toMap();
        emit osdDimensionsChanged();
        if (m_nav)
            m_nav->setOsdDimensions(node().toMap());
        if (m_dvd)
            m_dvd->setOsdDimensions(node().toMap());
        break;
    case P_MOUSEPOS: {
        const QVariantMap m = node().toMap();
        m_mouseX = m.value("x").toDouble();
        m_mouseY = m.value("y").toDouble();
        if (m_nav && m_nav->menuVisible() && m.value("hover").toBool())
            m_nav->mouseMove(m_mouseX, m_mouseY);
        if (m_dvd && m_dvd->menuVisible() && m.value("hover").toBool())
            m_dvd->mouseMove(m_mouseX, m_mouseY);
        break;
    }
    case P_VODROPS:
    case P_DECDROPS: {
        const int v = int(std::max<qint64>(0, i64(0)));
        (PropId(id) == P_VODROPS ? m_vo_drops : m_dec_drops) = v;
        const int shown = std::max(0, m_vo_drops + m_dec_drops - m_dropBase);
        if (shown != m_droppedFrames) {
            m_droppedFrames = shown;
            emit droppedFramesChanged();
        }
        break;
    }
    case P_WINDOWID: {
#ifdef Q_OS_WIN
        // mpv lädt sein Fenstersymbol aus der eigenen DLL – Lumens Symbol setzen
        if (data && format == MPV_FORMAT_INT64) {
            const auto hwnd = reinterpret_cast<HWND>(static_cast<intptr_t>(*static_cast<int64_t *>(data)));
            const HINSTANCE self = GetModuleHandleW(nullptr);
            auto icon = [&](int size) {
                return reinterpret_cast<LPARAM>(LoadImageW(self, L"IDI_ICON1", IMAGE_ICON, size, size, LR_SHARED));
            };
            if (hwnd) {
                SendMessageW(hwnd, WM_SETICON, ICON_BIG, icon(GetSystemMetrics(SM_CXICON)));
                SendMessageW(hwnd, WM_SETICON, ICON_SMALL, icon(GetSystemMetrics(SM_CXSMICON)));
            }
        }
#endif
        break;
    }
    case P_EOF: {
        const bool eof = flag();
        if (eof && !m_eof && m_queueActive)
            QMetaObject::invokeMethod(this, &MpvController::advanceQueue, Qt::QueuedConnection);
        if (eof && !m_eof && m_vcd && m_vcd->active())
            QMetaObject::invokeMethod(m_vcd, [this] { m_vcd->itemFinished(); }, Qt::QueuedConnection);
        m_eof = eof;
        break;
    }
    case P_AUDIODELAY: m_audioDelay = dbl(); emit delaysChanged(); break;
    case P_SUBDELAY:
        m_subDelay = dbl();
        emit delaysChanged();
        if (m_stereoBitmap) { // Bild-Untertitel je Auge: mit der neuen Verzögerung neu wählen
            m_bitmapSubs->setPosition(m_position - m_subDelay);
            drawStereoSubs();
        }
        break;
    case P_DEMUXSTART: {
        const double start = dbl(-1);
        if (start != m_demuxStart) {
            m_demuxStart = start;
            // Die Zeitachse ist erst jetzt bekannt: einen schon laufenden Leser neu ansetzen
            if (m_stereoBitmap && m_bitmapSubs) {
                m_bitmapSubs->stop();
                updateStereoSubs();
            }
        }
        break;
    }
    case P_SUBTEXT: {
        const QString text = str();
        if (text != m_subText) {
            m_subText = text;
            updateStereoSubs();
        }
        break;
    }
    case P_CACHEPAUSE: m_buffering = flag(); emit bufferingChanged(); break;
    case P_CACHEDUR: m_cacheSeconds = dbl(); emit bufferingChanged(); break;
    }
}

void MpvController::rebuildTracks(const QVariantList &list)
{
    m_rawTracks = list;
    m_audioTracks.clear();
    m_subtitleTracks.clear();
    int dv = 0;
    // Disc über libbluray: nur die Ströme des laufenden Clips anbieten (mpv behält die Spuren
    // früherer Playlists, etwa die des Menüs)
    QSet<int> live = m_nav && m_nav->active() ? m_nav->livePids() : QSet<int>();
    const bool known = std::any_of(list.cbegin(), list.cend(), [&live](const QVariant &v) {
        return live.contains(v.toMap().value("src-id").toInt());
    });
    if (!known)
        live.clear(); // die Spuren des Clips sind noch nicht da: nichts ausblenden
    for (const auto &v : list) {
        const QVariantMap t = v.toMap();
        const QString type = t.value("type").toString();
        if (type == QLatin1String("video") && t.value("selected").toBool())
            dv = t.value("dolby-vision-profile").toInt();
        if (type != QLatin1String("audio") && type != QLatin1String("sub"))
            continue;
        if (!live.isEmpty() && !t.value("external").toBool() && !live.contains(t.value("src-id").toInt()))
            continue;
        QStringList parts;
        // ohne Sprachangabe nur Format und Titel (statt eines Strichs)
        QString lang = t.value("lang").toString().toUpper();
        // Blu-ray: die Sprachen stehen in der Playlist, nicht im Datenstrom
        if (lang.isEmpty() && !live.isEmpty())
            lang = m_nav->pidLanguage(t.value("src-id").toInt()).toUpper();
        if (!lang.isEmpty())
            parts << lang;
        parts << prettyCodec(t.value("codec").toString(), t.value("codec-profile").toString());
        if (type == QLatin1String("audio")) {
            const QString ch = channelLabel(t.value("demux-channel-count").toInt());
            if (!ch.isEmpty())
                parts << ch;
        }
        const QString title = t.value("title").toString();
        if (!title.isEmpty())
            parts << title;
        // DVD: Sprache/Format aus der IFO (der MPEG-PS-Strom kennt keine Sprachen)
        if (m_dvd && m_dvd->active() && type == QLatin1String("audio")) {
            const QString label = m_dvd->audioLabel(t.value("src-id").toInt());
            if (!label.isEmpty())
                parts = QStringList{label};
        }
        if (t.value("forced").toBool())
            parts << LTR("erzwungen");

        QVariantMap e{
            {"id", t.value("id").toInt()},
            {"label", parts.join(QStringLiteral(" · "))},
            {"selected", t.value("selected").toBool()},
            {"external", t.value("external").toBool()},
        };
        if (!live.isEmpty() && !t.value("external").toBool())
            e.insert(QStringLiteral("order"), m_nav->pidOrder(t.value("src-id").toInt()));
        (type == QLatin1String("audio") ? m_audioTracks : m_subtitleTracks).append(e);
    }
    if (!live.isEmpty()) {
        // in der Reihenfolge der Disc; mpv zählt die Spuren, wie sie im Strom auftauchen
        const auto discOrder = [](const QVariant &a, const QVariant &b) {
            const int x = a.toMap().value(QStringLiteral("order"), 1 << 20).toInt();
            const int y = b.toMap().value(QStringLiteral("order"), 1 << 20).toInt();
            return (x < 0 ? 1 << 19 : x) < (y < 0 ? 1 << 19 : y);
        };
        std::stable_sort(m_audioTracks.begin(), m_audioTracks.end(), discOrder);
        std::stable_sort(m_subtitleTracks.begin(), m_subtitleTracks.end(), discOrder);
    }
    emit tracksChanged();
    syncDiscTracks();
    if (dv != m_dvProfile) {
        m_dvProfile = dv;
        updateVideoInfo();
    }
}

void MpvController::updateVideoInfo()
{
    QVariantMap v;
    const int w = m_videoParams.value("w").toInt();
    const int h = m_videoParams.value("h").toInt();
    const QString gamma = m_videoParams.value("gamma").toString();
    const QString prim = m_videoParams.value("primaries").toString();

    QString range = QStringLiteral("SDR");
    if (gamma == QLatin1String("pq"))
        range = QStringLiteral("HDR10");
    else if (gamma == QLatin1String("hlg"))
        range = QStringLiteral("HLG");
    // Dolby Vision: gpu-next wendet die RPU-Metadaten beim Tonemapping an; über
    // HDMI geht bei Passthrough die HDR10-Basis (echtes DV-Signal kann kein PC-Player)
    if (m_dvProfile > 0)
        range = LTR("Dolby Vision P%1").arg(m_dvProfile);
    // Digitalkino: JPEG 2000 in CIE XYZ (DCI-P3, Gamma 2.6)
    if (m_videoParams.value("pixelformat").toString().startsWith(QLatin1String("xyz")))
        range = QStringLiteral("DCI XYZ");
    v["dolbyVision"] = m_dvProfile;

    v["width"] = w;
    v["height"] = h;
    v["codec"] = prettyCodec(m_videoCodec, {});
    v["fps"] = m_containerFps;
    v["displayFps"] = m_displayFps;
    v["hwdec"] = m_hwdec;
    v["primaries"] = prim;
    v["gamma"] = gamma;
    v["range"] = w > 0 ? range : QString();
    v["pixelformat"] = m_videoParams.value("hw-pixelformat", m_videoParams.value("pixelformat"));

    QStringList parts;
    if (w > 0)
        parts << QStringLiteral("%1×%2").arg(w).arg(h);
    if (!m_videoCodec.isEmpty())
        parts << v["codec"].toString();
    if (!prim.isEmpty())
        parts << prim.toUpper();
    if (m_containerFps > 0)
        parts << LTR("%1 fps").arg(m_containerFps, 0, 'f', 3);
    if (!m_hwdec.isEmpty() && m_hwdec != QLatin1String("no"))
        parts << m_hwdec;
    v["summary"] = parts.join(QStringLiteral(" · "));

    if (v != m_videoInfo) {
        m_videoInfo = v;
        emit videoInfoChanged();
    }
}
