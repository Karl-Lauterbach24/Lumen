#include "DvdNav.h"

#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <QSet>
#include <QThread>

#include <mpv/client.h>
#include <mpv/stream_cb.h>

#ifdef LUMEN_HAVE_DVDNAV
#include <dvdnav/dvdnav.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>

namespace {
constexpr double kTicks = 90000.0;
constexpr int kBlock = 2048;
constexpr const char *kOverlayId = "60";

quint16 rb16(const uchar *p) { return quint16((p[0] << 8) | p[1]); }

// YCrCb (DVD-Palette, 0x00YYCrCb) -> RGB (BT.601, begrenzter Bereich)
QRgb ycrcbToRgb(quint32 c)
{
    const double y = ((c >> 16) & 0xff) - 16.0;
    const double cr = ((c >> 8) & 0xff) - 128.0;
    const double cb = (c & 0xff) - 128.0;
    auto clamp = [](double v) { return int(std::clamp(v, 0.0, 255.0)); };
    return qRgb(clamp(1.164 * y + 1.596 * cr), clamp(1.164 * y - 0.813 * cr - 0.391 * cb), clamp(1.164 * y + 2.018 * cb));
}

QString langName(quint16 code)
{
    if (code == 0 || code == 0xffff)
        return QStringLiteral("—");
    return (QString(QChar(code >> 8)) + QChar(code & 0xff)).toUpper();
}
} // namespace

// ---------------------------------------------------------------------------
// Subpicture (SPU): Lauflängen-kodiertes 2-Bit-Bild + Steuersequenzen
// ---------------------------------------------------------------------------
struct DvdNav::Spu
{
    qint64 pts = -1;       // 90 kHz, -1 = unbekannt
    bool menu = false;     // im Menü dekodiert -> sofort zeigen
    double start = 0;      // Sekunden ab PTS
    double stop = -1;
    bool forced = false;
    bool display = false;
    QRect rect;
    QByteArray pixels;     // rect.width() * rect.height(), Werte 0..3
    uchar color[4] = {0, 1, 2, 3};
    uchar alpha[4] = {0, 15, 15, 15};

    static std::shared_ptr<Spu> decode(const QByteArray &pkt, qint64 pts, bool menu)
    {
        const auto *d = reinterpret_cast<const uchar *>(pkt.constData());
        const int size = int(pkt.size());
        if (size < 4)
            return {};
        const int total = rb16(d);
        const int dcsq = rb16(d + 2);
        if (total > size || dcsq >= total || dcsq < 4)
            return {};
        auto spu = std::make_shared<Spu>();
        spu->pts = pts;
        spu->menu = menu;
        int x1 = 0, x2 = -1, y1 = 0, y2 = -1, top = -1, bottom = -1;
        int off = dcsq, prev = -1;
        bool started = false;
        while (off + 4 <= total && off != prev) {
            const double delay = rb16(d + off) * 1024.0 / kTicks;
            const int next = rb16(d + off + 2);
            int p = off + 4;
            bool end = false;
            while (p < total && !end) {
                const uchar cmd = d[p++];
                switch (cmd) {
                case 0x00: spu->forced = true; [[fallthrough]];
                case 0x01:
                    if (!started) {
                        spu->start = delay;
                        started = true;
                    }
                    spu->display = true;
                    break;
                case 0x02: spu->stop = delay; break;
                case 0x03:
                    if (p + 2 > total) return {};
                    for (int i = 0; i < 4; ++i)
                        spu->color[i] = uchar((rb16(d + p) >> (4 * i)) & 0xf);
                    p += 2;
                    break;
                case 0x04:
                    if (p + 2 > total) return {};
                    for (int i = 0; i < 4; ++i)
                        spu->alpha[i] = uchar((rb16(d + p) >> (4 * i)) & 0xf);
                    p += 2;
                    break;
                case 0x05:
                    if (p + 6 > total) return {};
                    x1 = (d[p] << 4) | (d[p + 1] >> 4);
                    x2 = ((d[p + 1] & 0xf) << 8) | d[p + 2];
                    y1 = (d[p + 3] << 4) | (d[p + 4] >> 4);
                    y2 = ((d[p + 4] & 0xf) << 8) | d[p + 5];
                    p += 6;
                    break;
                case 0x06:
                    if (p + 4 > total) return {};
                    top = rb16(d + p);
                    bottom = rb16(d + p + 2);
                    p += 4;
                    break;
                case 0x07: // CHG_COLCON: Länge überspringen
                    if (p + 2 > total) return {};
                    p += rb16(d + p);
                    break;
                case 0xff: end = true; break;
                default: end = true; break;
                }
            }
            prev = off;
            off = next;
        }
        const int w = x2 - x1 + 1, h = y2 - y1 + 1;
        if (w <= 0 || h <= 0 || w > 1920 || h > 1152 || top < 4 || bottom < 4)
            return spu->display ? spu : std::shared_ptr<Spu>();
        spu->rect = QRect(x1, y1, w, h);
        spu->pixels = QByteArray(w * h, '\0');
        auto *px = reinterpret_cast<uchar *>(spu->pixels.data());
        const int limit = dcsq * 2; // Nibble-Grenze: Bilddaten enden vor der Steuersequenz
        for (int field = 0; field < 2; ++field) {
            int nib = (field ? bottom : top) * 2;
            auto get = [&]() -> int {
                if (nib >= limit)
                    return 0;
                const int v = (d[nib >> 1] >> ((nib & 1) ? 0 : 4)) & 0xf;
                ++nib;
                return v;
            };
            for (int y = field; y < h; y += 2) {
                int x = 0;
                while (x < w) {
                    int v = get();
                    if (v < 0x4) {
                        v = (v << 4) | get();
                        if (v < 0x10) {
                            v = (v << 4) | get();
                            if (v < 0x40)
                                v = (v << 4) | get();
                        }
                    }
                    int run = v >> 2;
                    if (run == 0)
                        run = w - x;
                    run = std::min(run, w - x);
                    std::memset(px + y * w + x, v & 3, size_t(run));
                    x += run;
                    if (nib >= limit)
                        break;
                }
                if (nib & 1)
                    ++nib;
            }
        }
        return spu;
    }
};

