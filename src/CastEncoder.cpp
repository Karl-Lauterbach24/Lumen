#include "CastEncoder.h"

#include "AudioTap.h"
#include "CastAudio.h"
#include "CastOutput.h"
#include "CastStream.h"
#include "Tr.h"

#include <QMutexLocker>

#include <mpv/client.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

const int kAudioFrame = 1024; // AAC
const AVRational kVideoBase{1, 90000};

// libx264 zuerst (überall gleich, in Lumens FFmpeg enthalten); die anderen nur als Ersatz,
// falls Lumen gegen ein fremdes FFmpeg gebaut wurde
const char *const kVideoEncoders[] = {"libx264", "h264_videotoolbox", "libopenh264", "h264_mf"};

const AVCodec *findVideoEncoder()
{
    for (const char *name : kVideoEncoders) {
        if (const AVCodec *c = avcodec_find_encoder_by_name(name))
            return c;
    }
    return nullptr;
}

QString avError(int err)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(err, buf, sizeof buf);
    return QString::fromUtf8(buf);
}

int writeCallback(void *opaque, const uint8_t *buf, int size)
{
    static_cast<CastStream *>(opaque)->append(QByteArray(reinterpret_cast<const char *>(buf), size));
    return size;
}

// Alles, was der Encoder-Thread an FFmpeg-Objekten hält
struct Pipeline
{
    AVCodecContext *video = nullptr;
    AVCodecContext *audio = nullptr;
    AVFormatContext *mux = nullptr;
    AVStream *videoStream = nullptr;
    AVStream *audioStream = nullptr;
    SwsContext *sws = nullptr;
    AVFrame *picture = nullptr;
    AVFrame *samples = nullptr;
    AVPacket *packet = nullptr;

    ~Pipeline()
    {
        if (mux) {
            if (mux->pb) {
                av_freep(&mux->pb->buffer);
                avio_context_free(&mux->pb);
            }
            avformat_free_context(mux);
        }
        avcodec_free_context(&video);
        avcodec_free_context(&audio);
        sws_freeContext(sws);
        av_frame_free(&picture);
        av_frame_free(&samples);
        av_packet_free(&packet);
    }
};

} // namespace

bool castTapAvailable()
{
    static const bool available = [] {
        mpv_handle *probe = mpv_create();
        if (!probe)
            return false;
        const bool ok = mpv_set_option_string(probe, "ao-null-outfile", "") >= 0;
        mpv_destroy(probe);
        return ok;
    }();
    return available;
}

CastEncoder::CastEncoder(CastStream *stream, AudioTap *tap, const Settings &settings, QObject *parent)
    : QThread(parent)
    , m_stream(stream)
    , m_tap(tap)
    , m_settings(settings)
{
}

CastEncoder::~CastEncoder()
{
    end();
}

QString CastEncoder::videoEncoderName()
{
    const AVCodec *c = findVideoEncoder();
    return (c && avcodec_find_encoder_by_name("aac")) ? QString::fromLatin1(c->name) : QString();
}

void CastEncoder::begin()
{
    m_stop = false;
    m_clock.start();
    start();
}

void CastEncoder::end()
{
    m_stop = true;
    wait();
}

void CastEncoder::setMpvTime(qint64 mpvTimeUs)
{
    m_offset = clock() - double(mpvTimeUs) / 1e6;
}

double CastEncoder::streamTime(qint64 mpvTimeUs) const
{
    return double(mpvTimeUs) / 1e6 + m_offset.load() + CastAudio::kDelay;
}

void CastEncoder::pushFrame(QByteArray &&rgba, double pts)
{
    QMutexLocker lock(&m_mutex);
    // Encoder zu langsam: lieber ein altes Bild verwerfen als immer weiter zurückfallen
    while (m_frames.size() >= 3) {
        m_frames.dequeue();
        ++m_overflow;
    }
    ++m_pushed;
    m_frames.enqueue(Frame{std::move(rgba), pts});
}

