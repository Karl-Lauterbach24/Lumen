#include "BlurayNav.h"
#include "Tr.h"
#include "PathUtil.h"

#include <QDeadlineTimer>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <QPainter>
#include <QSet>
#include <QThread>

#include <mpv/client.h>
#include <mpv/stream_cb.h>

#ifdef LUMEN_HAVE_BLURAY
#include "BdOverlay.h"
#include "DiscReadAhead.h"
#include "MvcMerger.h"
#include "TsRemap.h"
#include "TsRetime.h"
#include <libbluray/bluray.h>
#include <libbluray/keys.h>
#include <libbluray/overlay.h>
#include <libbluray/player_settings.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

namespace {
constexpr double kTicks = 90000.0;
constexpr int kReadBlock = 6144 * 32;
constexpr int kVideoPid = 0x1011; // Hauptbild jeder Blu-ray
}

// ---------------------------------------------------------------------------
// Session: lebt im mpv-Demux-Thread (open/read/close), GUI greift gesperrt zu
// ---------------------------------------------------------------------------
struct BlurayNav::Session
{
    BlurayNav *nav = nullptr;
    std::atomic_bool cancel{false};
    bool menu = true;
    bool still = false;
    bool idle = false;
    QDeadlineTimer stillUntil{QDeadlineTimer::Forever};
    bool timedStill = false;
    QByteArray out;   // fertige Ausgabe (ggf. mit zugemischter MVC-Ansicht)
    int outPos = 0;
    QByteArray block; // Lesepuffer für libbluray
    bool flushed = false;
#ifdef LUMEN_HAVE_BLURAY
    BLURAY *bd = nullptr;
    std::unique_ptr<MvcMerger> mvc;
    // [BD_OVERLAY_PG], [BD_OVERLAY_IG], ARGB32 (entspricht libbluray-ARGB). HDMV-Menüs und Untertitel
    // kommen komprimiert im Thread des Aufrufers, BD-J-Grafik als ARGB aus einem Thread der Java-VM.
    QImage planes[2];
    std::mutex planeMutex;
    int playitem = 0;
    BLURAY_TITLE_INFO *playlistInfo = nullptr;
    int64_t tsOffset = 0; // 90 kHz: Zeit auf der Zeitachse minus Zeitstempel des laufenden Clips
    // Zeitachse über Playlists hinweg (Menü: Logos, Vorspann, Menüschleifen), siehe retime()
    int64_t tsBase = 0;     // Versatz der laufenden Playlist
    int64_t tsEnd = -1;     // Ende des zuletzt ausgegebenen Bildes, -1 = noch keines
    int64_t tsSegment = -1; // höchste Bildzeit seit dem letzten Sprung oder Playlistbeginn
    bool rebase = false;    // die nächsten Bilddaten schließen an tsEnd an
    TsRemap remap;          // eigene PID für Ströme, deren Format sich mit der Playlist ändert
    int audioStream = 1;    // Wahl der Disc (Menü "Sprachen"): Nummer der Tonspur, 1 = erste
    int pgStream = 0;       // Nummer der Untertitel, 0 = keine Angabe
    bool pgOn = false;
    bool drained = false;   // die Playlist ist bis zu ihrem Ende gelesen (BD_EVENT_END_OF_TITLE)
    std::atomic_bool userSeek{false}; // der nächste Sprung kommt vom Nutzer (Lumen leert mpvs Puffer selbst)
    QString discRoot;       // Ordner der eingehängten Disc; leer bei einem Abbild oder Laufwerk
    std::unique_ptr<QFile> dump; // LUMEN_BD_DUMP=<datei>: alles, was mpv bekommt, mitschreiben

    ~Session()
    {
        BlurayReadAhead::hintNext(QString());
        mvc.reset(); // schließt die Datei der abhängigen Ansicht vor bd_close()
        if (playlistInfo)
            bd_free_title_info(playlistInfo);
        if (bd)
            bd_close(bd);
    }

    static void overlayProc(void *handle, const BD_ARGB_OVERLAY *const ov);
    static void yuvOverlayProc(void *handle, const BD_OVERLAY *const ov);
    void flushOverlay();
    void handleEvent(const BD_EVENT &ev);
    void startMvc(uint32_t playlist);
    void startClip();
    void announceNextClip();
    int64_t clipOffset() const;
    int64_t frameTicks() const;
    void updateOffset();
    void startTimeline(const uint8_t *raw, size_t size);
    void retime(int from);
    int64_t read(char *buf, uint64_t size);
#endif
};

#ifdef LUMEN_HAVE_BLURAY

void BlurayNav::Session::overlayProc(void *handle, const BD_ARGB_OVERLAY *const ov)
{
    auto *s = static_cast<Session *>(handle);
    std::lock_guard<std::mutex> lock(s->planeMutex);
    if (!ov) { // libbluray schließt alle Ebenen
        s->planes[0] = s->planes[1] = QImage();
        s->flushOverlay();
        return;
    }
    if (ov->plane > 1)
        return;
    QImage &plane = s->planes[ov->plane];

    switch (ov->cmd) {
    case BD_ARGB_OVERLAY_INIT:
        plane = QImage(ov->w, ov->h, QImage::Format_ARGB32);
        plane.fill(Qt::transparent);
        break;
    case BD_ARGB_OVERLAY_CLOSE:
        plane = QImage();
        s->flushOverlay();
        break;
    case BD_ARGB_OVERLAY_DRAW: {
        if (plane.isNull())
            break;
        const QRect r = QRect(ov->x, ov->y, ov->w, ov->h).intersected(plane.rect());
        for (int y = 0; y < r.height(); ++y) {
            auto *dst = reinterpret_cast<uint32_t *>(plane.scanLine(r.y() + y)) + r.x();
            if (ov->argb)
                std::memcpy(dst, ov->argb + size_t(y) * ov->stride, size_t(r.width()) * 4);
            else
                std::memset(dst, 0, size_t(r.width()) * 4);
        }
        break;
    }
    case BD_ARGB_OVERLAY_FLUSH:
        s->flushOverlay();
        break;
    default:
        break;
    }
}