// ---------------------------------------------------------------------------
// Session: lebt im mpv-Demux-Thread
// ---------------------------------------------------------------------------
struct DvdNav::Session
{
    DvdNav *nav = nullptr;
    std::atomic_bool cancel{false};
    QByteArray out;
    int outPos = 0;
    uchar block[kBlock];
    QByteArray packHeader;       // letzter Pack-Header (für eingefügte Pakete)
    bool still = false;
    bool stillFlushed = false;
    QDeadlineTimer stillUntil{QDeadlineTimer::Forever};
    QDeadlineTimer nextPad;
    int lastTitle = -1;
    bool menuDomain = false;
    std::atomic_int spuStream{-1}; // physische SPU-Nummer, die gesammelt wird
    QByteArray spuBuf;
    int spuExpected = 0;
    qint64 spuPts = -1;
#ifdef LUMEN_HAVE_DVDNAV
    dvdnav_t *dvd = nullptr;

    ~Session()
    {
        if (dvd)
            dvdnav_close(dvd);
    }

    void post(std::function<void()> fn) { QMetaObject::invokeMethod(nav, std::move(fn), Qt::QueuedConnection); }

    // Pack durchgehen: Subpicture-PES einsammeln und durch Füll-PES ersetzen
    void processBlock(uchar *buf, int len)
    {
        int p = 0;
        if (len >= 14 && buf[0] == 0 && buf[1] == 0 && buf[2] == 1 && buf[3] == 0xba) {
            const int hdr = (buf[4] & 0xc0) == 0x40 ? 14 + (buf[13] & 7) : 12;
            packHeader = QByteArray(reinterpret_cast<const char *>(buf), std::min(hdr, 14));
            p = hdr;
        }
        while (p + 6 <= len) {
            if (buf[p] != 0 || buf[p + 1] != 0 || buf[p + 2] != 1)
                break;
            const int id = buf[p + 3];
            const int plen = rb16(buf + p + 4);
            const int end = p + 6 + plen;
            if (end > len)
                break;
            if (id == 0xbd && p + 9 <= end) {
                const int hlen = buf[p + 8];
                const int payload = p + 9 + hlen;
                if (payload < end && (buf[payload] & 0xe0) == 0x20) {
                    const int stream = buf[payload] & 0x1f;
                    qint64 pts = -1;
                    if ((buf[p + 7] & 0x80) && hlen >= 5)
                        pts = (qint64((buf[p + 9] >> 1) & 7) << 30) | (qint64(rb16(buf + p + 10) >> 1) << 15) | (rb16(buf + p + 12) >> 1);
                    if (stream == spuStream)
                        collectSpu(buf + payload + 1, end - payload - 1, pts);
                    buf[p + 3] = 0xbe; // -> Padding-Stream, mpv ignoriert den Inhalt
                }
            }
            p = end;
        }
    }

    void collectSpu(const uchar *data, int n, qint64 pts)
    {
        if (pts >= 0 || spuBuf.isEmpty()) {
            // Beginn eines neuen SPU-Pakets
            if (n < 2)
                return;
            spuBuf = QByteArray(reinterpret_cast<const char *>(data), n);
            spuExpected = rb16(data);
            spuPts = pts;
        } else {
            spuBuf.append(reinterpret_cast<const char *>(data), n);
        }
        if (spuExpected > 0 && spuBuf.size() >= spuExpected) {
            auto spu = Spu::decode(spuBuf, spuPts, menuDomain);
            spuBuf.clear();
            spuExpected = 0;
            if (spu)
                post([n = nav, spu] { n->onSpu(spu); });
        }
    }

    // Standbild: Sequenzende einfügen, damit der Decoder das letzte Bild ausgibt
    void appendSequenceEnd()
    {
        static const uchar pes[] = {0, 0, 1, 0xe0, 0, 7, 0x80, 0, 0, 0, 0, 1, 0xb7};
        if (!packHeader.isEmpty())
            out.append(packHeader);
        out.append(reinterpret_cast<const char *>(pes), sizeof(pes));
    }

    // Während eines Standbilds regelmäßig Füllpakete liefern: mpv's Demuxer
    // blockiert sonst beim Einlesen (Stream-Analyse) bis zur nächsten Eingabe
    void appendPadding()
    {
        QByteArray pad(kBlock, '\0');
        int p = 0;
        if (!packHeader.isEmpty()) {
            std::memcpy(pad.data(), packHeader.constData(), size_t(packHeader.size()));
            p = int(packHeader.size());
        }
        auto *b = reinterpret_cast<uchar *>(pad.data());
        const int len = kBlock - p - 6;
        b[p] = 0; b[p + 1] = 0; b[p + 2] = 1; b[p + 3] = 0xbe;
        b[p + 4] = uchar(len >> 8);
        b[p + 5] = uchar(len & 0xff);
        std::memset(b + p + 6, 0xff, size_t(len));
        out.append(pad);
    }

