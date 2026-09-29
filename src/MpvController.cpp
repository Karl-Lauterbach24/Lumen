#include "MpvController.h"

#include "BlurayNav.h"
#include "DcpPackage.h"
#include "DisplayManager.h"
#include "DvdNav.h"
#include "OpticalMedia.h"
#include "PathUtil.h"
#include "PlayerWindow.h"
#include "ProfileManager.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QImage>
#include <QScreen>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QSet>
#include <QStandardPaths>
#include <QtDebug>

#include <mpv/client.h>

#include <cmath>
#include <utility>
#include <vector>

namespace {

enum PropId : quint64 {
    P_PAUSE = 1, P_TIMEPOS, P_DURATION, P_VOLUME, P_VOLMAX, P_MUTE, P_SPEED,
    P_MEDIATITLE, P_PATH, P_IDLE, P_DISCTITLES, P_DISCTITLE, P_CHAPTERLIST,
    P_CHAPTER, P_TRACKLIST, P_AID, P_SID, P_VIDEOPARAMS, P_VIDEOFORMAT, P_FPS,
    P_HWDEC, P_DISPLAYFPS, P_AUDIOOUTPARAMS, P_AUDIOCODEC, P_AUDIODEVICES,
    P_ABA, P_ABB, P_FULLSCREEN, P_AUDIODELAY, P_SUBDELAY, P_CACHEPAUSE, P_CACHEDUR,
    P_OSDDIMS, P_MOUSEPOS, P_VODROPS, P_DECDROPS, P_EOF,
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
    case 1: return QStringLiteral("Mono");
    case 2: return QStringLiteral("2.0");
    case 6: return QStringLiteral("5.1");
    case 8: return QStringLiteral("7.1");
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
    const QString snap = qEnvironmentVariable("LUMEN_PLAYER_SNAPSHOT");
    if (snap.contains(QLatin1Char('@'))) {
        m_snapshotFile = snap.section(QLatin1Char('@'), 0, -2);
        m_snapshotAt = snap.section(QLatin1Char('@'), -1).toDouble();
    }
    if (m_nav) {
        connect(m_nav, &BlurayNav::audioPidSelected, this, [this](int pid) { selectTrackByPid(QStringLiteral("audio"), pid); });
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
        m_outputStatus = QStringLiteral("Blu-ray 3D (MVC) → %1").arg(stereoOutLabel(m_profile.value("stereoOut").toString()));
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
    static const QHash<QString, QString> labels = {
        {"none", "2D"}, {"sbs2l", "Side-by-Side Half"}, {"sbsl", "Side-by-Side Full"},
        {"ab2l", "Top-and-Bottom Half"}, {"abl", "Top-and-Bottom Full"},
        {"fp", "HDMI Frame Packing 1080p"}, {"irl", "Zeilenverschachtelt"}, {"arcd", "Anaglyph"},
    };
    return labels.value(out, out);
}

bool MpvController::want3D() const
{
    return m_mvcCapable && m_nav && BlurayNav::available()
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

bool MpvController::wantsEmbedded(const QVariantMap &profile)
{
    const QString mode = profile.value("playerWindow", "auto").toString();
    if (mode == QLatin1String("embedded"))
        return true;
    if (mode == QLatin1String("native"))
        return false;
#ifdef Q_OS_MACOS
    return true; // libmpv kann unter macOS kein eigenes Fenster öffnen
#else
    return false;
#endif
}

MpvController::~MpvController()
{
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
        setError(QStringLiteral("libmpv konnte nicht initialisiert werden"));
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
        {"ytdl", "no"},
        {"alang", langs},
        {"slang", langs},
        {"sub-auto", "fuzzy"},
        {"hr-seek-framedrop", "no"},
    };
    for (auto it = base.cbegin(); it != base.cend(); ++it)
        setOptionRaw(it.key(), it.value(), true);
    for (auto it = options.cbegin(); it != options.cend(); ++it)
        setOptionRaw(it.key(), it.value(), true);

    // Entwickler-Hilfe: LUMEN_MPV_LOG=warn|info|v|debug gibt mpv-Meldungen aus
    const QByteArray logLevel = qgetenv("LUMEN_MPV_LOG");
    mpv_request_log_messages(m_mpv, logLevel.isEmpty() ? "error" : logLevel.constData());

    if (mpv_initialize(m_mpv) < 0) {
        mpv_terminate_destroy(m_mpv);
        m_mpv = nullptr;
        setError(QStringLiteral("mpv-Initialisierung fehlgeschlagen (Renderer/Optionen prüfen)"));
        return false;
    }
    observeAll();
    // FFmpeg-mvc meldet sich mit "-mvc" in der Versionskennung (z. B. n9.0.2-mvc8)
    m_mvcCapable = getProperty(QStringLiteral("ffmpeg-version")).toString().contains(QLatin1String("mvc"), Qt::CaseInsensitive);
    if (m_nav) {
        m_nav->attach(m_mpv);
        m_nav->setStereo(want3D(), m_profile.value("stereoOut").toString());
    }
    if (m_dvd)
        m_dvd->attach(m_mpv);
    Optical::attachProtocol(m_mpv);
    for (const auto &attach : m_protocols)
        attach(m_mpv);

    if (options.value("vo").toString() == QLatin1String("libmpv")) {
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

void MpvController::destroy()
{
    if (!m_mpv)
        return;
    mpv_handle *h = m_mpv;
    m_mpv = nullptr;
    mpv_set_wakeup_callback(h, nullptr, nullptr);
    // Render-Kontext muss vor dem mpv-Kern freigegeben werden
    if (m_window)
        m_window->releaseRenderContext();
    mpv_terminate_destroy(h);
    if (m_nav)
        m_nav->detach();
    if (m_dvd)
        m_dvd->detach();
    delete m_window;
    m_window = nullptr;
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
            else
                setSubtitleId(t.value("id").toInt());
            return;
        }
    }
}

void MpvController::handleClientMessage(const QStringList &args)
{
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

void MpvController::openLocation(const QString &location)
{
    if (QFileInfo::exists(location))
        openSource(QFileInfo(location).absoluteFilePath());
    else
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
        showText(QStringLiteral("Programm beendet"), 3000);
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
    command({"stop"});
}

void MpvController::seek(double seconds, bool relative)
{
    command({"osd-msg-bar", "seek", QString::number(seconds, 'f', 3), relative ? "relative" : "absolute"});
}

void MpvController::seekExact(double seconds)
{
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

void MpvController::setVolume(double v) { setOptionRaw(QStringLiteral("volume"), v, false); }
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
    QVariantMap o = ProfileManager::toMpvOptions(profile);
    if (wantsEmbedded(profile)) {
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

    // 3D: Quellformat (Laufzeit) -> Geräteformat (Profil). Ohne 3D-Gerät wird
    // eine 3D-Quelle auf das linke Auge (2D) reduziert.
    const QString out = profile.value("stereoOut", "none").toString();
    o["vf"] = stereoFilter(m_stereoIn, out);
    if (m_nav) {
        m_nav->setStereo(m_mvcCapable && BlurayNav::available() && out != QLatin1String("none"), out);
        m_nav->setSubtitleDepth(profile.value("subtitleDepth").toInt());
    }
    return o;
}

// Filterkette Quellformat -> Geräteformat.
//   Frame Packing (HDMI 1.4, 1080p24): linkes Auge oben, 45 Zeilen Lücke, rechtes
//   Auge unten = 1920x2205. Erfordert am Ausgang einen 1920x2205-Anzeigemodus.
QString MpvController::stereoFilter(const QString &in, const QString &out)
{
    if (in == QLatin1String("none"))
        return QString();
    const QString target = out == QLatin1String("none") ? QStringLiteral("ml") : out;
    if (target == QLatin1String("fp")) {
        // Zuerst auf Side-by-Side Full, linkes Auge links, normalisieren
        const QString norm = (in == QLatin1String("sbsl")) ? QString() : QStringLiteral("stereo3d=%1:sbsl,").arg(in);
        return QStringLiteral("lavfi=[%1split[a][b];[a]crop=iw/2:ih:0:0[l];[b]crop=iw/2:ih:iw/2:0,"
                              "pad=iw:ih+45:0:45[r];[l][r]vstack]").arg(norm);
    }
    if (target == in)
        return QString();
    return QStringLiteral("lavfi=[stereo3d=%1:%2]").arg(in, target);
}

void MpvController::setStereoInput(const QString &format)
{
    if (format == m_stereoIn)
        return;
    m_stereoIn = format;
    const QVariantMap opts = buildOptions(m_profile);
    m_appliedOptions["vf"] = opts.value("vf");
    setOptionRaw(QStringLiteral("vf"), opts.value("vf"), false);
    emit stereoInputChanged();
}

void MpvController::applyProfile(const QVariantMap &profile)
{
    // Vorherige Geräteänderungen zurücknehmen, falls das Ziel wechselt
    if (m_displays && profile.value("output") != m_profile.value("output"))
        m_displays->restoreAll();

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
    m_outputStatus = QStringLiteral("Profil „%1“ aktiv").arg(profile.value("name").toString());
    emit outputStatusChanged();
    emit profileApplied();
    onContentFormatKnown();
}

void MpvController::onContentFormatKnown()
{
    if (!m_displays || m_idle)
        return;
    const QString out = m_profile.value("output").toString();
    QStringList status;

    // HDMI Frame Packing braucht den Modus 1920x2205 (bei 3D immer, sonst optional Bildrate)
    const bool framePacking = mvcActive() && m_profile.value("stereoOut").toString() == QLatin1String("fp");
    if ((m_profile.value("matchRefreshRate").toBool() || framePacking) && m_containerFps > 1
        && std::abs(m_containerFps - m_matchedFps) > 0.01) {
        QString info;
        if (m_displays->matchRefreshRate(out, m_containerFps, &info, framePacking ? QSize(1920, 2205) : QSize())) {
            m_matchedFps = m_containerFps;
            status << QStringLiteral("Bildrate %1").arg(info);
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
                status << (hdr ? QStringLiteral("System-HDR an") : QStringLiteral("System-HDR aus"));
        }
    }

    if (!status.isEmpty()) {
        m_outputStatus = status.join(QStringLiteral(" · "));
        emit outputStatusChanged();
    }
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
        const bool transient = prefix == "ad" || prefix == "vd" || prefix.startsWith("ffmpeg/");
        if (m->log_level <= MPV_LOG_LEVEL_ERROR && !transient)
            setError(text);
        break;
    }
    case MPV_EVENT_END_FILE: {
        auto *e = static_cast<mpv_event_end_file *>(ev->data);
        if (e->reason == MPV_END_FILE_REASON_ERROR) {
            QString msg = QStringLiteral("Wiedergabe fehlgeschlagen: %1").arg(QString::fromUtf8(mpv_error_string(e->error)));
            if (m_sourceKind == QLatin1String("bluray"))
                msg += QStringLiteral(" – ist die Disc für libbluray lesbar (LibreDrive-Laufwerk + externe AACS-Bibliothek)?");
            else if (m_sourceKind == QLatin1String("dvd"))
                msg += QStringLiteral(" – ist die DVD für libdvdread lesbar?");
            else if (m_sourceKind == QLatin1String("dcp"))
                msg += QStringLiteral(" – Spurdateien vollständig und Schlüssel (KDM) passend?");
            setError(msg);
        }
        break;
    }
    case MPV_EVENT_CLIENT_MESSAGE: {
        auto *m = static_cast<mpv_event_client_message *>(ev->data);
        QStringList args;
        for (int i = 0; i < m->num_args; ++i)
            args << QString::fromUtf8(m->args[i]);
        handleClientMessage(args);
        break;
    }
    case MPV_EVENT_FILE_LOADED:
        m_vo_drops = m_dec_drops = 0;
        m_matchedFps = 0;
        m_hdrState = -1;
        emit fileLoaded();
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
    case P_PAUSE: m_paused = flag(); emit pausedChanged(); break;
    case P_TIMEPOS: {
        m_position = dbl();
        if (m_snapshotAt >= 0 && m_position >= m_snapshotAt && !m_idle) {
            m_snapshotAt = -1;
            setOptionRaw(QStringLiteral("pause"), true, false);
            command({"screenshot-to-file", m_snapshotFile, "window"});
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
    case P_PATH: m_path = str(); emit mediaChanged(); break;
    case P_IDLE:
        m_idle = flag();
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
                title = QStringLiteral("Kapitel %1").arg(i + 1);
            m_chapters.append(QVariantMap{{"index", i++}, {"title", title}, {"time", m.value("time").toDouble()}});
        }
        emit chaptersChanged();
        break;
    }
    case P_CHAPTER: m_currentChapter = int(i64()); emit currentChapterChanged(); break;
    case P_TRACKLIST: rebuildTracks(node().toList()); break;
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
        if (m_vo_drops + m_dec_drops != m_droppedFrames) {
            m_droppedFrames = m_vo_drops + m_dec_drops;
            emit droppedFramesChanged();
        }
        break;
    }
    case P_EOF: {
        const bool eof = flag();
        if (eof && !m_eof && m_queueActive)
            QMetaObject::invokeMethod(this, &MpvController::advanceQueue, Qt::QueuedConnection);
        m_eof = eof;
        break;
    }
    case P_AUDIODELAY: m_audioDelay = dbl(); emit delaysChanged(); break;
    case P_SUBDELAY: m_subDelay = dbl(); emit delaysChanged(); break;
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
    for (const auto &v : list) {
        const QVariantMap t = v.toMap();
        const QString type = t.value("type").toString();
        if (type == QLatin1String("video") && t.value("selected").toBool())
            dv = t.value("dolby-vision-profile").toInt();
        if (type != QLatin1String("audio") && type != QLatin1String("sub"))
            continue;
        QStringList parts;
        const QString lang = t.value("lang").toString().toUpper();
        parts << (lang.isEmpty() ? QStringLiteral("—") : lang);
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
            parts << QStringLiteral("erzwungen");

        QVariantMap e{
            {"id", t.value("id").toInt()},
            {"label", parts.join(QStringLiteral(" · "))},
            {"selected", t.value("selected").toBool()},
            {"external", t.value("external").toBool()},
        };
        (type == QLatin1String("audio") ? m_audioTracks : m_subtitleTracks).append(e);
    }
    emit tracksChanged();
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
        range = QStringLiteral("Dolby Vision P%1").arg(m_dvProfile);
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
        parts << QStringLiteral("%1 fps").arg(m_containerFps, 0, 'f', 3);
    if (!m_hwdec.isEmpty() && m_hwdec != QLatin1String("no"))
        parts << m_hwdec;
    v["summary"] = parts.join(QStringLiteral(" · "));

    if (v != m_videoInfo) {
        m_videoInfo = v;
        emit videoInfoChanged();
    }
}