// Komprimierte Grafik: Menüs im HDMV-Modus (IG) und Untertitel (PG), siehe BdOverlay
void BlurayNav::Session::yuvOverlayProc(void *handle, const BD_OVERLAY *const ov)
{
    auto *s = static_cast<Session *>(handle);
    std::lock_guard<std::mutex> lock(s->planeMutex);
    if (!ov) { // libbluray schließt alle Ebenen
        s->planes[0] = s->planes[1] = QImage();
        s->flushOverlay();
        return;
    }
    if (ov->plane <= 1 && BdOverlay::apply(s->planes[ov->plane], *ov))
        s->flushOverlay();
}

// PG (Untertitel, nur im 3D-Modus von libbluray dekodiert) unter IG (Menü) legen,
// sichtbaren Bereich ausschneiden und an den GUI-Thread geben. Aufrufer hält planeMutex.
void BlurayNav::Session::flushOverlay()
{
    QSize size;
    for (const QImage &p : planes)
        if (!p.isNull())
            size = size.expandedTo(p.size());
    if (size.isEmpty()) {
        QMetaObject::invokeMethod(nav, [n = nav] { n->onOverlay(QImage(), QRect()); }, Qt::QueuedConnection);
        return;
    }
    QImage comp(size, QImage::Format_ARGB32_Premultiplied);
    comp.fill(Qt::transparent);
    {
        QPainter painter(&comp);
        for (const QImage &p : planes)
            if (!p.isNull())
                painter.drawImage(0, 0, p);
    }
    int x0 = size.width(), y0 = size.height(), x1 = -1, y1 = -1;
    for (int y = 0; y < comp.height(); ++y) {
        const auto *row = reinterpret_cast<const uint32_t *>(comp.constScanLine(y));
        for (int x = 0; x < comp.width(); ++x) {
            if (row[x] >> 24) {
                x0 = std::min(x0, x); x1 = std::max(x1, x);
                y0 = std::min(y0, y); y1 = std::max(y1, y);
            }
        }
    }
    const QRect box = x1 >= 0 ? QRect(QPoint(x0, y0), QPoint(x1, y1)) : QRect();
    const QImage img = box.isNull() ? QImage() : comp.copy(box);
    QMetaObject::invokeMethod(nav, [n = nav, img, box, size] {
        n->m_planeSize = size;
        n->onOverlay(img, box);
    }, Qt::QueuedConnection);
}

void BlurayNav::Session::startMvc(uint32_t playlist)
{
    if (!mvc)
        return;
    const bool on = mvc->setPlaylist(playlist, out, drained);
    QMetaObject::invokeMethod(nav, [n = nav, on, right = mvc->baseViewIsRight()] {
        if (n->m_mvcActive == on && n->m_baseRight == right)
            return;
        n->m_mvcActive = on;
        n->m_baseRight = right;
        n->setStatus(on ? LTR("Blu-ray 3D: beide Ansichten aktiv") : n->m_status);
        emit n->mvcChanged();
        n->updateOverlay();
    }, Qt::QueuedConnection);
}

// Jeder Clip zählt seine Zeitstempel für sich. Ohne Umrechnung sieht mpv an jeder Clipgrenze
// einen Zeitsprung, setzt die Wiedergabe zurück und verliert dabei Bilder. Die Playlist nennt
// für jeden Clip Beginn (in_time) und Platz auf ihrer Zeitachse (start_time). Der erste Clip
// behält seine Zeitstempel (DTS und PCR liegen vor der ersten PTS und dürfen nicht unter null
// rutschen), die folgenden schließen daran an.
int64_t BlurayNav::Session::clipOffset() const
{
    if (playlistInfo && playitem > 0 && playitem < int(playlistInfo->clip_count)) {
        const BLURAY_CLIP_INFO &c = playlistInfo->clips[playitem];
        return int64_t(c.start_time) - int64_t(c.in_time) + int64_t(playlistInfo->clips[0].in_time);
    }
    return 0;
}

void BlurayNav::Session::updateOffset()
{
    tsOffset = tsBase + clipOffset();
}

// Dauer eines Bildes des laufenden Clips in 90-kHz-Ticks (Kennzahl der Bildrate aus der Playlist)
int64_t BlurayNav::Session::frameTicks() const
{
    if (playlistInfo && playitem < int(playlistInfo->clip_count) && playlistInfo->clips[playitem].video_stream_count > 0) {
        switch (playlistInfo->clips[playitem].video_streams[0].rate) {
        case BLURAY_VIDEO_RATE_24: return 3750;
        case BLURAY_VIDEO_RATE_25: return 3600;
        case BLURAY_VIDEO_RATE_30000_1001: return 3003;
        case BLURAY_VIDEO_RATE_50: return 1800;
        case BLURAY_VIDEO_RATE_60000_1001: return 1502;
        default: break;
        }
    }
    return 3754; // 23,976 Bilder/s
}