    void chapterInfo(int title)
    {
        uint64_t *times = nullptr;
        uint64_t duration = 0;
        const uint32_t n = dvdnav_describe_title_chapters(dvd, title, &times, &duration);
        QVariantList chapters;
        for (uint32_t i = 0; i < n; ++i)
            chapters.append(QVariantMap{{"index", int(i)}, {"title", QStringLiteral("Kapitel %1").arg(i + 1)},
                                        {"time", i == 0 ? 0.0 : double(times[i - 1]) / kTicks}});
        if (times)
            std::free(times);
        post([nv = nav, chapters, d = duration / kTicks, title] {
            nv->m_title = title;
            nv->m_chapters = chapters;
            nv->m_duration = d;
            emit nv->stateChanged();
        });
    }

    void handleEvent(int ev, uchar *buf)
    {
        DvdNav *n = nav;
        switch (ev) {
        case DVDNAV_STILL_FRAME: {
            const auto *e = reinterpret_cast<dvdnav_still_event_t *>(buf);
            if (!still) {
                still = true;
                stillFlushed = false;
                stillUntil = e->length < 0xff ? QDeadlineTimer(qint64(e->length) * 1000) : QDeadlineTimer(QDeadlineTimer::Forever);
                post([n] { n->m_still = true; emit n->stateChanged(); });
            }
            if (!stillFlushed) {
                stillFlushed = true;
                appendSequenceEnd();
            }
            if (stillUntil.hasExpired()) {
                dvdnav_still_skip(dvd);
                still = false;
                post([n] { n->m_still = false; emit n->stateChanged(); });
            }
            break;
        }
        case DVDNAV_WAIT:
            dvdnav_wait_skip(dvd);
            break;
        case DVDNAV_SPU_CLUT_CHANGE: {
            quint32 clut[16];
            std::memcpy(clut, buf, sizeof(clut));
            post([n, c = QByteArray(reinterpret_cast<const char *>(clut), sizeof(clut))] {
                std::memcpy(n->m_clut, c.constData(), sizeof(n->m_clut));
                n->render();
            });
            break;
        }
        case DVDNAV_SPU_STREAM_CHANGE: {
            const auto *e = reinterpret_cast<dvdnav_spu_stream_change_event_t *>(buf);
            spuStream = e->physical_wide & 0x1f;
            spuBuf.clear();
            post([n] { n->refreshStreams(); });
            break;
        }
        case DVDNAV_AUDIO_STREAM_CHANGE: {
            const auto *e = reinterpret_cast<dvdnav_audio_stream_change_event_t *>(buf);
            if (e->physical >= 0 && e->logical >= 0) {
                audio_attr_t attr{};
                int id = 0x80 + e->physical;
                if (dvdnav_get_audio_attr(dvd, uint8_t(e->logical), &attr) == DVDNAV_STATUS_OK) {
                    switch (attr.audio_format) {
                    case 2: case 3: id = 0x1c0 + e->physical; break;
                    case 4: id = 0xa0 + e->physical; break;
                    case 6: id = 0x88 + e->physical; break;
                    default: break;
                    }
                }
                post([n, id] { emit n->audioPidSelected(id); });
            }
            break;
        }
        case DVDNAV_VTS_CHANGE: {
            const auto *e = reinterpret_cast<dvdnav_vts_change_event_t *>(buf);
            menuDomain = e->new_domain != DVD_DOMAIN_VTSTitle;
            spuBuf.clear();
            uint32_t w = 720, h = 576;
            dvdnav_get_video_resolution(dvd, &w, &h);
            post([n, md = menuDomain, sz = QSize(int(w), int(h))] {
                n->m_menuDomain = md;
                if (sz.isValid() && sz.width() > 0)
                    n->m_videoSize = sz;
                n->m_queue.clear();
                n->m_shown.reset();
                n->refreshStreams();
                n->render();
                emit n->stateChanged();
            });
            break;
        }
        case DVDNAV_CELL_CHANGE: {
            int32_t title = 0, part = 0;
            if (dvdnav_current_title_info(dvd, &title, &part) == DVDNAV_STATUS_OK) {
                if (title > 0 && title != lastTitle) {
                    lastTitle = title;
                    chapterInfo(title);
                }
                post([n, part] { n->m_chapter = part - 1; emit n->positionChanged(); });
            }
            if (menuDomain != !(dvdnav_is_domain_vts(dvd))) {
                menuDomain = !dvdnav_is_domain_vts(dvd);
                post([n, md = menuDomain] { n->m_menuDomain = md; emit n->stateChanged(); });
            }
            break;
        }
        case DVDNAV_NAV_PACKET:
        case DVDNAV_HIGHLIGHT:
            post([n] { n->updateHighlight(); });
            break;
        case DVDNAV_HOP_CHANNEL:
            spuBuf.clear();
            break;
        default:
            break;
        }
    }

    int64_t read(char *dst, uint64_t size)
    {
        while (!cancel) {
            if (outPos < out.size()) {
                const int n = int(std::min<uint64_t>(size, uint64_t(out.size() - outPos)));
                std::memcpy(dst, out.constData() + outPos, size_t(n));
                outPos += n;
                if (outPos >= out.size()) {
                    out.clear();
                    outPos = 0;
                }
                return n;
            }
            int32_t ev = 0, len = 0;
            uchar *buf = block;
            if (dvdnav_get_next_block(dvd, buf, &ev, &len) != DVDNAV_STATUS_OK) {
                const QString err = QString::fromUtf8(dvdnav_err_to_string(dvd));
                post([n = nav, err] { n->setStatus(QStringLiteral("DVD-Lesefehler: ") + err); });
                return -1;
            }
            if (ev == DVDNAV_BLOCK_OK) {
                if (still) {
                    still = false;
                    post([n = nav] { n->m_still = false; emit n->stateChanged(); });
                }
                processBlock(buf, len);
                out.append(reinterpret_cast<const char *>(buf), len);
                continue;
            }
            if (ev == DVDNAV_STOP)
                return 0;
            handleEvent(ev, buf);
            if (ev == DVDNAV_STILL_FRAME && out.isEmpty()) {
                if (!nextPad.isForever() && nextPad.hasExpired()) {
                    appendPadding();
                    nextPad = QDeadlineTimer(60);
                } else {
                    if (nextPad.isForever())
                        nextPad = QDeadlineTimer(60);
                    QThread::msleep(15);
                }
            }
        }
        return -1;
    }
#endif
};

