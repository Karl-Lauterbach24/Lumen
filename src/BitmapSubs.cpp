#include "BitmapSubs.h"

#include <QFile>
#include <QPainter>

#include <algorithm>
#include <cmath>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace {
// So weit liest der Thread der Wiedergabe voraus (Sekunden)
constexpr double kReadAhead = 30;
// Ein Untertitel ohne Ende verschwindet spätestens nach dieser Zeit, falls die Zeile, die ihn
// beendet, (noch) nicht gelesen ist
constexpr double kOpenEnd = 15;
}

BitmapSubs::BitmapSubs(QObject *parent)
    : QObject(parent)
{
}

BitmapSubs::~BitmapSubs()
{
    stop();
}

bool BitmapSubs::active(const QString &path, int streamIndex) const
{
    QMutexLocker lock(&m_lock);
    return m_thread && m_path == path && m_stream == streamIndex;
}

void BitmapSubs::start(const QString &path, int streamIndex, const QString &codec)
{
    stop();
    {
        QMutexLocker lock(&m_lock);
        m_events.clear();
        m_coveredFrom = 0;
        m_coveredTo = -1;
        m_path = path;
        m_stream = streamIndex;
    }
    m_stop = false;
    m_seekTo = -1;
    m_thread = QThread::create([this, path, streamIndex, codec] { run(path, streamIndex, codec); });
    m_thread->setObjectName(QStringLiteral("lumen-bitmapsubs"));
    m_thread->start(QThread::LowPriority);
}

void BitmapSubs::stop()
{
    if (!m_thread)
        return;
    m_stop = true;
    m_thread->wait();
    delete m_thread;
    m_thread = nullptr;
    QMutexLocker lock(&m_lock);
    m_path.clear();
    m_stream = -1;
}

void BitmapSubs::setPosition(double seconds)
{
    m_position = seconds;
    QMutexLocker lock(&m_lock);
    // in einen Bereich gesprungen, der noch nicht gelesen ist: dort neu ansetzen – etwas davor,
    // damit ein Untertitel, der schon zu sehen sein soll, mitkommt
    if (m_thread && (seconds < m_coveredFrom - 0.5 || seconds > m_coveredTo + 5)) {
        const double from = std::max(0.0, seconds - 10);
        m_seekTo = from;
        m_coveredFrom = m_coveredTo = from;
    }
}

BitmapSubs::Event BitmapSubs::at(double t, bool forcedOnly) const
{
    QMutexLocker lock(&m_lock);
    // das letzte Untertitelbild, das vor t beginnt
    auto it = std::upper_bound(m_events.cbegin(), m_events.cend(), t, [](double v, const Event &e) { return v < e.start; });
    while (it != m_events.cbegin()) {
        --it;
        if (forcedOnly && !it->forced && !it->image.isNull())
            continue; // nur erzwungene: ein gewöhnliches Bild zählt nicht, sein Ende schon
        if (it->image.isNull())
            return {};
        if (it->end >= 0)
            return t < it->end ? *it : Event();
        // Ohne Ende nur, wenn der Leser im selben Abschnitt schon über t hinaus ist: sonst kann die
        // Zeile, die das Bild beendet, noch ungelesen dazwischen liegen (gleich nach einem Sprung)
        if (it->start >= m_coveredFrom - 1 && t <= m_coveredTo && t < it->start + kOpenEnd)
            return *it;
        return {};
    }
    return {};
}

void BitmapSubs::addEvent(Event e)
{
    {
        QMutexLocker lock(&m_lock);
        auto it = std::lower_bound(m_events.begin(), m_events.end(), e.start, [](const Event &x, double v) { return x.start < v; });
        // nach einem Sprung kommt Gelesenes ein zweites Mal
        if (it != m_events.end() && std::abs(it->start - e.start) < 0.001 && it->image.isNull() == e.image.isNull())
            return;
        // ein Bild ohne eigenes Ende endet mit dem nächsten
        if (it != m_events.begin()) {
            Event &prev = *(it - 1);
            if (prev.end < 0 || prev.end > e.start)
                prev.end = e.start;
        }
        if (e.end < 0 && it != m_events.end())
            e.end = it->start;
        m_events.insert(it, std::move(e));
    }
    emit eventsChanged();
}