// Auch jede Playlist zählt ihre Zeit von vorn, die meisten ab derselben Zahl. Beim Start einer Disc
// folgen mehrere aufeinander (Logos, Hinweise, Menü), ein Menü läuft in einer Schleife. Ohne
// Umrechnung springt die Zeit an jedem Wechsel zurück: mpv hält das Bild an und verwirft, was
// "in der Vergangenheit" liegt – kurze Logos ganz, vom Menü die ersten Sekunden. Darum schließt
// eine neue Playlist an das Ende des zuletzt ausgegebenen Bildes an. Sprünge des Nutzers innerhalb
// einer Playlist bleiben, wie sie sind: dort leert Lumen mpvs Puffer selbst.
//
// raw: der nächste Block von libbluray, noch vor dem Zumischen der zweiten Ansicht (das gibt jedes
// Bild erst mit dem Beginn des nächsten aus; Ton davor bekäme sonst noch den alten Versatz).
void BlurayNav::Session::startTimeline(const uint8_t *raw, size_t size)
{
    const TsStamps video = scanM2ts(raw, size, kVideoPid);
    if (video.first < 0)
        return;
    rebase = false;
    tsBase = tsEnd - (video.first + clipOffset());
    updateOffset();
}

// Zeitstempel der M2TS-Pakete in out ab der Stelle from auf die Zeitachse legen
void BlurayNav::Session::retime(int from)
{
    if (from >= out.size())
        return;
    auto *data = reinterpret_cast<uint8_t *>(out.data()) + from;
    const size_t size = size_t(out.size() - from);
    retimeM2ts(data, size, tsOffset);
    const TsStamps video = scanM2ts(data, size, kVideoPid);
    if (video.max >= 0) {
        tsSegment = std::max(tsSegment, video.max);
        tsEnd = tsSegment + frameTicks();
    }
    remap.process(data, size);
}

// Dem Vorausleser den Clip nennen, der in der Playlist folgt: Sein Anfang liegt dann schon im
// Arbeitsspeicher, wenn libbluray an der Clipgrenze die nächste Datei öffnet (Discs mit vielen
// kurzen Clips: nahtlose Verzweigung). Nur für eine eingehängte Disc – in einem Abbild liegen die
// Clips ohnehin hintereinander.
void BlurayNav::Session::announceNextClip()
{
    QString next;
    if (!discRoot.isEmpty() && playitem + 1 < int(playlistInfo->clip_count)) {
        const BLURAY_CLIP_INFO &c = playlistInfo->clips[playitem + 1];
        const QString id = QString::fromLatin1(c.clip_id, int(qstrnlen(c.clip_id, sizeof(c.clip_id))));
        if (!id.isEmpty())
            next = QDir(discRoot).filePath(QStringLiteral("BDMV/STREAM/%1.m2ts").arg(id));
    }
    BlurayReadAhead::hintNext(next);
}

// Ein Clip beginnt: seinen Strömen, deren Format von dem abweicht, was der Demuxer unter derselben
// PID schon kennt, eine eigene PID geben (TsRemap) und der Oberfläche sagen, welche Spuren es
// jetzt gibt und welche die Disc vorsieht.
void BlurayNav::Session::startClip()
{
    if (!playlistInfo || playitem < 0 || playitem >= int(playlistInfo->clip_count))
        return;
    announceNextClip();
    const BLURAY_CLIP_INFO &c = playlistInfo->clips[playitem];
    QList<TsRemap::Stream> streams;
    const auto add = [&streams](const BLURAY_STREAM_INFO *s, int n) {
        for (int i = 0; i < n; ++i)
            streams.append({s[i].pid, s[i].coding_type});
    };
    add(c.video_streams, c.video_stream_count);
    add(c.audio_streams, c.audio_stream_count);
    add(c.pg_streams, c.pg_stream_count);
    add(c.ig_streams, c.ig_stream_count);
    add(c.sec_audio_streams, c.sec_audio_stream_count);
    add(c.sec_video_streams, c.sec_video_stream_count);
    remap.setStreams(streams);

    QSet<int> live;
    QHash<int, int> source;
    for (const TsRemap::Stream &s : std::as_const(streams)) {
        const int out = remap.map(s.pid);
        live.insert(out);
        if (out != s.pid)
            source.insert(out, s.pid);
    }
    // Sprache und Platz in der Liste der Disc (der Datenstrom selbst nennt keine Sprachen)
    QHash<int, QString> langs;
    QHash<int, int> order;
    const auto describe = [&](const BLURAY_STREAM_INFO *s, int n) {
        for (int i = 0; i < n; ++i) {
            const int out = remap.map(s[i].pid);
            order.insert(out, i);
            const QString lang = QString::fromLatin1(reinterpret_cast<const char *>(s[i].lang), int(qstrnlen(reinterpret_cast<const char *>(s[i].lang), 3)));
            if (!lang.trimmed().isEmpty())
                langs.insert(out, lang.trimmed());
        }
    };
    describe(c.audio_streams, c.audio_stream_count);
    describe(c.pg_streams, c.pg_stream_count);
    const int video = c.video_stream_count > 0 ? remap.map(c.video_streams[0].pid) : 0;
    const int a = audioStream >= 1 && audioStream <= c.audio_stream_count ? audioStream : 1;
    const int audio = c.audio_stream_count > 0 ? remap.map(c.audio_streams[a - 1].pid) : 0;
    const int sub = pgOn && pgStream >= 1 && pgStream <= c.pg_stream_count ? remap.map(c.pg_streams[pgStream - 1].pid) : 0;
    QMetaObject::invokeMethod(nav, [n = nav, live, source, langs, order, video, audio, sub] {
        n->m_livePids = live;
        n->m_pidSource = source;
        n->m_pidLang = langs;
        n->m_pidOrder = order;
        n->m_videoPid = video;
        n->m_audioPid = audio;
        n->m_subPid = sub;
        emit n->streamsChanged();
    }, Qt::QueuedConnection);
}