// ---------------------------------------------------------------------------

DvdNav::DvdNav(QObject *parent)
    : QObject(parent)
{
    m_poll.setInterval(250);
    connect(&m_poll, &QTimer::timeout, this, &DvdNav::pollPosition);
    m_clock.setInterval(20);
    connect(&m_clock, &QTimer::timeout, this, &DvdNav::tick);
}

DvdNav::~DvdNav()
{
    detach();
}

bool DvdNav::available()
{
#ifdef LUMEN_HAVE_DVDNAV
    return true;
#else
    return false;
#endif
}

bool DvdNav::isDvd(const QString &path)
{
    const QFileInfo fi(path);
    if (fi.isDir())
        return QFileInfo::exists(QDir(path).filePath(QStringLiteral("VIDEO_TS/VIDEO_TS.IFO")))
               || QFileInfo::exists(QDir(path).filePath(QStringLiteral("VIDEO_TS.IFO")));
    return fi.fileName().compare(QLatin1String("VIDEO_TS.IFO"), Qt::CaseInsensitive) == 0;
}

QVariantMap DvdNav::scan(const QString &device)
{
    QVariantMap m;
    m["device"] = device;
    m["kind"] = QStringLiteral("dvd");
#ifdef LUMEN_HAVE_DVDNAV
    dvdnav_t *dvd = nullptr;
    if (dvdnav_open(&dvd, QDir::toNativeSeparators(device).toUtf8().constData()) != DVDNAV_STATUS_OK || !dvd) {
        m["error"] = QStringLiteral("libdvdnav konnte die DVD nicht öffnen");
        return m;
    }
    const char *name = nullptr;
    if (dvdnav_get_title_string(dvd, &name) == DVDNAV_STATUS_OK && name)
        m["discName"] = QString::fromUtf8(name).trimmed();
    int32_t count = 0;
    dvdnav_get_number_of_titles(dvd, &count);
    QVariantList titles;
    double longest = 0;
    int mainIndex = -1;
    for (int t = 1; t <= count; ++t) {
        uint64_t *times = nullptr;
        uint64_t duration = 0;
        const uint32_t chapters = dvdnav_describe_title_chapters(dvd, t, &times, &duration);
        if (times)
            std::free(times);
        const double secs = duration / kTicks;
        if (secs > longest) {
            longest = secs;
            mainIndex = int(titles.size());
        }
        titles.append(QVariantMap{{"index", t - 1}, {"title", t}, {"duration", secs}, {"chapters", int(chapters)}});
    }
    if (mainIndex >= 0) {
        QVariantMap mt = titles[mainIndex].toMap();
        mt["main"] = true;
        titles[mainIndex] = mt;
    }
    int32_t region = 0;
    if (dvdnav_get_disk_region_mask(dvd, &region) == DVDNAV_STATUS_OK) {
        QStringList regions;
        for (int r = 0; r < 8; ++r)
            if (region & (1 << r))
                regions << QString::number(r + 1);
        m["regions"] = regions.join(QLatin1Char(','));
    }
    m["titles"] = titles;
    dvdnav_close(dvd);
#else
    m["error"] = QStringLiteral("Ohne libdvdnav gebaut");
#endif
    return m;
}

void DvdNav::attach(mpv_handle *mpv)
{
    m_mpv = mpv;
#ifdef LUMEN_HAVE_DVDNAV
    mpv_stream_cb_add_ro(mpv, "lumendvd", this, [](void *ud, char *uri, mpv_stream_cb_info *info) {
        return DvdNav::openStream(ud, uri, info);
    });
#endif
}

void DvdNav::detach()
{
    m_mpv = nullptr;
    m_poll.stop();
    m_clock.stop();
    m_overlay = QImage();
    if (m_active) {
        m_active = false;
        m_menuVisible = false;
        emit stateChanged();
    }
}

QString DvdNav::prepare(const QString &device, const QString &mode, int title)
{
    m_device = device;
    m_mode = mode;
    m_requestedTitle = title;
    return mode == QLatin1String("title") ? QStringLiteral("lumendvd://title/%1").arg(title) : QStringLiteral("lumendvd://") + mode;
}

void DvdNav::setStatus(const QString &s)
{
    if (s == m_status)
        return;
    m_status = s;
    emit statusChanged();
}

