#include "DcpIab.h"
#include "Tr.h"

#include <QFile>
#include <QHash>
#include <QtEndian>

#include <mpv/client.h>
#include <mpv/stream_cb.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <vector>

#ifdef LUMEN_HAVE_IAB
#include "IABElementsAPI.h"
#include "IABParserAPI.h"
#include "IABRendererAPI.h"
using namespace SMPTE::ImmersiveAudioBitstream;
#endif

namespace Dcp {

namespace {

// Lautsprechername (DTS-Konfiguration) -> WAVE_FORMAT_EXTENSIBLE-Kanalbit
quint32 speakerBit(const QString &name)
{
    static const QHash<QString, quint32> bits = {
        {"L", 0x1},       {"R", 0x2},       {"C", 0x4},       {"LFE", 0x8},
        {"LRS", 0x10},    {"RRS", 0x20},    {"LS", 0x200},    {"RS", 0x400},
        {"LSS", 0x200},   {"RSS", 0x400},   {"LFH", 0x1000},  {"RFH", 0x4000},
        {"LRH", 0x8000},  {"RRH", 0x20000},
    };
    return bits.value(name, 0);
}

// Immersive Audio Data Element: 06.0e.2b.34.01.02.01.xx.0e.09.06.01.00.00.00.01
bool isIabElement(const unsigned char *k)
{
    static const unsigned char head[4] = {0x06, 0x0e, 0x2b, 0x34};
    return std::memcmp(k, head, 4) == 0 && k[4] == 0x01 && k[5] == 0x02 && k[8] == 0x0e && k[9] == 0x09 && k[10] == 0x06
           && k[11] == 0x01;
}

} // namespace

bool iabAvailable()
{
#ifdef LUMEN_HAVE_IAB
    return true;
#else
    return false;
#endif
}

QList<IabLayout> iabLayouts()
{
    return {
        {QStringLiteral("7.1.4"), LTR("7.1.4 (Höhenlautsprecher)")},
        {QStringLiteral("5.1.4"), LTR("5.1.4 (Höhenlautsprecher)")},
        {QStringLiteral("7.1"), QStringLiteral("7.1")},
        {QStringLiteral("5.1"), QStringLiteral("5.1")},
        {QStringLiteral("2.0"), LTR("Stereo")},
    };
}

QString defaultIabLayout()
{
    return QStringLiteral("7.1.4");
}

struct IabDecoder::Impl
{
    explicit Impl(const StreamSpec &spec) : reader(spec, isIabElement) {}
    EssenceReader reader;
    QString error;
    int channels = 0, sampleRate = 48000, frameSamples = 0, errors = 0;
    quint32 mask = 0;
    QStringList names;
#ifdef LUMEN_HAVE_IAB
    RenderUtils::IRendererConfigurationFile *config = nullptr;
    IABRendererInterface *renderer = nullptr;
    IABParserInterface *parser = nullptr;
    int rendererChannels = 0, maxSamples = 0;
    std::vector<std::vector<IABSampleType>> planes;
    std::vector<IABSampleType *> planePtr;
    std::vector<int> order; // Ausgabekanal (WAV-Reihenfolge) -> Renderer-Ausgang
#endif
};

IabDecoder::IabDecoder(const StreamSpec &spec, const QString &layout)
    : d(new Impl(spec))
{
#ifndef LUMEN_HAVE_IAB
    Q_UNUSED(layout)
    d->error = LTR("Lumen wurde ohne IAB-Renderer gebaut");
#else
    if (!d->reader.ok()) {
        d->error = LTR("Keine IAB-Frames in der Spurdatei");
        return;
    }
    QFile cfg(QStringLiteral(":/iab/%1.cfg").arg(layout));
    if (!cfg.open(QIODevice::ReadOnly)) {
        d->error = LTR("Unbekanntes Lautsprecherlayout %1").arg(layout);
        return;
    }
    const QByteArray text = cfg.readAll();
    d->config = RenderUtils::IRendererConfigurationFile::FromBuffer(text.constData());
    if (!d->config) {
        d->error = LTR("Renderer-Konfiguration %1 ungültig").arg(layout);
        return;
    }
    d->renderer = IABRendererInterface::Create(*d->config);
    d->parser = IABParserInterface::Create();
    if (!d->renderer || !d->parser) {
        d->error = LTR("IAB-Renderer konnte nicht erzeugt werden");
        return;
    }
    d->rendererChannels = int(d->renderer->GetOutputChannelCount());
    d->maxSamples = int(d->renderer->GetMaxOutputSampleCount());
    d->planes.assign(size_t(d->rendererChannels), std::vector<IABSampleType>(size_t(d->maxSamples)));
    for (auto &p : d->planes)
        d->planePtr.push_back(p.data());

    // Kanalreihenfolge wie WAVE_FORMAT_EXTENSIBLE (aufsteigende Kanalbits)
    std::vector<std::pair<quint32, std::pair<int, QString>>> outs;
    for (const auto &[name, index] : d->config->GetSpeakerNameToOutputIndexMap()) {
        const QString n = QString::fromStdString(name);
        const quint32 bit = speakerBit(n);
        if (bit && index >= 0 && index < d->rendererChannels)
            outs.push_back({bit, {index, n}});
    }
    std::sort(outs.begin(), outs.end());
    for (const auto &o : outs) {
        if (d->mask & o.first)
            continue;
        d->mask |= o.first;
        d->order.push_back(o.second.first);
        d->names << o.second.second;
    }
    d->channels = int(d->order.size());

    // Erstes Frame: Abtastrate und Frame-Länge
    QByteArray first = d->reader.element(0);
    if (first.isEmpty() || d->parser->ParseIABFrame(first.data(), uint32_t(first.size())) != kIABNoError) {
        d->error = LTR("Erstes IAB-Frame nicht lesbar (Schlüssel?)");
        return;
    }
    d->sampleRate = d->parser->GetSampleRate() == kIABSampleRate_96000Hz ? 96000 : 48000;
    d->frameSamples = int(d->parser->GetFrameSampleCount());
    if (d->frameSamples <= 0 || d->frameSamples > d->maxSamples) {
        d->error = LTR("IAB-Frame-Länge %1 nicht unterstützt").arg(d->frameSamples);
        d->frameSamples = 0;
    }
#endif
}

IabDecoder::~IabDecoder()
{
#ifdef LUMEN_HAVE_IAB
    if (d->parser)
        IABParserInterface::Delete(d->parser);
    if (d->renderer)
        IABRendererInterface::Delete(d->renderer);
    delete d->config;
#endif
    delete d;
}

bool IabDecoder::ok() const { return d->error.isEmpty() && d->frameSamples > 0 && d->channels > 0; }
QString IabDecoder::error() const { return d->error; }
int IabDecoder::frames() const { return d->reader.count(); }
int IabDecoder::channels() const { return d->channels; }
quint32 IabDecoder::channelMask() const { return d->mask; }
QStringList IabDecoder::channelNames() const { return d->names; }
int IabDecoder::sampleRate() const { return d->sampleRate; }
int IabDecoder::frameSamples() const { return d->frameSamples; }
int IabDecoder::errors() const { return d->errors; }

bool IabDecoder::render(int frame, float *out)
{
    const size_t total = size_t(d->frameSamples) * size_t(d->channels);
    std::fill(out, out + total, 0.0f);
#ifdef LUMEN_HAVE_IAB
    if (!ok())
        return false;
    auto fail = [this, frame](const char *step, int code) {
        if (d->errors++ < 3)
            qWarning("Lumen: IAB-Frame %d: %s fehlgeschlagen (Code %d)", frame, step, code);
        return false;
    };
    QByteArray data = d->reader.element(frame);
    if (data.isEmpty())
        return fail("Lesen", 0);
    iabError e = d->parser->ParseIABFrame(data.data(), uint32_t(data.size()));
    if (e != kIABNoError)
        return fail("Parsen", int(e));
    const IABFrameInterface *f = nullptr;
    e = d->parser->GetIABFrame(f);
    if (e != kIABNoError || !f)
        return fail("GetIABFrame", int(e));
    IABRenderedOutputSampleCountType rendered = 0;
    e = d->renderer->RenderIABFrame(*f, d->planePtr.data(), IABRenderedOutputChannelCountType(d->rendererChannels),
                                    IABRenderedOutputSampleCountType(d->frameSamples), rendered); // genau die Frame-Länge
    // Warnungen (z. B. Stereo ohne LFE-Kanal: Bett-LFE entfällt) liefern gültige Ausgabe
    if (e != kIABNoError && e != kIABRendererNoLFEInConfigForBedLFEWarning && e != kIABRendererNoLFEInConfigForRemapLFEWarning)
        return fail("Rendern", int(e));
    const int n = std::min(int(rendered), d->frameSamples);
    for (int c = 0; c < d->channels; ++c) {
        const IABSampleType *src = d->planes[size_t(d->order[size_t(c)])].data();
        for (int i = 0; i < n; ++i)
            out[size_t(i) * size_t(d->channels) + size_t(c)] = src[i];
    }
    return true;
#else
    Q_UNUSED(frame)
    return false;
#endif
}

// --------------------------------------------------------------------------
// mpv-Stream: RIFF/RF64-WAV (WAVE_FORMAT_EXTENSIBLE, IEEE-Float)
// --------------------------------------------------------------------------

namespace {

struct IabSpec
{
    StreamSpec spec;
    QString layout;
};

std::mutex g_iabMutex;
QHash<int, IabSpec> g_iabStreams;
int g_iabNext = 1;

struct IabStream
{
    explicit IabStream(const IabSpec &s) : decoder(s.spec, s.layout) {}
    IabDecoder decoder;
    QByteArray header;
    qint64 frameBytes = 0, dataBytes = 0, pos = 0;
    int cachedFrame = -1;
    std::vector<float> cache;