void BlurayNav::Session::handleEvent(const BD_EVENT &ev)
{
    BlurayNav *n = nav;
    auto post = [n](auto fn) { QMetaObject::invokeMethod(n, fn, Qt::QueuedConnection); };

    switch (ev.event) {
    case BD_EVENT_ERROR:
    case BD_EVENT_READ_ERROR:
        post([n] { n->setStatus(LTR("Lesefehler auf der Disc")); });
        break;
    case BD_EVENT_ENCRYPTED:
        post([n] { n->setStatus(LTR("Disc ist verschlüsselt und extern nicht entschlüsselt (LibreDrive/AACS-Bibliothek prüfen)")); });
        break;
    case BD_EVENT_TITLE:
        post([n, t = int(ev.param)] { n->m_title = t; emit n->stateChanged(); });
        break;
    case BD_EVENT_PLAYLIST: {
        // Was die alte Playlist noch ausgibt (3D: ihr letztes Bild), trägt deren Zeitstempel
        const int from = out.size();
        startMvc(ev.param);
        retime(from);
        if (playlistInfo)
            bd_free_title_info(playlistInfo);
        playlistInfo = bd_get_playlist_info(bd, ev.param, 0);
        playitem = 0;
        // die neue Playlist schließt an das zuletzt ausgegebene Bild an
        rebase = tsEnd >= 0;
        tsSegment = -1;
        updateOffset();
        startClip();
        QVariantList chapters;
        double duration = 0;
        if (playlistInfo) {
            duration = playlistInfo->duration / kTicks;
            for (uint32_t i = 0; i < playlistInfo->chapter_count; ++i)
                chapters.append(QVariantMap{{"index", int(i)},
                                            {"title", LTR("Kapitel %1").arg(i + 1)},
                                            {"time", playlistInfo->chapters[i].start / kTicks}});
        }
        post([n, pl = int(ev.param), chapters, duration] {
            n->m_playlist = pl;
            n->m_chapters = chapters;
            n->m_duration = duration;
            emit n->stateChanged();
        });
        break;
    }
    case BD_EVENT_PLAYITEM: {
        // Was der alte Clip noch ausgibt, trägt dessen Zeitstempel
        const int from = out.size();
        if (mvc)
            mvc->setPlayItem(int(ev.param), out);
        retime(from);
        playitem = int(ev.param);
        updateOffset();
        startClip();
        break;
    }
    case BD_EVENT_SEEK:
        // Springt die Disc selbst (Java-Menü, das Ausschnitte des Films zeigt), schließt das Neue an
        // das zuletzt ausgegebene Bild an – ein Sprung zurück hielte das Bild sonst an, bis die Zeit
        // wieder aufgeholt ist. Nach einem Sprung des Nutzers bleibt die Zeit der Playlist: dort
        // leert Lumen mpvs Puffer selbst.
        if (!userSeek.exchange(false))
            rebase = tsEnd >= 0;
        // für den Anschluss der nächsten Playlist zählt nur, was nach dem Sprung kam
        tsSegment = -1;
        [[fallthrough]];
    case BD_EVENT_DISCONTINUITY:
        if (mvc)
            mvc->reset();
        break;
    case BD_EVENT_CHAPTER:
        post([n, c = int(ev.param) - 1] { n->m_chapter = c; emit n->positionChanged(); });
        break;
    // Die Wahl im Menü der Disc gilt dem Film: das Menü selbst hat meist nur eine Tonspur. Die
    // Nummer merken, startClip() wendet sie auf den Clip an, der dann beginnt.
    case BD_EVENT_AUDIO_STREAM:
        if (ev.param >= 1)
            audioStream = int(ev.param);
        if (playlistInfo && playitem < int(playlistInfo->clip_count) && ev.param >= 1) {
            const BLURAY_CLIP_INFO &c = playlistInfo->clips[playitem];
            if (ev.param <= c.audio_stream_count)
                post([n, pid = int(remap.map(c.audio_streams[ev.param - 1].pid))] {
                    n->m_audioPid = pid;
                    emit n->audioPidSelected(pid);
                });
        }
        break;
    case BD_EVENT_PG_TEXTST_STREAM:
        pgStream = int(ev.param);
        if (playlistInfo && playitem < int(playlistInfo->clip_count) && ev.param >= 1) {
            const BLURAY_CLIP_INFO &c = playlistInfo->clips[playitem];
            if (ev.param <= c.pg_stream_count)
                post([n, on = pgOn, pid = int(remap.map(c.pg_streams[ev.param - 1].pid))] {
                    n->m_subPid = on ? pid : 0;
                    emit n->subtitlePidSelected(pid, true);
                });
        }
        break;
    case BD_EVENT_PG_TEXTST:
        pgOn = ev.param != 0;
        if (!ev.param)
            post([n] {
                n->m_subPid = 0;
                emit n->subtitlePidSelected(0, false);
            });
        break;
    case BD_EVENT_STILL:
        still = ev.param != 0;
        post([n, st = still] { n->m_still = st; emit n->stateChanged(); });
        break;
    case BD_EVENT_STILL_TIME:
        // param = Sekunden (0 = bis zur Eingabe). Nach Ablauf bd_read_skip_still().
        still = true;
        if (ev.param > 0 && !timedStill) {
            timedStill = true;
            stillUntil = QDeadlineTimer(qint64(ev.param) * 1000);
        }
        break;
    case BD_EVENT_IDLE:
        idle = true;
        break;
    case BD_EVENT_END_OF_TITLE:
        drained = true;
        break;
    case BD_EVENT_MENU:
        post([n, on = ev.param != 0] { n->m_menuVisible = on; emit n->stateChanged(); });
        break;
    case BD_EVENT_POPUP:
        post([n, on = ev.param != 0] { n->m_popupAvailable = on; emit n->stateChanged(); });
        break;
    default:
        break;
    }
}