int DvdNav::openStream(void *userData, char *, void *infoPtr)
{
#ifdef LUMEN_HAVE_DVDNAV
    auto *nav = static_cast<DvdNav *>(userData);
    auto *info = static_cast<mpv_stream_cb_info *>(infoPtr);
    auto post = [nav](auto fn) { QMetaObject::invokeMethod(nav, fn, Qt::QueuedConnection); };
    auto fail = [&](Session *s, const QString &msg) {
        post([nav, msg] { nav->setStatus(msg); });
        delete s;
        return int(MPV_ERROR_LOADING_FAILED);
    };

    auto *s = new Session;
    s->nav = nav;
    if (dvdnav_open(&s->dvd, QDir::toNativeSeparators(nav->m_device).toUtf8().constData()) != DVDNAV_STATUS_OK || !s->dvd)
        return fail(s, QStringLiteral("libdvdnav konnte die DVD nicht öffnen (bei CSS-geschützten Discs muss das System den Zugriff bereitstellen)"));

    dvdnav_set_readahead_flag(s->dvd, 1);
    dvdnav_set_PGC_positioning_flag(s->dvd, 1);
    // Sprache und Region wie ein Hardware-Player aus den Systemeinstellungen
    const QLocale loc;
    const QByteArray lang = QLocale::languageToCode(loc.language(), QLocale::ISO639Part1).toLatin1();
    if (lang.size() == 2) {
        dvdnav_menu_language_select(s->dvd, const_cast<char *>(lang.constData()));
        dvdnav_audio_language_select(s->dvd, const_cast<char *>(lang.constData()));
        dvdnav_spu_language_select(s->dvd, const_cast<char *>(lang.constData()));
    }
    const QByteArray country = QLocale::territoryToCode(loc.territory()).toLower().toLatin1();
    static const QSet<QByteArray> r1 = {"us", "ca", "bm"};
    static const QSet<QByteArray> r3 = {"kr", "tw", "hk", "id", "th", "ph", "sg", "my", "vn", "mo"};
    static const QSet<QByteArray> r4 = {"au", "nz", "mx", "br", "ar", "cl", "co", "pe", "ve", "uy", "py", "ec", "bo"};
    static const QSet<QByteArray> r5 = {"ru", "ua", "by", "kz", "in", "pk", "bd", "ng", "ke", "mn", "kp"};
    static const QSet<QByteArray> r6 = {"cn"};
    const int region = r1.contains(country) ? 1 : r3.contains(country) ? 3 : r4.contains(country) ? 4
                     : r5.contains(country) ? 5 : r6.contains(country) ? 6 : 2;
    dvdnav_set_region_mask(s->dvd, 1 << (region - 1));

    int32_t titles = 0;
    dvdnav_get_number_of_titles(s->dvd, &titles);
    const char *name = nullptr;
    QString discTitle;
    if (dvdnav_get_title_string(s->dvd, &name) == DVDNAV_STATUS_OK && name)
        discTitle = QString::fromUtf8(name).trimmed();

    if (nav->m_mode != QLatin1String("menu")) {
        int title = nav->m_requestedTitle;
        if (nav->m_mode == QLatin1String("main") || title < 1) {
            uint64_t longest = 0;
            for (int t = 1; t <= titles; ++t) {
                uint64_t *times = nullptr;
                uint64_t d = 0;
                dvdnav_describe_title_chapters(s->dvd, t, &times, &d);
                if (times)
                    std::free(times);
                if (d > longest) {
                    longest = d;
                    title = t;
                }
            }
        }
        if (title < 1 || dvdnav_title_play(s->dvd, title) != DVDNAV_STATUS_OK)
            return fail(s, QStringLiteral("DVD-Titel konnte nicht gestartet werden"));
    }

    {
        std::lock_guard<std::mutex> lock(nav->m_mutex);
        nav->m_session = s;
    }
    post([nav, titles, discTitle] {
        nav->m_active = true;
        nav->m_titleCount = titles;
        nav->m_discTitle = discTitle;
        nav->m_position = 0;
        nav->m_queue.clear();
        nav->m_shown.reset();
        nav->m_hl = Highlight();
        nav->m_poll.start();
        nav->m_clock.start();
        nav->setStatus(nav->m_mode == QLatin1String("menu") ? QStringLiteral("DVD-Menü aktiv") : QStringLiteral("DVD-Titel über libdvdnav"));
        emit nav->stateChanged();
    });

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

void DvdNav::closeSession(Session *s)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_session == s)
            m_session = nullptr;
    }
    delete s;
    QMetaObject::invokeMethod(this, [this] {
        m_poll.stop();
        m_clock.stop();
        m_active = false;
        m_menuVisible = false;
        m_still = false;
        m_chapters.clear();
        m_queue.clear();
        m_shown.reset();
        m_hl = Highlight();
        render();
        emit stateChanged();
    }, Qt::QueuedConnection);
}

void DvdNav::dropBuffers()
{
    m_queue.clear();
    m_shown.reset();
    render();
    if (!m_mpv)
        return;
    const char *args[] = {"drop-buffers", nullptr};
    mpv_command_async(m_mpv, 0, args);
}

// ---------------------------------------------------------------------------
// Streams, Position
// ---------------------------------------------------------------------------