// Die Rechtecke eines dekodierten Untertitels (Palettenbilder) zu einem Bild zusammensetzen
static QImage compose(const AVSubtitle &sub, QRect *where, bool *forced)
{
    QRect all;
    for (unsigned i = 0; i < sub.num_rects; ++i) {
        const AVSubtitleRect *r = sub.rects[i];
        if (r->type == SUBTITLE_BITMAP && r->w > 0 && r->h > 0)
            all |= QRect(r->x, r->y, r->w, r->h);
    }
    *forced = false;
    if (all.isEmpty())
        return {};
    QImage img(all.size(), QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    for (unsigned i = 0; i < sub.num_rects; ++i) {
        const AVSubtitleRect *r = sub.rects[i];
        if (r->type != SUBTITLE_BITMAP || r->w <= 0 || r->h <= 0 || !r->data[0] || !r->data[1])
            continue;
        *forced = *forced || (r->flags & AV_SUBTITLE_FLAG_FORCED);
        const uint32_t *palette = reinterpret_cast<const uint32_t *>(r->data[1]);
        const int colors = std::clamp(r->nb_colors, 1, 256);
        for (int y = 0; y < r->h; ++y) {
            const uint8_t *src = r->data[0] + size_t(y) * r->linesize[0];
            auto *dst = reinterpret_cast<uint32_t *>(img.scanLine(r->y - all.y() + y)) + (r->x - all.x());
            for (int x = 0; x < r->w; ++x)
                dst[x] = src[x] < colors ? palette[src[x]] : 0; // 0xAARRGGBB wie QImage::Format_ARGB32
        }
    }
    *where = all;
    return img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

void BitmapSubs::run(QString path, int streamIndex, QString codec)
{
    AVFormatContext *fmt = nullptr;
    AVDictionary *opts = nullptr;
    av_dict_set(&opts, "probesize", "5000000", 0);
    av_dict_set(&opts, "analyzeduration", "2000000", 0);
    const QByteArray file = QFile::encodeName(path);
    const int opened = avformat_open_input(&fmt, file.constData(), nullptr, &opts);
    av_dict_free(&opts);
    if (opened < 0)
        return;
    AVCodecContext *ctx = nullptr;
    AVPacket *pkt = av_packet_alloc();
    auto cleanup = [&] {
        av_packet_free(&pkt);
        avcodec_free_context(&ctx);
        avformat_close_input(&fmt);
    };
    if (avformat_find_stream_info(fmt, nullptr) < 0 || !pkt) {
        cleanup();
        return;
    }
    // Die Spur, die mpv zeigt: FFmpegs Zählung stimmt bei Matroska und MPEG-TS; passt der Codec
    // nicht, die erste Untertitelspur mit diesem Codec
    const QByteArray want = codec.toLatin1();
    auto matches = [&](int i) {
        const AVCodecDescriptor *d = avcodec_descriptor_get(fmt->streams[i]->codecpar->codec_id);
        return fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE && d && (want.isEmpty() || want == d->name);
    };
    int index = streamIndex >= 0 && streamIndex < int(fmt->nb_streams) && matches(streamIndex) ? streamIndex : -1;
    for (unsigned i = 0; index < 0 && i < fmt->nb_streams; ++i)
        if (matches(int(i)))
            index = int(i);
    if (index < 0) {
        cleanup();
        return;
    }
    // Neben der Untertitelspur nur das Bild lesen: dessen Zeitstempel zeigen, wie weit gelesen ist
    const int video = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    for (unsigned i = 0; i < fmt->nb_streams; ++i)
        fmt->streams[i]->discard = (int(i) == index || int(i) == video) ? AVDISCARD_DEFAULT : AVDISCARD_ALL;

    AVStream *st = fmt->streams[index];
    const AVCodec *dec = avcodec_find_decoder(st->codecpar->codec_id);
    ctx = dec ? avcodec_alloc_context3(dec) : nullptr;
    if (!ctx || avcodec_parameters_to_context(ctx, st->codecpar) < 0) {
        cleanup();
        return;
    }
    ctx->pkt_timebase = st->time_base;
    if (avcodec_open2(ctx, dec, nullptr) < 0) {
        cleanup();
        return;
    }
    // Zeit wie mpvs time-pos: ab dem Anfang der Datei
    const double t0 = fmt->start_time != AV_NOPTS_VALUE ? fmt->start_time / double(AV_TIME_BASE) : 0;
    double readPos = 0;
    bool eof = false;

    while (!m_stop) {
        const double seek = m_seekTo.exchange(-1);
        if (seek >= 0) {
            av_seek_frame(fmt, -1, int64_t((seek + t0) * AV_TIME_BASE), AVSEEK_FLAG_BACKWARD);
            avcodec_flush_buffers(ctx);
            readPos = seek;
            eof = false;
        }
        if (eof || readPos > m_position + kReadAhead) {
            QThread::msleep(100);
            continue;
        }
        const int r = av_read_frame(fmt, pkt);
        if (r < 0) {
            if (r != AVERROR(EAGAIN)) {
                eof = true;
                QMutexLocker lock(&m_lock);
                m_coveredTo = 1e18;
            }
            continue;
        }
        const AVStream *ps = fmt->streams[pkt->stream_index];
        const double pts = pkt->pts != AV_NOPTS_VALUE ? pkt->pts * av_q2d(ps->time_base) - t0 : readPos;
        if (pkt->stream_index == index) {
            AVSubtitle sub;
            int got = 0;
            if (avcodec_decode_subtitle2(ctx, &sub, &got, pkt) >= 0 && got) {
                Event e;
                const double base = sub.pts != AV_NOPTS_VALUE ? sub.pts / double(AV_TIME_BASE) - t0 : pts;
                e.start = base + sub.start_display_time / 1000.0;
                if (sub.end_display_time != UINT32_MAX && sub.end_display_time > sub.start_display_time)
                    e.end = base + sub.end_display_time / 1000.0;
                e.canvas = ctx->width > 0 && ctx->height > 0 ? QSize(ctx->width, ctx->height) : QSize(1920, 1080);
                e.image = compose(sub, &e.rect, &e.forced);
                avsubtitle_free(&sub);
                addEvent(std::move(e));
            }
        }
        readPos = std::max(readPos, pts);
        av_packet_unref(pkt);
        QMutexLocker lock(&m_lock);
        m_coveredTo = std::max(m_coveredTo, readPos);
    }
    cleanup();
}