int64_t BlurayNav::Session::read(char *buf, uint64_t size)
{
    while (!cancel) {
        // 1. Fertige Ausgabe abgeben
        if (outPos < out.size()) {
            const int n = int(std::min<uint64_t>(size, uint64_t(out.size() - outPos)));
            std::memcpy(buf, out.constData() + outPos, size_t(n));
            if (dump)
                dump->write(buf, n);
            outPos += n;
            if (outPos >= out.size()) {
                out.clear();
                outPos = 0;
            }
            return n;
        }

        // 2. Nächsten Block von libbluray holen ("Aligned Units", 3 x 2048 Byte)
        if (block.size() != kReadBlock)
            block.resize(kReadBlock);
        BD_EVENT ev;
        still = false;
        idle = false;
        QElapsedTimer took;
        took.start();
        const int r = bd_read_ext(bd, reinterpret_cast<unsigned char *>(block.data()), kReadBlock, &ev);
        // steht der Aufruf im Laufwerk, fehlen gleich darauf Bilder – nicht, weil die Maschine zu langsam wäre
        DiscReadAhead::noteSourceRead(took.nsecsElapsed() / 1e9);
        do {
            if (ev.event != BD_EVENT_NONE)
                handleEvent(ev);
        } while (bd_get_event(bd, &ev) && ev.event != BD_EVENT_NONE);

        if (r < 0)
            return -1;
        if (r > 0) {
            drained = false;
            timedStill = false;
            if (rebase)
                startTimeline(reinterpret_cast<const uint8_t *>(block.constData()), size_t(r));
            const int from = out.size();
            if (mvc && mvc->active())
                mvc->process(reinterpret_cast<const uint8_t *>(block.constData()), size_t(r), out);
            else
                out.append(block.constData(), r);
            retime(from);
            continue;
        }
        if (timedStill && stillUntil.hasExpired()) {
            timedStill = false;
            bd_read_skip_still(bd);
            continue;
        }
        // Titelmodus ohne Daten und ohne Standbild = Ende (vorher letzte 3D-Einheit ausgeben)
        if (!menu && !still && !idle) {
            if (mvc && mvc->active() && !flushed) {
                flushed = true;
                const int from = out.size();
                mvc->flush(out);
                retime(from);
                continue;
            }
            return 0;
        }
        // Menü wartet auf Eingabe / Standbild: mpv erwartet einen blockierenden Read
        QThread::msleep(15);
    }
    return -1;
}

#endif // LUMEN_HAVE_BLURAY

// ---------------------------------------------------------------------------

BlurayNav::BlurayNav(QObject *parent)
    : QObject(parent)
{
    m_poll.setInterval(250);
    connect(&m_poll, &QTimer::timeout, this, &BlurayNav::pollPosition);
}

BlurayNav::~BlurayNav()
{
    detach();
}

bool BlurayNav::available()
{
#ifdef LUMEN_HAVE_BLURAY
    return true;
#else
    return false;
#endif
}

void BlurayNav::attach(mpv_handle *mpv)
{
    m_mpv = mpv;
#ifdef LUMEN_HAVE_BLURAY
    mpv_stream_cb_add_ro(mpv, "lumenbd", this, [](void *ud, char *uri, mpv_stream_cb_info *info) {
        return BlurayNav::openStream(ud, uri, info);
    });
#endif
}

void BlurayNav::detach()
{
    // Nach mpv_terminate_destroy() sind alle Streams geschlossen
    m_mpv = nullptr;
    m_poll.stop();
    m_overlay = QImage();
    if (m_active || m_mvcActive) {
        m_active = false;
        m_menuVisible = false;
        m_mvcActive = false;
        emit stateChanged();
        emit mvcChanged();
    }
}

QString BlurayNav::prepare(const QString &device, const QString &mode, int playlist)
{
    m_device = device;
    m_mode = mode;
    m_requestedPlaylist = playlist;
    return QStringLiteral("lumenbd://%1").arg(mode == QLatin1String("playlist") ? QStringLiteral("playlist/%1").arg(playlist) : mode);
}

void BlurayNav::setStereo(bool want3d, const QString &layout)
{
    m_want3d = want3d;
    m_layout = layout;
    updateOverlay();
}

void BlurayNav::setSubtitleDepth(int px)
{
    px = std::clamp(px, -60, 60);
    if (px == m_subtitleDepth)
        return;
    m_subtitleDepth = px;
    emit subtitleDepthChanged();
    updateOverlay();
}

void BlurayNav::setStatus(const QString &s)
{
    if (s == m_status)
        return;
    m_status = s;
    emit statusChanged();
}

