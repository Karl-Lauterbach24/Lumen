#include "UiAudio.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>

#include <cmath>

#ifdef LUMEN_HAVE_ALSA
#include <alsa/asoundlib.h>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}
#endif

namespace {
const int kRate = 48000;
const int kBlock = 480; // 10 ms
}

UiAudio::UiAudio(bool enabled, QObject *parent)
    : QObject(parent)
#ifdef LUMEN_HAVE_ALSA
    , m_enabled(enabled)
#else
    , m_enabled(false)
#endif
{
    if (m_enabled)
        m_thread = std::thread([this] { run(); });
}

UiAudio::~UiAudio()
{
    if (m_thread.joinable()) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_quit = true;
        }
        m_wake.notify_all();
        m_thread.join();
    }
}

void UiAudio::wake()
{
    m_wake.notify_all();
}

QString UiAudio::findFile(const QString &name)
{
    // eigene Klänge gehen vor; LUMEN_SOUNDS für den Entwickler
    QStringList dirs;
    const QString env = qEnvironmentVariable("LUMEN_SOUNDS");
    if (!env.isEmpty())
        dirs << env;
    dirs << QStringLiteral("/etc/lumenos/sounds") << QCoreApplication::applicationDirPath() + QStringLiteral("/../share/lumen/sounds");
    for (const QString &dir : std::as_const(dirs)) {
        for (const char *ext : {"flac", "wav", "ogg", "opus", "mp3", "m4a"}) {
            const QString path = QStringLiteral("%1/%2.%3").arg(dir, name, QLatin1String(ext));
            if (QFileInfo::exists(path))
                return path; // (auch eine leere Datei: sie bringt den Klang zum Schweigen)
        }
    }
    return {};
}

std::vector<float> UiAudio::decodeFile(const QString &path)
{
    std::vector<float> out;
#ifdef LUMEN_HAVE_ALSA
    if (path.isEmpty() || QFileInfo(path).size() == 0)
        return out;
    AVFormatContext *format = nullptr;
    if (avformat_open_input(&format, QFile::encodeName(path).constData(), nullptr, nullptr) < 0)
        return out;
    AVCodecContext *codec = nullptr;
    SwrContext *swr = nullptr;
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    const auto convert = [&](const AVFrame *in) {
        // (in == nullptr: was der Umsetzer noch hält)
        const int room = int(swr_get_out_samples(swr, in ? in->nb_samples : 0)) + 64;
        const size_t before = out.size();
        out.resize(before + size_t(room) * 2);
        uint8_t *planes[1] = {reinterpret_cast<uint8_t *>(out.data() + before)};
        const int got = swr_convert(swr, planes, room, in ? const_cast<const uint8_t **>(in->extended_data) : nullptr, in ? in->nb_samples : 0);
        out.resize(before + size_t(got > 0 ? got : 0) * 2);
    };
    do {
        if (avformat_find_stream_info(format, nullptr) < 0)
            break;
        const AVCodec *decoder = nullptr;
        const int stream = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0);
        if (stream < 0 || !decoder)
            break;
        codec = avcodec_alloc_context3(decoder);
        if (!codec || avcodec_parameters_to_context(codec, format->streams[stream]->codecpar) < 0 || avcodec_open2(codec, decoder, nullptr) < 0)
            break;
        AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
        AVChannelLayout from;
        if (codec->ch_layout.nb_channels > 0 && codec->ch_layout.order != AV_CHANNEL_ORDER_UNSPEC)
            av_channel_layout_copy(&from, &codec->ch_layout);
        else
            av_channel_layout_default(&from, codec->ch_layout.nb_channels > 0 ? codec->ch_layout.nb_channels : 2);
        const int ok = swr_alloc_set_opts2(&swr, &stereo, AV_SAMPLE_FMT_FLT, kRate, &from, codec->sample_fmt, codec->sample_rate, 0, nullptr);
        av_channel_layout_uninit(&from);
        if (ok < 0 || !swr || swr_init(swr) < 0)
            break;
        // (höchstens fünf Minuten: mehr braucht keine Schleife, und alles liegt im Speicher)
        const size_t limit = size_t(kRate) * 2 * 300;
        while (out.size() < limit && av_read_frame(format, packet) >= 0) {
            if (packet->stream_index == stream && avcodec_send_packet(codec, packet) >= 0) {
                while (avcodec_receive_frame(codec, frame) >= 0)
                    convert(frame);
            }
            av_packet_unref(packet);
        }
        avcodec_send_packet(codec, nullptr);
        while (avcodec_receive_frame(codec, frame) >= 0)
            convert(frame);
        convert(nullptr);
    } while (false);
    av_frame_free(&frame);
    av_packet_free(&packet);
    if (swr)
        swr_free(&swr);
    if (codec)
        avcodec_free_context(&codec);
    avformat_close_input(&format);