void DvdNav::refreshStreams()
{
#ifdef LUMEN_HAVE_DVDNAV
    QVariantList audio, subs;
    int active = -1, angle = 1, angles = 1;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_session)
            return;
        dvdnav_t *dvd = m_session->dvd;
        for (int phys = 0; phys < 8; ++phys) {
            const int logical = dvdnav_get_audio_logical_stream(dvd, uint8_t(phys));
            if (logical < 0)
                continue;
            audio_attr_t attr{};
            if (dvdnav_get_audio_attr(dvd, uint8_t(logical), &attr) != DVDNAV_STATUS_OK)
                continue;
            int id = 0x80 + phys;
            QString fmt = QStringLiteral("AC-3");
            switch (attr.audio_format) {
            case 2: case 3: id = 0x1c0 + phys; fmt = QStringLiteral("MPEG"); break;
            case 4: id = 0xa0 + phys; fmt = QStringLiteral("LPCM"); break;
            case 6: id = 0x88 + phys; fmt = QStringLiteral("DTS"); break;
            default: break;
            }
            const int ch = attr.channels + 1;
            QString label = langName(dvdnav_audio_stream_to_lang(dvd, uint8_t(logical))) + QStringLiteral(" · ") + fmt
                            + QStringLiteral(" · ") + (ch == 6 ? QStringLiteral("5.1") : ch == 2 ? QStringLiteral("2.0") : QStringLiteral("%1 ch").arg(ch));
            if (attr.code_extension == 2 || attr.code_extension == 3)
                label += QStringLiteral(" · Audiodeskription");
            else if (attr.code_extension == 4)
                label += QStringLiteral(" · Kommentar");
            audio.append(QVariantMap{{"srcId", id}, {"logical", logical}, {"label", label}});
        }
        for (int phys = 0; phys < 32; ++phys) {
            const int logical = dvdnav_get_spu_logical_stream(dvd, uint8_t(phys));
            if (logical < 0)
                continue;
            subp_attr_t attr{};
            dvdnav_get_spu_attr(dvd, uint8_t(logical), &attr);
            QString label = langName(dvdnav_spu_stream_to_lang(dvd, uint8_t(logical)));
            if (attr.code_extension == 9)
                label += QStringLiteral(" · erzwungen");
            else if (attr.code_extension == 13 || attr.code_extension == 14 || attr.code_extension == 15)
                label += QStringLiteral(" · Kommentar");
            else if (attr.code_extension >= 5 && attr.code_extension <= 7)
                label += QStringLiteral(" · Hörgeschädigte");
            subs.append(QVariantMap{{"id", phys}, {"logical", logical}, {"label", label}});
        }
        const int8_t a = dvdnav_get_active_spu_stream(dvd);
        active = (a < 0 || (a & 0x80)) ? -1 : (a & 0x1f);
        int32_t cur = 1, num = 1;
        if (dvdnav_get_angle_info(dvd, &cur, &num) == DVDNAV_STATUS_OK) {
            angle = cur;
            angles = num;
        }
    }
    m_audioStreams = audio;
    m_subStreams = subs;
    if (!m_menuDomain)
        m_userSpu = active;
    m_angle = angle;
    m_angles = angles;
    emit streamsChanged();
#endif
}

QString DvdNav::audioLabel(int srcId) const
{
    for (const auto &v : m_audioStreams) {
        const QVariantMap m = v.toMap();
        if (m.value("srcId").toInt() == srcId)
            return m.value("label").toString();
    }
    return {};
}

void DvdNav::pollPosition()
{
#ifdef LUMEN_HAVE_DVDNAV
    double pos = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_session)
            return;
        pos = dvdnav_get_current_time(m_session->dvd) / kTicks;
    }
    if (std::abs(pos - m_position) > 0.2) {
        m_position = pos;
        emit positionChanged();
    }
#endif
}

// ---------------------------------------------------------------------------
// Bedienung
// ---------------------------------------------------------------------------

bool DvdNav::key(const QString &name)
{
#ifdef LUMEN_HAVE_DVDNAV
    bool ok = false;
    bool jump = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_session)
            return false;
        dvdnav_t *dvd = m_session->dvd;
        pci_t *pci = dvdnav_get_current_nav_pci(dvd);
        const bool buttons = pci && (pci->hli.hl_gi.hli_ss & 3) && pci->hli.hl_gi.btn_ns > 0;
        if (name == QLatin1String("menu")) {
            ok = dvdnav_menu_call(dvd, DVD_MENU_Root) == DVDNAV_STATUS_OK || dvdnav_menu_call(dvd, DVD_MENU_Title) == DVDNAV_STATUS_OK;
            jump = ok;
        } else if (name == QLatin1String("popup")) {
            ok = dvdnav_menu_call(dvd, DVD_MENU_Title) == DVDNAV_STATUS_OK;
            jump = ok;
        } else if (name == QLatin1String("audio")) {
            ok = jump = dvdnav_menu_call(dvd, DVD_MENU_Audio) == DVDNAV_STATUS_OK;
        } else if (name == QLatin1String("subtitle")) {
            ok = jump = dvdnav_menu_call(dvd, DVD_MENU_Subpicture) == DVDNAV_STATUS_OK;
        } else if (name == QLatin1String("back")) {
            ok = jump = dvdnav_go_up(dvd) == DVDNAV_STATUS_OK;
        } else if (buttons) {
            if (name == QLatin1String("up"))
                ok = dvdnav_upper_button_select(dvd, pci) == DVDNAV_STATUS_OK;
            else if (name == QLatin1String("down"))
                ok = dvdnav_lower_button_select(dvd, pci) == DVDNAV_STATUS_OK;
            else if (name == QLatin1String("left"))
                ok = dvdnav_left_button_select(dvd, pci) == DVDNAV_STATUS_OK;
            else if (name == QLatin1String("right"))
                ok = dvdnav_right_button_select(dvd, pci) == DVDNAV_STATUS_OK;
            else if (name == QLatin1String("enter"))
                ok = jump = dvdnav_button_activate(dvd, pci) == DVDNAV_STATUS_OK;
            else if (name.size() == 1 && name.at(0).isDigit() && name != QLatin1String("0")) {
                ok = dvdnav_button_select(dvd, pci, name.toInt()) == DVDNAV_STATUS_OK;
                if (ok)
                    jump = dvdnav_button_activate(dvd, pci) == DVDNAV_STATUS_OK;
            }
        }
    }
    if (jump)
        dropBuffers();
    updateHighlight();
    return ok;
#else
    Q_UNUSED(name)
    return false;
#endif
}

void DvdNav::seek(double seconds)
{
#ifdef LUMEN_HAVE_DVDNAV
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_session || m_menuDomain)
            return;
        const uint64_t t = uint64_t(std::max(0.0, seconds) * kTicks);
        if (dvdnav_jump_to_sector_by_time(m_session->dvd, t, SEEK_SET) != DVDNAV_STATUS_OK)
            dvdnav_time_search(m_session->dvd, t);
    }
    m_position = seconds;
    emit positionChanged();
    dropBuffers();
#else
    Q_UNUSED(seconds)
#endif
}