int BlurayNav::openStream(void *userData, char *, void *infoPtr)
{
#ifdef LUMEN_HAVE_BLURAY
    auto *nav = static_cast<BlurayNav *>(userData);
    auto *info = static_cast<mpv_stream_cb_info *>(infoPtr);
    auto post = [nav](auto fn) { QMetaObject::invokeMethod(nav, fn, Qt::QueuedConnection); };
    auto fail = [&](Session *s, const QString &msg) {
        post([nav, msg] { nav->setStatus(msg); });
        delete s;
        return int(MPV_ERROR_LOADING_FAILED);
    };

    auto *s = new Session;
    s->nav = nav;
    s->menu = nav->m_mode == QLatin1String("menu");
    if (QFileInfo(nav->m_device).isDir())
        s->discRoot = nav->m_device;
    {
        QMutexLocker lock(&blurayOpenMutex());
        s->bd = bd_open(blurayPath(nav->m_device).toUtf8().constData(), nullptr);
    }
    if (!s->bd)
        return fail(s, LTR("libbluray konnte die Disc nicht öffnen"));

    const BLURAY_DISC_INFO *di = bd_get_disc_info(s->bd);
    if (di && di->aacs_detected && !di->aacs_handled)
        return fail(s, LTR("AACS nicht extern gelöst – Wiedergabe über libbluray nicht möglich"));
    // Menü nicht möglich (BD-J ohne Java oder ohne das JAR von libbluray): statt abzubrechen den
    // Hauptfilm spielen und sagen, warum
    QString noMenu;
    if (s->menu && di && di->bdj_detected && !di->bdj_handled && !di->first_play_supported && !di->top_menu_supported)
        noMenu = di->libjvm_detected ? LTR("Disc-Menü (BD-J): JAR von libbluray fehlt – Hauptfilm läuft")
                                     : LTR("Disc-Menü (BD-J) braucht Java – Hauptfilm läuft");

    // Spieler-Einstellungen: Sprache (ISO 639-2/T, z. B. "deu") und Region aus dem System
    const QLocale loc;
    const QByteArray lang = QLocale::languageToCode(loc.language(), QLocale::ISO639Part2T).toLatin1();
    const QByteArray country = QLocale::territoryToCode(loc.territory()).toLower().toLatin1();
    if (!lang.isEmpty()) {
        bd_set_player_setting_str(s->bd, BLURAY_PLAYER_SETTING_AUDIO_LANG, lang.constData());
        bd_set_player_setting_str(s->bd, BLURAY_PLAYER_SETTING_PG_LANG, lang.constData());
        bd_set_player_setting_str(s->bd, BLURAY_PLAYER_SETTING_MENU_LANG, lang.constData());
    }
    if (!country.isEmpty())
        bd_set_player_setting_str(s->bd, BLURAY_PLAYER_SETTING_COUNTRY_CODE, country.constData());
    // Region: A (Amerika/Ostasien) = 1, B (Europa/Afrika/Ozeanien) = 2, C (Rest Asiens) = 4
    static const QSet<QByteArray> regionA = {"us", "ca", "mx", "br", "ar", "cl", "co", "pe", "jp", "kr", "tw", "hk", "ph", "th", "id", "my", "sg", "vn"};
    static const QSet<QByteArray> regionC = {"cn", "ru", "in", "pk", "bd", "np", "lk", "mn", "kz", "by", "ua"};
    const uint32_t region = regionA.contains(country) ? 1 : regionC.contains(country) ? 4 : 2;
    bd_set_player_setting(s->bd, BLURAY_PLAYER_SETTING_REGION_CODE, region);
    bd_set_player_setting(s->bd, BLURAY_PLAYER_SETTING_PARENTAL, 99);
    // UHD-Discs prüfen teils das Spielerprofil (PSR31) -> Profil 6 (UHD) melden
    if (di && di->video_format == BLURAY_VIDEO_FORMAT_2160P)
        bd_set_player_setting(s->bd, BLURAY_PLAYER_SETTING_PLAYER_PROFILE, BLURAY_PLAYER_PROFILE_6_v3_1);

    // 3D: Disc soll 3D-Playlists wählen; Untertitel rendert libbluray (je Auge platziert)
    const bool want3d = nav->m_want3d && di && di->content_exist_3D;
    if (want3d) {
        bd_set_player_setting(s->bd, BLURAY_PLAYER_SETTING_OUTPUT_PREFER, BLURAY_OUTPUT_PREFER_3D);
        bd_set_player_setting(s->bd, BLURAY_PLAYER_SETTING_DISPLAY_CAP, BLURAY_DCAP_1080p_720p_3D);
        bd_set_player_setting(s->bd, BLURAY_PLAYER_SETTING_DECODE_PG, 1);
        s->mvc = std::make_unique<MvcMerger>(s->bd);
    }

    bd_register_overlay_proc(s->bd, s, &Session::yuvOverlayProc);
    bd_register_argb_overlay_proc(s->bd, s, &Session::overlayProc, nullptr);

    if (s->menu && noMenu.isEmpty() && !bd_play(s->bd))
        noMenu = LTR("Disc-Menü ließ sich nicht starten – Hauptfilm läuft");
    const bool fellBack = !noMenu.isEmpty();
    if (fellBack) {
        s->menu = false;
        post([nav] { nav->m_mode = QStringLiteral("main"); });
    }
    if (!s->menu) {
        uint32_t playlist = uint32_t(nav->m_requestedPlaylist);
        bool ok = false;
        if (!fellBack && nav->m_mode == QLatin1String("playlist")) {
            bd_get_titles(s->bd, TITLES_ALL, 0); // Pflicht vor bd_select_playlist()
            ok = bd_select_playlist(s->bd, playlist);
        } else { // "main": Hauptfilm
            bd_get_titles(s->bd, TITLES_RELEVANT, 0);
            const int main = bd_get_main_title(s->bd);
            if (main >= 0 && bd_select_title(s->bd, uint32_t(main))) {
                if (BLURAY_TITLE_INFO *ti = bd_get_title_info(s->bd, uint32_t(main), 0)) {
                    playlist = ti->playlist;
                    bd_free_title_info(ti);
                }
                ok = true;
            }
        }
        if (!ok)
            return fail(s, LTR("Titel konnte nicht geöffnet werden"));
        // Im Titelmodus kommen PLAYLIST-/PLAYITEM-Ereignisse nicht zuverlässig -> direkt setzen
        BD_EVENT ev{BD_EVENT_PLAYLIST, playlist};
        s->handleEvent(ev);
        if (want3d && di->content_exist_3D)
            bd_select_stream(s->bd, BLURAY_PG_TEXTST_STREAM, 1, 0); // Untertitel erst auf Wunsch
    }

    {
        std::lock_guard<std::mutex> lock(nav->m_mutex);
        nav->m_session = s;
    }
    post([nav, noMenu] {
        nav->m_menuFallback = !noMenu.isEmpty();
        nav->m_active = true;
        nav->m_position = 0;
        nav->m_poll.start();
        nav->setStatus(!noMenu.isEmpty() ? noMenu : nav->menuMode() ? LTR("Disc-Menü aktiv") : LTR("Titel über libbluray"));
        emit nav->stateChanged();
    });

    if (qEnvironmentVariableIsSet("LUMEN_BD_DUMP")) {
        s->dump = std::make_unique<QFile>(qEnvironmentVariable("LUMEN_BD_DUMP"));
        if (!s->dump->open(QIODevice::WriteOnly))
            s->dump.reset();
    }
    info->cookie = s;
    info->read_fn = [](void *c, char *buf, uint64_t n) { return static_cast<Session *>(c)->read(buf, n); };
    info->close_fn = [](void *c) {
        auto *sess = static_cast<Session *>(c);
        sess->nav->closeSession(sess);
    };
    info->cancel_fn = [](void *c) { static_cast<Session *>(c)->cancel = true; };
    return 0;
#else
    Q_UNUSED(userData)
    Q_UNUSED(infoPtr)
    return MPV_ERROR_LOADING_FAILED;
#endif
}