#else
    Q_UNUSED(path)
#endif
    return out;
}

std::shared_ptr<const UiAudio::Clip> UiAudio::clip(const QString &name)
{
    const auto it = m_clips.constFind(name);
    if (it != m_clips.constEnd())
        return *it;
    auto loaded = std::make_shared<Clip>();
    loaded->samples = decodeFile(findFile(name));
    std::shared_ptr<const Clip> result = loaded->samples.empty() ? nullptr : loaded;
    m_clips.insert(name, result);
    return result;
}

void UiAudio::play(const QString &name)
{
    if (!m_enabled || !m_effects || m_effectsVolume <= 0)
        return;
    const auto c = clip(name);
    if (!c)
        return;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_suspended || m_pending.size() > 12)
            return;
        // (das Quadrat: ein Regler in der Mitte klingt halb so laut, nicht halb so stark)
        m_pending.push_back({c, 0, float(m_effectsVolume * m_effectsVolume)});
    }
    wake();
}

void UiAudio::setEffects(bool on)
{
    if (m_effects == on)
        return;
    m_effects = on;
    emit changed();
}

void UiAudio::setEffectsVolume(qreal volume)
{
    volume = qBound(0.0, volume, 1.0);
    if (qFuzzyCompare(m_effectsVolume, volume))
        return;
    m_effectsVolume = volume;
    emit changed();
}

void UiAudio::setMusicVolume(qreal volume)
{
    volume = qBound(0.0, volume, 1.0);
    if (qFuzzyCompare(m_musicVolume, volume))
        return;
    m_musicVolume = volume;
    m_musicLevel.store(float(volume * volume));
    emit changed();
}

void UiAudio::setMusic(bool on)
{
    if (m_music == on && (!on || m_musicClip))
        return;
    m_music = on;
    if (m_enabled) {
        const auto c = on ? clip(QStringLiteral("music")) : nullptr;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (c)
                m_musicClip = c;
            m_musicOn = on && m_musicClip;
        }
        m_musicLevel.store(float(m_musicVolume * m_musicVolume));
        wake();
    }
    emit changed();
}

void UiAudio::setDevice(const QString &mpvDevice)
{
    QByteArray device = "default";
    if (mpvDevice.startsWith(QLatin1String("alsa/")))
        device = mpvDevice.mid(5).toUtf8();
    std::lock_guard<std::mutex> lock(m_mutex);
    m_device = device;
}

void UiAudio::suspend()
{
    if (!m_enabled)
        return;
    std::unique_lock<std::mutex> lock(m_mutex);
    m_suspended = true;
    m_pending.clear();
    m_wake.notify_all();
    // … bis der Ausgang zu ist (er blendet in drei Hundertstelsekunden aus); im Zweifel nicht ewig
    m_closed.wait_for(lock, std::chrono::milliseconds(600), [this] { return !m_open; });
}

void UiAudio::resume()
{
    if (!m_enabled)
        return;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_suspended = false;
    }
    wake();
}