void CastEncoder::run()
{
    const Settings s = m_settings;
    Pipeline p;
    int err = 0;

    // ---- Bild ----
    const AVCodec *vcodec = findVideoEncoder();
    const AVCodec *acodec = avcodec_find_encoder_by_name("aac");
    if (!vcodec || !acodec) {
        emit failed(LTR("In dieser FFmpeg-Bibliothek fehlt ein H.264- oder AAC-Encoder."));
        return;
    }
    p.video = avcodec_alloc_context3(vcodec);
    p.video->width = s.width;
    p.video->height = s.height;
    p.video->pix_fmt = AV_PIX_FMT_YUV420P;
    p.video->time_base = kVideoBase;
    p.video->framerate = AVRational{s.fpsLimit, 1};
    p.video->bit_rate = qint64(s.videoKbps) * 1000;
    p.video->rc_max_rate = p.video->bit_rate;
    p.video->rc_buffer_size = int(p.video->bit_rate);
    p.video->gop_size = s.fpsLimit * 10; // Schlüsselbilder setzt die Segmentierung
    p.video->max_b_frames = 0;
    p.video->color_range = AVCOL_RANGE_MPEG;
    p.video->colorspace = AVCOL_SPC_BT709;
    p.video->color_primaries = AVCOL_PRI_BT709;
    p.video->color_trc = AVCOL_TRC_BT709;
    p.video->sample_aspect_ratio = AVRational{1, 1};
    AVDictionary *vopts = nullptr;
    if (!strcmp(vcodec->name, "libx264")) {
        av_dict_set(&vopts, "preset", "veryfast", 0);
        av_dict_set(&vopts, "tune", "zerolatency", 0);
        av_dict_set(&vopts, "profile", "high", 0);
        av_dict_set(&vopts, "forced-idr", "1", 0);
        av_dict_set(&vopts, "x264-params", "scenecut=0", 0);
    } else if (!strcmp(vcodec->name, "h264_videotoolbox")) {
        av_dict_set(&vopts, "realtime", "1", 0);
        av_dict_set(&vopts, "allow_sw", "1", 0);
    }
    err = avcodec_open2(p.video, vcodec, &vopts);
    av_dict_free(&vopts);
    if (err < 0) {
        emit failed(LTR("H.264-Encoder (%1) lässt sich nicht öffnen: %2").arg(QString::fromLatin1(vcodec->name), avError(err)));
        return;
    }

    // ---- Ton ----
    p.audio = avcodec_alloc_context3(acodec);
    p.audio->sample_fmt = AV_SAMPLE_FMT_FLTP;
    p.audio->sample_rate = AudioTap::kRate;
    av_channel_layout_default(&p.audio->ch_layout, AudioTap::kChannels);
    p.audio->bit_rate = qint64(s.audioKbps) * 1000;
    p.audio->time_base = AVRational{1, AudioTap::kRate};
    if ((err = avcodec_open2(p.audio, acodec, nullptr)) < 0) {
        emit failed(LTR("AAC-Encoder lässt sich nicht öffnen: %1").arg(avError(err)));
        return;
    }

    // ---- MPEG-TS ----
    avformat_alloc_output_context2(&p.mux, nullptr, "mpegts", nullptr);
    if (!p.mux) {
        emit failed(LTR("MPEG-TS-Ausgabe nicht verfügbar"));
        return;
    }
    const int ioSize = 188 * 348;
    p.mux->pb = avio_alloc_context(static_cast<unsigned char *>(av_malloc(ioSize)), ioSize, 1, m_stream, nullptr, writeCallback, nullptr);
    p.mux->flags |= AVFMT_FLAG_CUSTOM_IO;
    p.videoStream = avformat_new_stream(p.mux, nullptr);
    p.audioStream = avformat_new_stream(p.mux, nullptr);
    avcodec_parameters_from_context(p.videoStream->codecpar, p.video);
    avcodec_parameters_from_context(p.audioStream->codecpar, p.audio);
    p.videoStream->time_base = p.video->time_base;
    p.audioStream->time_base = p.audio->time_base;
    AVDictionary *mopts = nullptr;
    av_dict_set(&mopts, "mpegts_flags", "+resend_headers", 0);
    av_dict_set(&mopts, "pat_period", "0.5", 0);
    av_dict_set(&mopts, "muxdelay", "0", 0);
    err = avformat_write_header(p.mux, &mopts);
    av_dict_free(&mopts);
    if (err < 0) {
        emit failed(LTR("MPEG-TS-Ausgabe lässt sich nicht starten: %1").arg(avError(err)));
        return;
    }

    p.sws = sws_getContext(s.width, s.height, AV_PIX_FMT_RGBA, s.width, s.height, AV_PIX_FMT_YUV420P,
                           SWS_POINT, nullptr, nullptr, nullptr);
    // RGB (voller Bereich) -> BT.709, begrenzter Bereich
    sws_setColorspaceDetails(p.sws, sws_getCoefficients(SWS_CS_ITU709), 1, sws_getCoefficients(SWS_CS_ITU709), 0, 0, 1 << 16, 1 << 16);
    p.picture = av_frame_alloc();
    p.picture->format = AV_PIX_FMT_YUV420P;
    p.picture->width = s.width;
    p.picture->height = s.height;
    av_frame_get_buffer(p.picture, 0);
    p.samples = av_frame_alloc();
    p.samples->format = AV_SAMPLE_FMT_FLTP;
    p.samples->sample_rate = AudioTap::kRate;
    av_channel_layout_copy(&p.samples->ch_layout, &p.audio->ch_layout);
    p.samples->nb_samples = kAudioFrame;
    av_frame_get_buffer(p.samples, 0);
    p.packet = av_packet_alloc();

    bool failedWrite = false;
    auto flushMux = [&] {
        av_write_frame(p.mux, nullptr); // angefangene Ton-PES-Pakete herausschreiben
        avio_flush(p.mux->pb);
    };

    // ---- Zustand ----
    qint64 audioSamples = 0;           // bereits kodierte Samples = Position des Tons im Strom
    CastAudio audio;                   // Ton aus mpv, nach Abspielzeit geordnet
    QByteArray chunk(64 * 1024, Qt::Uninitialized);
    std::vector<qint16> pcm(size_t(kAudioFrame * AudioTap::kChannels));
    qint64 lastVideoPts = -1;          // in 1/90000
    qint64 segmentStart = -1;
    bool havePicture = false;
    double lastPictureAt = 0;
    const qint64 minInterval = 90000 / std::max(1, s.fpsLimit);
    const qint64 segmentTicks = qint64(s.segmentSeconds * 90000);

    auto writeVideoPackets = [&] {
        while (avcodec_receive_packet(p.video, p.packet) == 0) {
            if ((p.packet->flags & AV_PKT_FLAG_KEY) && segmentStart >= 0 && p.packet->pts - segmentStart >= segmentTicks * 9 / 10) {
                // Segmentgrenze: alles Bisherige abschließen, das nächste beginnt mit PAT/PMT + Schlüsselbild
                flushMux();
                m_stream->closeSegment(double(p.packet->pts - segmentStart) / 90000.0);
                av_opt_set(p.mux->priv_data, "mpegts_flags", "+resend_headers", 0);
                segmentStart = p.packet->pts;
            } else if (segmentStart < 0) {
                segmentStart = p.packet->pts;
            }
            p.packet->stream_index = p.videoStream->index;
            av_packet_rescale_ts(p.packet, p.video->time_base, p.videoStream->time_base);
            if (av_write_frame(p.mux, p.packet) < 0)
                failedWrite = true;
            av_packet_unref(p.packet);
        }
    };
    auto encodePicture = [&](qint64 pts) {
        if (pts <= lastVideoPts)
            pts = lastVideoPts + 1;
        lastVideoPts = pts;
        p.picture->pts = pts;
        const bool key = segmentStart < 0 || pts - segmentStart >= segmentTicks;
        p.picture->pict_type = key ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_NONE;
        if (key)
            p.picture->flags |= AV_FRAME_FLAG_KEY;
        else
            p.picture->flags &= ~AV_FRAME_FLAG_KEY;
        if (avcodec_send_frame(p.video, p.picture) == 0)
            writeVideoPackets();
    };

    // Entwickler-Hilfe: LUMEN_CAST_DEBUG=1 gibt alle zwei Sekunden Zähler aus
    const bool debug = qEnvironmentVariableIsSet("LUMEN_CAST_DEBUG");
    int encoded = 0, skipped = 0;
    qint64 silence = 0;
    double lastReport = 0;

    while (!m_stop && !failedWrite) {
        const double now = clock();
        if (debug && now - lastReport >= 2.0) {
            std::fprintf(stderr, "cast %.1fs: Bilder erhalten %d, kodiert %d, Bildratengrenze %d, verworfen %d; Stille %.0f ms; Ton im Vorlauf %.0f ms\n",
                         now, int(m_pushed), encoded, skipped, int(m_overflow), silence * 1000.0 / AudioTap::kRate,
                         audio.queued() * 1000.0 / AudioTap::kRate);
            lastReport = now;
        }

        // ---- Ton: alles aus der Pipe einordnen, dann bis zur aktuellen Uhrzeit ausgeben ----
        audio.setOffset(m_offset.load());
        for (int round = 0; round < 64; ++round) {
            const int got = m_tap->read(chunk.data(), chunk.size());
            if (got <= 0)
                break;
            audio.feed(chunk.constData(), got);
        }
        while (double(audioSamples + kAudioFrame) / AudioTap::kRate <= now) {
            const int real = audio.take(audioSamples, pcm.data(), kAudioFrame);
            silence += kAudioFrame - real;
            av_frame_make_writable(p.samples);
            for (int ch = 0; ch < AudioTap::kChannels; ++ch) {
                auto *out = reinterpret_cast<float *>(p.samples->data[ch]);
                for (int i = 0; i < kAudioFrame; ++i)
                    out[i] = pcm[size_t(i * AudioTap::kChannels + ch)] / 32768.0f;
            }
            p.samples->pts = audioSamples;
            audioSamples += kAudioFrame;
            if (avcodec_send_frame(p.audio, p.samples) == 0) {
                while (avcodec_receive_packet(p.audio, p.packet) == 0) {
                    p.packet->stream_index = p.audioStream->index;
                    av_packet_rescale_ts(p.packet, p.audio->time_base, p.audioStream->time_base);
                    if (av_write_frame(p.mux, p.packet) < 0)
                        failedWrite = true;
                    av_packet_unref(p.packet);
                }
            }
        }

        // ---- Bild ----
        for (;;) {
            Frame f;
            {
                QMutexLocker lock(&m_mutex);
                if (m_frames.isEmpty())
                    break;
                f = m_frames.dequeue();
            }
            const qint64 pts = qint64(std::llround(f.pts * 90000.0));
            if (f.rgba.size() != s.width * s.height * 4)
                continue;
            // Bildratengrenze: Bilder, die zu dicht auf das vorige folgen, auslassen. Die Schwelle liegt
            // bei drei Vierteln des Abstands: 60 -> 30 fps lässt jedes zweite aus, 30 fps bleiben trotz
            // schwankender Zeitstempel vollständig.
            if (lastVideoPts >= 0 && pts - lastVideoPts < minInterval * 3 / 4) {
                ++skipped;
                continue;
            }
            ++encoded;
            const uint8_t *src[1] = {reinterpret_cast<const uint8_t *>(f.rgba.constData())};
            const int stride[1] = {s.width * 4};
            av_frame_make_writable(p.picture);
            sws_scale(p.sws, src, stride, 0, s.height, p.picture->data, p.picture->linesize);
            havePicture = true;
            lastPictureAt = now;
            encodePicture(pts);
        }
        // Standbild (Pause, Menü, Leerlauf): letztes Bild wiederholen, damit der Strom weiterläuft
        if (now - lastPictureAt > 0.25) {
            if (!havePicture) {
                av_frame_make_writable(p.picture);
                for (int y = 0; y < s.height; ++y)
                    memset(p.picture->data[0] + y * p.picture->linesize[0], 16, s.width);
                for (int plane = 1; plane <= 2; ++plane)
                    for (int y = 0; y < s.height / 2; ++y)
                        memset(p.picture->data[plane] + y * p.picture->linesize[plane], 128, s.width / 2);
                havePicture = true;
            } else {
                av_frame_make_writable(p.picture);
            }
            lastPictureAt = now;
            encodePicture(qint64(std::llround(now * 90000.0)));
        }

        flushMux();
        QThread::msleep(4);
    }

    if (failedWrite)
        emit failed(LTR("Der Sendestrom konnte nicht geschrieben werden."));
}