void DvdNav::seekRelative(double delta)
{
    seek(std::clamp(m_position + delta, 0.0, m_duration > 0 ? m_duration - 1 : m_position + delta));
}

void DvdNav::setChapter(int index)
{
#ifdef LUMEN_HAVE_DVDNAV
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_session || m_title < 1)
            return;
        dvdnav_part_play(m_session->dvd, m_title, index + 1);
    }
    dropBuffers();
#else
    Q_UNUSED(index)
#endif
}

void DvdNav::nextChapter()
{
#ifdef LUMEN_HAVE_DVDNAV
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_session)
            return;
        dvdnav_next_pg_search(m_session->dvd);
    }
    dropBuffers();
#endif
}

void DvdNav::prevChapter()
{
#ifdef LUMEN_HAVE_DVDNAV
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_session)
            return;
        dvdnav_prev_pg_search(m_session->dvd);
    }
    dropBuffers();
#endif
}

void DvdNav::playTitle(int title)
{
#ifdef LUMEN_HAVE_DVDNAV
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_session)
            return;
        dvdnav_title_play(m_session->dvd, title);
    }
    dropBuffers();
#else
    Q_UNUSED(title)
#endif
}

void DvdNav::selectSubtitle(int physical)
{
#ifdef LUMEN_HAVE_DVDNAV
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_session)
            return;
        if (physical >= 0) {
            dvdnav_set_active_stream(m_session->dvd, uint8_t(physical), DVD_SUBTITLE_STREAM);
            m_session->spuStream = physical;
        }
        dvdnav_toggle_spu_stream(m_session->dvd, physical >= 0 ? 1 : 0);
    }
    m_userSpu = physical;
    m_queue.clear();
    m_shown.reset();
    render();
    emit streamsChanged();
#else
    Q_UNUSED(physical)
#endif
}

void DvdNav::noteAudioSelected(int srcId)
{
#ifdef LUMEN_HAVE_DVDNAV
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_session)
        return;
    const int phys = srcId >= 0x1c0 ? srcId - 0x1c0 : (srcId & 0x07);
    dvdnav_set_active_stream(m_session->dvd, uint8_t(phys), DVD_AUDIO_STREAM);
#else
    Q_UNUSED(srcId)
#endif
}

void DvdNav::setAngle(int angle)
{
#ifdef LUMEN_HAVE_DVDNAV
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_session)
            return;
        dvdnav_angle_change(m_session->dvd, angle);
    }
    m_angle = angle;
    emit streamsChanged();
#else
    Q_UNUSED(angle)
#endif
}

bool DvdNav::mapToVideo(double x, double y, int *px, int *py) const
{
    const double ow = m_osd.value("w").toDouble(), oh = m_osd.value("h").toDouble();
    const double ml = m_osd.value("ml").toDouble(), mt = m_osd.value("mt").toDouble();
    const double vw = ow - ml - m_osd.value("mr").toDouble();
    const double vh = oh - mt - m_osd.value("mb").toDouble();
    if (vw <= 0 || vh <= 0 || x < ml || y < mt || x >= ml + vw || y >= mt + vh)
        return false;
    *px = int((x - ml) / vw * m_videoSize.width());
    *py = int((y - mt) / vh * m_videoSize.height());
    return true;
}

void DvdNav::mouseMove(double x, double y)
{
#ifdef LUMEN_HAVE_DVDNAV
    int px, py;
    if (!mapToVideo(x, y, &px, &py))
        return;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_session)
            return;
        pci_t *pci = dvdnav_get_current_nav_pci(m_session->dvd);
        if (!pci || dvdnav_mouse_select(m_session->dvd, pci, px, py) != DVDNAV_STATUS_OK)
            return;
    }
    updateHighlight();
#else
    Q_UNUSED(x)
    Q_UNUSED(y)
#endif
}

void DvdNav::mouseClick(double x, double y)
{
#ifdef LUMEN_HAVE_DVDNAV
    int px, py;
    if (!mapToVideo(x, y, &px, &py))
        return;
    bool ok = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_session)
            return;
        pci_t *pci = dvdnav_get_current_nav_pci(m_session->dvd);
        ok = pci && dvdnav_mouse_activate(m_session->dvd, pci, px, py) == DVDNAV_STATUS_OK;
    }
    if (ok)
        dropBuffers();
#else
    Q_UNUSED(x)
    Q_UNUSED(y)
#endif
}

// ---------------------------------------------------------------------------
// Subpicture-Anzeige
// ---------------------------------------------------------------------------

void DvdNav::updateHighlight()
{
#ifdef LUMEN_HAVE_DVDNAV
    Highlight hl;
    bool buttons = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_session)
            return;
        dvdnav_t *dvd = m_session->dvd;
        pci_t *pci = dvdnav_get_current_nav_pci(dvd);
        int32_t button = 0;
        dvdnav_get_current_highlight(dvd, &button);
        buttons = pci && (pci->hli.hl_gi.hli_ss & 3) && pci->hli.hl_gi.btn_ns > 0;
        dvdnav_highlight_area_t area{};
        if (buttons && button > 0 && button <= pci->hli.hl_gi.btn_ns
            && dvdnav_get_highlight_area(pci, button, 0, &area) == DVDNAV_STATUS_OK) {
            hl.on = true;
            hl.sx = area.sx;
            hl.sy = area.sy;
            hl.ex = area.ex;
            hl.ey = area.ey;
            hl.palette = area.palette;
        }
    }
    const bool changed = hl.on != m_hl.on || hl.sx != m_hl.sx || hl.sy != m_hl.sy || hl.ex != m_hl.ex || hl.ey != m_hl.ey || hl.palette != m_hl.palette;
    m_hl = hl;
    if (buttons != m_menuVisible) {
        m_menuVisible = buttons;
        emit stateChanged();
    }
    if (changed)
        render();