void BlurayNav::closeSession(Session *s)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_session == s)
            m_session = nullptr;
    }
    delete s;
    QMetaObject::invokeMethod(this, [this] {
        m_poll.stop();
        m_active = false;
        m_menuFallback = false;
        m_menuVisible = false;
        m_popupAvailable = false;
        m_still = false;
        m_chapters.clear();
        m_livePids.clear();
        m_pidSource.clear();
        m_pidLang.clear();
        m_pidOrder.clear();
        m_videoPid = m_audioPid = m_subPid = 0;
        const bool hadMvc = m_mvcActive;
        m_mvcActive = false;
        onOverlay(QImage(), QRect());
        emit stateChanged();
        if (hadMvc)
            emit mvcChanged();
    }, Qt::QueuedConnection);
}

// ---------------------------------------------------------------------------
// Overlay (Menü, im 3D-Modus auch Untertitel) – je Auge platziert
// ---------------------------------------------------------------------------

void BlurayNav::onOverlay(const QImage &img, const QRect &box)
{
    m_overlay = img;
    m_overlayBox = box;
    updateOverlay();
}

void BlurayNav::setOsdDimensions(const QVariantMap &dims)
{
    m_osd = dims;
    updateOverlay();
}

// Bildbereich(e) der Augen im Player-Fenster, abhängig vom 3D-Ausgabeformat
QList<BlurayNav::EyeRect> BlurayNav::eyeRects() const
{
    const double ow = m_osd.value("w").toDouble(), oh = m_osd.value("h").toDouble();
    const double ml = m_osd.value("ml").toDouble(), mt = m_osd.value("mt").toDouble();
    const double vw = ow - ml - m_osd.value("mr").toDouble();
    const double vh = oh - mt - m_osd.value("mb").toDouble();
    if (!m_mvcActive)
        return {{ml, mt, vw, vh}};
    if (m_layout.startsWith(QLatin1String("sbs")))
        return {{ml, mt, vw / 2, vh}, {ml + vw / 2, mt, vw / 2, vh}};
    if (m_layout.startsWith(QLatin1String("ab")))
        return {{ml, mt, vw, vh / 2}, {ml, mt + vh / 2, vw, vh / 2}};
    if (m_layout == QLatin1String("fp")) // 1920x2205: 1080 Zeilen, 45 Zeilen Lücke, 1080 Zeilen
        return {{ml, mt, vw, vh * 1080 / 2205}, {ml, mt + vh * 1125 / 2205, vw, vh * 1080 / 2205}};
    return {{ml, mt, vw, vh}};
}

void BlurayNav::updateOverlay()
{
    if (!m_mpv)
        return;
    const auto removeAll = [this] {
        for (const char *id : {"62", "63"}) {
            const char *args[] = {"overlay-remove", id, nullptr};
            mpv_command_async(m_mpv, 0, args);
        }
    };
    const int ow = m_osd.value("w").toInt(), oh = m_osd.value("h").toInt();
    if (m_overlay.isNull() || ow <= 0 || oh <= 0 || m_planeSize.isEmpty()) {
        removeAll();
        return;
    }

    const QList<EyeRect> eyes = eyeRects();
    const QByteArray addr = "&" + QByteArray::number(quintptr(m_overlay.constBits()));
    const QByteArray w = QByteArray::number(m_overlay.width());
    const QByteArray h = QByteArray::number(m_overlay.height());
    const QByteArray stride = QByteArray::number(m_overlay.bytesPerLine());
    for (int i = 0; i < 2; ++i) {
        const QByteArray id = QByteArray::number(63 - i);
        if (i >= eyes.size()) {
            const char *args[] = {"overlay-remove", id.constData(), nullptr};
            mpv_command_async(m_mpv, 0, args);
            continue;
        }
        const EyeRect &e = eyes[i];
        const double sx = e.w / m_planeSize.width(), sy = e.h / m_planeSize.height();
        // Tiefe: linkes Auge nach rechts, rechtes nach links verschieben = vor der Leinwand
        const double depth = eyes.size() > 1 ? (i == 0 ? m_subtitleDepth : -m_subtitleDepth) : 0;
        const QByteArray x = QByteArray::number(qRound(e.x + (m_overlayBox.x() + depth) * sx));
        const QByteArray y = QByteArray::number(qRound(e.y + m_overlayBox.y() * sy));
        const QByteArray dw = QByteArray::number(qMax(1, qRound(m_overlay.width() * sx)));
        const QByteArray dh = QByteArray::number(qMax(1, qRound(m_overlay.height() * sy)));
        const char *args[] = {"overlay-add", id.constData(), x.constData(), y.constData(), addr.constData(), "0", "bgra",
                              w.constData(), h.constData(), stride.constData(), dw.constData(), dh.constData(), nullptr};
        // Synchron: mpv kopiert die Pixel, bevor der Aufruf zurückkehrt
        mpv_command(m_mpv, args);
    }
}

