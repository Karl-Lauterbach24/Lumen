#include "CastAudio.h"

#include "AudioTap.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
const int kHeader = 24;
const int kFrameBytes = AudioTap::kChannels * 2;
enum : quint32 { TapData = 1, TapPause = 2, TapResume = 3, TapReset = 4 };
}

qint64 CastAudio::toSample(qint64 timeUs) const
{
    return qint64(std::llround((double(timeUs) / 1e6 + m_offset + kDelay) * AudioTap::kRate));
}

void CastAudio::feed(const char *data, int size)
{
    m_in.append(data, size);
    int pos = 0;
    while (m_in.size() - pos >= kHeader) {
        const char *h = m_in.constData() + pos;
        if (memcmp(h, "LAO1", 4) != 0) {
            // aus dem Tritt (sollte nicht vorkommen): nächsten Satzanfang suchen
            const int next = m_in.indexOf("LAO1", pos + 1);
            pos = next < 0 ? m_in.size() - 3 : next;
            continue;
        }
        quint32 type, samples;
        qint64 timeUs;
        memcpy(&type, h + 4, 4);
        memcpy(&timeUs, h + 8, 8);
        memcpy(&samples, h + 16, 4);
        const qint64 payload = type == TapData ? qint64(samples) * kFrameBytes : 0;
        if (payload > 16 * 1024 * 1024) {
            pos += 4;
            continue;
        }
        if (m_in.size() - pos < kHeader + payload)
            break;
        record(type, timeUs, h + kHeader, int(samples));
        pos += kHeader + int(payload);
    }
    m_in.remove(0, pos);
}

// Alles ab `sample` verwerfen (ein Block, der darüber hinausreicht, wird gekürzt)
void CastAudio::cutAt(qint64 sample)
{
    while (!m_blocks.empty() && m_blocks.back().start >= sample)
        m_blocks.pop_back();
    if (!m_blocks.empty()) {
        Block &b = m_blocks.back();
        const qint64 end = b.start + b.pcm.size() / kFrameBytes;
        if (end > sample)
            b.pcm.truncate(int(sample - b.start) * kFrameBytes);
    }
}

void CastAudio::record(quint32 type, qint64 timeUs, const char *payload, int samples)
{
    const qint64 at = toSample(timeUs);
    switch (type) {
    case TapData: {
        if (samples <= 0)
            return;
        qint64 start = at;
        // schließt an den vorigen Block an (Rundung der Gerätesimulation): lückenlos anhängen
        if (m_end >= 0 && std::llabs(start - m_end) < AudioTap::kRate / 200)
            start = m_end;
        else if (m_end >= 0 && start < m_end)
            cutAt(start); // überlappt Älteres: das Neue gilt
        m_blocks.push_back(Block{start, QByteArray(payload, samples * kFrameBytes)});
        m_end = start + samples;
        break;
    }
    case TapPause:
        if (m_pausedAt < 0)
            m_pausedAt = at;
        break;
    case TapResume:
        if (m_pausedAt >= 0) {
            // was beim Anhalten noch im Gerät lag, spielt erst jetzt weiter
            const qint64 shift = at - m_pausedAt;
            std::deque<Block> moved;
            for (Block &b : m_blocks) {
                const qint64 end = b.start + b.pcm.size() / kFrameBytes;
                if (end <= m_pausedAt) {
                    moved.push_back(std::move(b));
                } else if (b.start >= m_pausedAt) {
                    b.start += shift;
                    moved.push_back(std::move(b));
                } else {
                    // Block reicht über den Haltepunkt: den Rest abtrennen und verschieben
                    const int keep = int(m_pausedAt - b.start) * kFrameBytes;
                    Block rest{m_pausedAt + shift, b.pcm.mid(keep)};
                    b.pcm.truncate(keep);
                    moved.push_back(std::move(b));
                    moved.push_back(std::move(rest));
                }
            }
            m_blocks.swap(moved);
            if (m_end >= 0)
                m_end += shift;
            m_pausedAt = -1;
        }
        break;
    case TapReset:
        // Sprung, Dateiwechsel, Ende: was das Gerät noch nicht gespielt hat, entfällt
        cutAt(m_pausedAt >= 0 ? std::min(at, m_pausedAt) : at);
        m_end = -1;
        m_pausedAt = -1;
        break;
    default:
        break;
    }
}

int CastAudio::take(qint64 start, qint16 *out, int samples)
{
    memset(out, 0, size_t(samples) * kFrameBytes);
    // angehalten: nichts ab dem Haltepunkt
    const qint64 limit = m_pausedAt >= 0 ? std::min(start + samples, m_pausedAt) : start + samples;
    int real = 0;
    for (const Block &b : m_blocks) {
        const qint64 end = b.start + b.pcm.size() / kFrameBytes;
        const qint64 from = std::max(b.start, start);
        const qint64 to = std::min(end, limit);
        if (to <= from)
            continue;
        memcpy(reinterpret_cast<char *>(out) + (from - start) * kFrameBytes,
               b.pcm.constData() + (from - b.start) * kFrameBytes, size_t(to - from) * kFrameBytes);
        real += int(to - from);
    }
    // Abgespieltes entfernen (angehalten: nur bis zum Haltepunkt)
    while (!m_blocks.empty()) {
        const Block &b = m_blocks.front();
        if (b.start + b.pcm.size() / kFrameBytes <= limit)
            m_blocks.pop_front();
        else
            break;
    }
    return real;
}

qint64 CastAudio::queued() const
{
    qint64 n = 0;
    for (const Block &b : m_blocks)
        n += b.pcm.size() / kFrameBytes;
    return n;
}