    void buildHeader()
    {
        const int ch = decoder.channels();
        frameBytes = qint64(decoder.frameSamples()) * ch * 4;
        dataBytes = frameBytes * decoder.frames();
        const bool rf64 = dataBytes + 80 > 0xffffffffLL;
        QByteArray h;
        auto u16 = [&](quint16 v) { char b[2]; qToLittleEndian(v, b); h.append(b, 2); };
        auto u32 = [&](quint32 v) { char b[4]; qToLittleEndian(v, b); h.append(b, 4); };
        auto u64 = [&](quint64 v) { char b[8]; qToLittleEndian(v, b); h.append(b, 8); };
        const qint64 headerSize = rf64 ? 12 + 36 + 48 + 8 : 12 + 48 + 8;
        h += rf64 ? "RF64" : "RIFF";
        u32(rf64 ? 0xffffffffu : quint32(headerSize - 8 + dataBytes));
        h += "WAVE";
        if (rf64) {
            h += "ds64";
            u32(28);
            u64(quint64(headerSize - 8 + dataBytes));
            u64(quint64(dataBytes));
            u64(quint64(decoder.frameSamples()) * quint64(decoder.frames()));
            u32(0);
        }
        h += "fmt ";
        u32(40);
        u16(0xfffe);                    // WAVE_FORMAT_EXTENSIBLE
        u16(quint16(ch));
        u32(quint32(decoder.sampleRate()));
        u32(quint32(decoder.sampleRate() * ch * 4));
        u16(quint16(ch * 4));
        u16(32);
        u16(22);
        u16(32);                        // gültige Bits
        u32(decoder.channelMask());
        // KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
        h.append(QByteArray::fromHex("0300000000001000800000aa00389b71"));
        h += "data";
        u32(rf64 ? 0xffffffffu : quint32(dataBytes));
        header = h;
    }