bool BlurayNav::mapToPlane(double x, double y, int *px, int *py) const
{
    // Im 3D-Modus zählt das Auge, in dem der Mauszeiger steht
    for (const EyeRect &e : eyeRects()) {
        if (e.w <= 0 || e.h <= 0 || x < e.x || y < e.y || x >= e.x + e.w || y >= e.y + e.h)
            continue;
        *px = int((x - e.x) / e.w * m_planeSize.width());
        *py = int((y - e.y) / e.h * m_planeSize.height());
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Bedienung (GUI-Thread)
// ---------------------------------------------------------------------------

void BlurayNav::dropBuffers()
{
    if (!m_mpv)
        return;
    const char *args[] = {"drop-buffers", nullptr};
    mpv_command_async(m_mpv, 0, args);
}

bool BlurayNav::key(const QString &name)
{
#ifdef LUMEN_HAVE_BLURAY
    static const QHash<QString, uint32_t> keys = {
        {"up", BD_VK_UP}, {"down", BD_VK_DOWN}, {"left", BD_VK_LEFT}, {"right", BD_VK_RIGHT},
        {"enter", BD_VK_ENTER}, {"popup", BD_VK_POPUP}, {"menu", BD_VK_ROOT_MENU},
        {"0", BD_VK_0}, {"1", BD_VK_1}, {"2", BD_VK_2}, {"3", BD_VK_3}, {"4", BD_VK_4},
        {"5", BD_VK_5}, {"6", BD_VK_6}, {"7", BD_VK_7}, {"8", BD_VK_8}, {"9", BD_VK_9},
    };
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_session || !keys.contains(name) || !m_session->menu)
        return false;
    if (name == QLatin1String("menu")) {
        // Hauptmenü springt – alte Puffer verwerfen, damit das Menü sofort erscheint
        if (bd_menu_call(m_session->bd, -1) <= 0)
            bd_user_input(m_session->bd, -1, BD_VK_ROOT_MENU);
        dropBuffers();
        return true;
    }
    return bd_user_input(m_session->bd, -1, keys.value(name)) >= 0;
#else
    Q_UNUSED(name)
    return false;
#endif
}

int BlurayNav::discPid(const QString &type) const
{
    if (type == QLatin1String("video"))
        return m_videoPid;
    if (type == QLatin1String("audio"))
        return m_audioPid;
    return type == QLatin1String("sub") ? m_subPid : 0;
}

void BlurayNav::selectSubtitlePid(int pid)
{
#ifdef LUMEN_HAVE_BLURAY
    pid = m_pidSource.value(pid, pid); // die Disc kennt ihre eigene PID
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_session || !m_session->playlistInfo)
        return;
    const BLURAY_TITLE_INFO *ti = m_session->playlistInfo;
    const int item = std::min(m_session->playitem, int(ti->clip_count) - 1);
    if (pid <= 0 || item < 0) {
        bd_select_stream(m_session->bd, BLURAY_PG_TEXTST_STREAM, 1, 0);
        return;
    }
    const BLURAY_CLIP_INFO &c = ti->clips[item];
    for (int i = 0; i < c.pg_stream_count; ++i)
        if (c.pg_streams[i].pid == pid)
            bd_select_stream(m_session->bd, BLURAY_PG_TEXTST_STREAM, uint32_t(i + 1), 1);
#else
    Q_UNUSED(pid)
#endif
}

void BlurayNav::mouseMove(double x, double y)
{
#ifdef LUMEN_HAVE_BLURAY
    int px, py;
    if (!m_menuVisible || !mapToPlane(x, y, &px, &py))
        return;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_session)
        bd_mouse_select(m_session->bd, -1, uint16_t(px), uint16_t(py));
#else
    Q_UNUSED(x)
    Q_UNUSED(y)
#endif
}

void BlurayNav::mouseClick(double x, double y)
{
#ifdef LUMEN_HAVE_BLURAY
    int px, py;
    if (!m_menuVisible || !mapToPlane(x, y, &px, &py))
        return;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_session && bd_mouse_select(m_session->bd, -1, uint16_t(px), uint16_t(py)) > 0)
        bd_user_input(m_session->bd, -1, BD_VK_MOUSE_ACTIVATE);
#else
    Q_UNUSED(x)
    Q_UNUSED(y)
#endif
}

void BlurayNav::seek(double seconds)
{
#ifdef LUMEN_HAVE_BLURAY
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_session)
            return;
        m_session->userSeek = true;
        bd_seek_time(m_session->bd, uint64_t(std::max(0.0, seconds) * kTicks));
    }
    m_position = seconds;
    emit positionChanged();
    dropBuffers();
#else
    Q_UNUSED(seconds)
#endif
}

void BlurayNav::seekRelative(double delta)
{
    seek(std::clamp(m_position + delta, 0.0, m_duration > 0 ? m_duration - 1 : m_position + delta));
}

void BlurayNav::setChapter(int index)
{
#ifdef LUMEN_HAVE_BLURAY
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_session)
            return;
        m_session->userSeek = true;
        bd_seek_chapter(m_session->bd, unsigned(index));
    }
    dropBuffers();
#else
    Q_UNUSED(index)
#endif
}

void BlurayNav::nextChapter() { if (m_chapter + 1 < m_chapters.size()) setChapter(m_chapter + 1); }
void BlurayNav::prevChapter() { setChapter(std::max(0, m_chapter - 1)); }

void BlurayNav::pollPosition()
{
#ifdef LUMEN_HAVE_BLURAY
    double pos = 0;
    int chapter = -1;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_session)
            return;
        pos = bd_tell_time(m_session->bd) / kTicks;
        chapter = int(bd_get_current_chapter(m_session->bd)); // 0-basiert (Event: 1-basiert)
    }
    if (std::abs(pos - m_position) > 0.2 || chapter != m_chapter) {
        m_position = pos;
        if (chapter >= 0)
            m_chapter = chapter;
        emit positionChanged();
    }
#endif
}