#endif
}

void DvdNav::onSpu(const std::shared_ptr<Spu> &spu)
{
    if (spu->menu || m_menuDomain || spu->pts < 0) {
        m_queue.clear();
        m_shown = spu->display ? spu : nullptr;
        render();
        return;
    }
    m_queue.append(spu);
    if (m_queue.size() > 64)
        m_queue.removeFirst();
}

// Zeitgenaue Anzeige: SPU-PTS gegen die Wiedergabezeit von mpv
void DvdNav::tick()
{
    if (!m_mpv || (m_queue.isEmpty() && (!m_shown || m_shown->stop < 0)))
        return;
    double now = 0, start = 0;
    if (mpv_get_property(m_mpv, "time-pos", MPV_FORMAT_DOUBLE, &now) < 0)
        return;
    mpv_get_property(m_mpv, "demuxer-start-time", MPV_FORMAT_DOUBLE, &start);
    auto at = [start](const Spu &s, double off) { return s.pts / kTicks - start + off; };

    bool changed = false;
    while (!m_queue.isEmpty() && at(*m_queue.first(), m_queue.first()->start) <= now + 0.02) {
        auto s = m_queue.takeFirst();
        const bool expired = s->stop >= 0 && at(*s, s->stop) < now;
        if (!expired && s->display && (s->forced || m_userSpu >= 0)) {
            m_shown = s;
            changed = true;
        } else if (m_shown && !s->display) {
            m_shown.reset();
            changed = true;
        }
    }
    // Nach einem Sprung zurück: veraltete Einträge verwerfen
    if (!m_queue.isEmpty() && at(*m_queue.first(), 0) > now + 30)
        m_queue.clear();
    if (m_shown && !m_shown->menu && m_shown->stop >= 0 && at(*m_shown, m_shown->stop) <= now) {
        m_shown.reset();
        changed = true;
    }
    if (changed)
        render();
}

void DvdNav::render()
{
    if (!m_shown || m_shown->rect.isEmpty()) {
        m_overlay = QImage();
        pushOverlay(QImage());
        return;
    }
    const Spu &s = *m_shown;
    QRgb pal[4], hlPal[4];
    for (int i = 0; i < 4; ++i) {
        pal[i] = (ycrcbToRgb(m_clut[s.color[i] & 0xf]) & 0x00ffffff) | (uint(s.alpha[i] * 17) << 24);
        const int c = int((m_hl.palette >> (16 + 4 * i)) & 0xf);
        const int a = int((m_hl.palette >> (4 * i)) & 0xf);
        hlPal[i] = (ycrcbToRgb(m_clut[c]) & 0x00ffffff) | (uint(a * 17) << 24);
    }
    QImage img(s.rect.size(), QImage::Format_ARGB32);
    const auto *px = reinterpret_cast<const uchar *>(s.pixels.constData());
    const QRect hlRect = m_hl.on ? QRect(QPoint(m_hl.sx, m_hl.sy), QPoint(m_hl.ex, m_hl.ey)) : QRect();
    for (int y = 0; y < s.rect.height(); ++y) {
        auto *row = reinterpret_cast<QRgb *>(img.scanLine(y));
        const int vy = s.rect.y() + y;
        const bool rowHl = m_hl.on && vy >= hlRect.top() && vy <= hlRect.bottom();
        for (int x = 0; x < s.rect.width(); ++x) {
            const int v = px[y * s.rect.width() + x] & 3;
            const int vx = s.rect.x() + x;
            row[x] = (rowHl && vx >= hlRect.left() && vx <= hlRect.right()) ? hlPal[v] : pal[v];
        }
    }
    m_overlay = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    pushOverlay(m_overlay);
}

void DvdNav::setOsdDimensions(const QVariantMap &dims)
{
    m_osd = dims;
    pushOverlay(m_overlay);
}

void DvdNav::pushOverlay(const QImage &img)
{
    if (!m_mpv)
        return;
    const int ow = m_osd.value("w").toInt(), oh = m_osd.value("h").toInt();
    if (img.isNull() || ow <= 0 || oh <= 0 || !m_shown) {
        if (m_overlayVisible) {
            const char *args[] = {"overlay-remove", kOverlayId, nullptr};
            mpv_command_async(m_mpv, 0, args);
            m_overlayVisible = false;
        }
        return;
    }
    const double ml = m_osd.value("ml").toDouble(), mt = m_osd.value("mt").toDouble();
    const double vw = ow - ml - m_osd.value("mr").toDouble();
    const double vh = oh - mt - m_osd.value("mb").toDouble();
    const double sx = vw / m_videoSize.width(), sy = vh / m_videoSize.height();
    const QRect r = m_shown->rect;
    const QByteArray addr = "&" + QByteArray::number(quintptr(img.constBits()));
    const QByteArray w = QByteArray::number(img.width()), h = QByteArray::number(img.height());
    const QByteArray stride = QByteArray::number(img.bytesPerLine());
    const QByteArray x = QByteArray::number(qRound(ml + r.x() * sx)), y = QByteArray::number(qRound(mt + r.y() * sy));
    const QByteArray dw = QByteArray::number(qMax(1, qRound(img.width() * sx))), dh = QByteArray::number(qMax(1, qRound(img.height() * sy)));
    const char *args[] = {"overlay-add", kOverlayId, x.constData(), y.constData(), addr.constData(), "0", "bgra",
                          w.constData(), h.constData(), stride.constData(), dw.constData(), dh.constData(), nullptr};
    mpv_command(m_mpv, args); // synchron: mpv kopiert die Pixel
    m_overlayVisible = true;
}