    qint64 size() const { return header.size() + dataBytes; }

    int64_t read(char *buf, uint64_t n)
    {
        qint64 done = 0;
        while (done < qint64(n) && pos < size()) {
            if (pos < header.size()) {
                const qint64 c = std::min<qint64>(qint64(n) - done, header.size() - pos);
                std::memcpy(buf + done, header.constData() + pos, size_t(c));
                pos += c;
                done += c;
                continue;
            }
            const qint64 rel = pos - header.size();
            const int frame = int(rel / frameBytes);
            const qint64 off = rel % frameBytes;
            if (frame != cachedFrame) {
                cache.resize(size_t(frameBytes / 4));
                decoder.render(frame, cache.data());
                cachedFrame = frame;
            }
            const qint64 c = std::min<qint64>(qint64(n) - done, frameBytes - off);
            std::memcpy(buf + done, reinterpret_cast<const char *>(cache.data()) + off, size_t(c));
            pos += c;
            done += c;
        }
        return done;
    }
};

int openIab(void *, char *uri, mpv_stream_cb_info *info)
{
    const QString u = QString::fromUtf8(uri);
    const int id = u.section(QStringLiteral("://"), 1).toInt();
    IabSpec spec;
    {
        std::lock_guard<std::mutex> lock(g_iabMutex);
        if (!g_iabStreams.contains(id))
            return MPV_ERROR_LOADING_FAILED;
        spec = g_iabStreams.value(id);
    }
    auto *s = new IabStream(spec);
    if (!s->decoder.ok()) {
        qWarning("Lumen: IAB: %s", qPrintable(s->decoder.error()));
        delete s;
        return MPV_ERROR_LOADING_FAILED;
    }
    s->buildHeader();
    info->cookie = s;
    info->read_fn = [](void *c, char *buf, uint64_t n) -> int64_t { return static_cast<IabStream *>(c)->read(buf, n); };
    info->seek_fn = [](void *c, int64_t off) -> int64_t {
        auto *st = static_cast<IabStream *>(c);
        if (off < 0 || off > st->size())
            return MPV_ERROR_GENERIC;
        st->pos = off;
        return off;
    };
    info->size_fn = [](void *c) -> int64_t { return static_cast<IabStream *>(c)->size(); };
    info->close_fn = [](void *c) { delete static_cast<IabStream *>(c); };
    return 0;
}

} // namespace

int registerIabStream(const StreamSpec &spec, const QString &layout)
{
    std::lock_guard<std::mutex> lock(g_iabMutex);
    const int id = g_iabNext++;
    g_iabStreams.insert(id, {spec, layout});
    return id;
}

QString iabUrl(int id)
{
    return QStringLiteral("lumeniab://%1").arg(id);
}

void attachIabProtocol(mpv_handle *mpv)
{
    mpv_stream_cb_add_ro(mpv, "lumeniab", nullptr, openIab);
}

} // namespace Dcp