void UiAudio::run()
{
#ifdef LUMEN_HAVE_ALSA
    std::vector<Voice> voices;
    std::vector<float> mix(kBlock * 2);
    std::vector<int16_t> pcmOut(kBlock * 2);
    std::shared_ptr<const Clip> music;
    size_t musicAt = 0;
    float musicGain = 0;
    uint32_t noise = 12345;

    for (;;) {
        QByteArray device;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_wake.wait(lock, [this] { return m_quit || (!m_suspended && (m_musicOn || !m_pending.empty())); });
            if (m_quit)
                return;
            device = m_device;
        }
        snd_pcm_t *pcm = nullptr;
        if (snd_pcm_open(&pcm, device.constData(), SND_PCM_STREAM_PLAYBACK, 0) < 0
            || snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 2, kRate, 1, 90000) < 0) {
            if (pcm)
                snd_pcm_close(pcm);
            // belegt oder nicht da: was jetzt hätte klingen sollen, verfällt; später neu versuchen
            std::unique_lock<std::mutex> lock(m_mutex);
            m_pending.clear();
            m_wake.wait_for(lock, std::chrono::seconds(3), [this] { return m_quit || m_suspended; });
            if (m_quit)
                return;
            continue;
        }
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_open = true;
        }
        musicGain = 0;
        double idle = 0;
        int closing = -1; // Blöcke bis zum Schließen, wenn ausgeblendet wird
        for (;;) {
            bool musicOn, stop;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                for (Voice &v : m_pending)
                    voices.push_back(std::move(v));
                m_pending.clear();
                if (m_musicClip != music) {
                    music = m_musicClip;
                    musicAt = 0;
                }
                musicOn = m_musicOn;
                stop = m_quit || m_suspended;
            }
            if (stop && closing < 0)
                closing = 3;
            const float level = m_musicLevel.load();
            const float target = (musicOn && closing < 0) ? level : 0.0f;
            // hinein in zwei Sekunden, hinaus in einer halben
            const float up = float(1.0 / (2.0 * kRate)), down = float(1.0 / (0.5 * kRate));
            const float master0 = closing < 0 ? 1.0f : float(closing) / 3.0f;
            const float master1 = closing < 0 ? 1.0f : float(closing - 1 > 0 ? closing - 1 : 0) / 3.0f;
            bool sounding = false;

            const size_t total = music ? music->samples.size() / 2 : 0;
            // Die Naht der Schleife: das Ende wird in den Anfang geblendet (eine Sekunde, höchstens ein Viertel)
            const size_t fade = total ? std::min<size_t>(size_t(kRate), total / 4) : 0;
            const size_t loop = total - fade;
            for (int i = 0; i < kBlock; ++i) {
                float l = 0, r = 0;
                if (musicGain < target)
                    musicGain = std::min(target, musicGain + up);
                else if (musicGain > target)
                    musicGain = std::max(target, musicGain - (target > 0 ? up : down));
                if (total > 0 && (musicGain > 0 || target > 0)) {
                    if (musicAt >= loop)
                        musicAt = 0;
                    const float *s = music->samples.data();
                    float ml = s[2 * musicAt], mr = s[2 * musicAt + 1];
                    if (musicAt < fade) {
                        const float x = float(musicAt) / float(fade);
                        const float in = std::sqrt(x), outGain = std::sqrt(1.0f - x);
                        ml = ml * in + s[2 * (loop + musicAt)] * outGain;
                        mr = mr * in + s[2 * (loop + musicAt) + 1] * outGain;
                    }
                    ++musicAt;
                    l += ml * musicGain;
                    r += mr * musicGain;
                    sounding = true;
                }
                for (Voice &v : voices) {
                    if (!v.clip)
                        continue;
                    const std::vector<float> &s = v.clip->samples;
                    if (v.at + 1 >= s.size()) {
                        v.clip.reset();
                        continue;
                    }
                    l += s[v.at] * v.gain;
                    r += s[v.at + 1] * v.gain;
                    v.at += 2;
                    sounding = true;
                }
                const float master = master0 + (master1 - master0) * float(i) / float(kBlock);
                mix[2 * i] = std::tanh(l * master);
                mix[2 * i + 1] = std::tanh(r * master);
            }
            voices.erase(std::remove_if(voices.begin(), voices.end(), [](const Voice &v) { return !v.clip; }), voices.end());

            for (int i = 0; i < kBlock * 2; ++i) {
                noise = noise * 1664525u + 1013904223u;
                const float dither = float((noise >> 8) & 0xffff) / 65536.0f - 0.5f;
                const long v = std::lround(double(mix[i]) * 32767.0 + dither);
                pcmOut[i] = int16_t(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
            }
            snd_pcm_sframes_t wrote = snd_pcm_writei(pcm, pcmOut.data(), kBlock);
            if (wrote < 0)
                wrote = snd_pcm_recover(pcm, int(wrote), 1);
            if (wrote < 0)
                break; // der Ausgang ist weg (abgezogen): schließen und neu versuchen
            if (closing >= 0 && --closing <= 0)
                break;
            // nichts mehr zu spielen: nach einer Sekunde Stille zu (der Ausgang soll nicht belegt bleiben)
            idle = sounding ? 0 : idle + double(kBlock) / kRate;
            if (idle > 1.0)
                break;
        }
        voices.clear();
        snd_pcm_drop(pcm);
        snd_pcm_close(pcm);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_open = false;
        }
        m_closed.notify_all();
    }
#endif
}
