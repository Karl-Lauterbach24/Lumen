#include "TsRemap.h"

namespace {

constexpr int kPmtPid = 0x0100; // auf jeder Blu-ray

uint32_t crcStep(uint32_t crc, uint8_t b)
{
    crc ^= uint32_t(b) << 24;
    for (int i = 0; i < 8; ++i)
        crc = (crc & 0x80000000u) ? (crc << 1) ^ 0x04C11DB7u : crc << 1;
    return crc;
}

} // namespace

void TsRemap::setStreams(const QList<Stream> &streams)
{
    // Ton: AC-3, DTS und ihre Erweiterungen, LPCM, MPEG; dazu der zweite Ton (0xa1, 0xa2)
    const auto isAudio = [](uint8_t t) { return (t >= 0x80 && t <= 0x86) || t == 0x03 || t == 0x04 || t == 0xa1 || t == 0xa2; };
    m_map.clear();
    m_back.clear();
    QSet<uint16_t> live;
    for (const Stream &s : streams) {
        const auto known = m_announced.constFind(s.pid);
        if (known == m_announced.constEnd()) {
            m_announced.insert(s.pid, s.type);
            live.insert(s.pid);
            continue;
        }
        const uint32_t key = uint32_t(s.pid) << 8 | s.type;
        // die PID, unter der der Demuxer diesen Strom in diesem Format kennt (0 = noch keine)
        uint16_t out = m_alias.value(key, known.value() == s.type ? s.pid : 0);
        if (!out || (isAudio(s.type) && !m_live.contains(out))) {
            int &used = m_used[uint8_t(s.pid >> 8)];
            if (used < 0x7f) {
                out = uint16_t((s.pid & 0xff00) | (0x80 + used++));
                m_alias.insert(key, out);
                m_announced.insert(out, s.type);
            } else if (!out) {
                out = s.pid; // kein Platz mehr: lieber die alte PID als gar keine
            }
        }
        live.insert(out);
        if (out != s.pid) {
            m_map.insert(s.pid, out);
            m_back.insert(out, s.pid);
        }
    }
    m_live = live;
}

// Ein Byte des PMT-Abschnitts: Länge und Einträge mitlesen, das untere PID-Byte ersetzen, die
// Prüfsumme über das Geschriebene rechnen und an ihrer Stelle einsetzen
void TsRemap::pmtByte(uint8_t &b)
{
    const int pos = m_pos++;
    if (pos == 0) {
        m_crc = 0xffffffffu;
        m_length = 0;
        m_es = -1;
        m_dirty = false;
        if (b != 0x02) { // keine PMT
            m_pos = -1;
            return;
        }
    } else if (pos == 1) {
        m_length = (b & 0x0f) << 8;
    } else if (pos == 2) {
        m_length = 3 + (m_length | b);
        if (m_length < 16 || m_length > 1024) {
            m_pos = -1;
            return;
        }
    } else if (pos == 10) {
        m_es = (b & 0x0f) << 8;
    } else if (pos == 11) {
        m_es = 12 + (m_es | b);
    }
    if (pos >= 3 && pos >= m_length - 4) {
        // Prüfsumme: nur ersetzen, wenn der Abschnitt geändert wurde
        if (m_dirty)
            b = uint8_t(m_crc >> (8 * (m_length - 1 - pos)));
        if (pos == m_length - 1)
            m_pos = -1;
        return;
    }
    if (pos >= 12 && m_es >= 12 && pos >= m_es) {
        const int at = pos - m_es;
        if (at == 1) {
            m_pidHigh = b & 0x1f;
        } else if (at == 2) {
            const uint16_t pid = uint16_t(m_pidHigh << 8 | b);
            const uint16_t out = map(pid);
            if (out != pid) {
                b = uint8_t(out & 0xff);
                m_dirty = true;
            }
        } else if (at == 3) {
            m_esInfo = (b & 0x0f) << 8;
        } else if (at == 4) {
            m_es = pos + 1 + (m_esInfo | b); // der nächste Eintrag
        }
    }
    m_crc = crcStep(m_crc, b);
}

void TsRemap::process(uint8_t *data, size_t size)
{
    if (m_map.isEmpty() && m_pos < 0)
        return;
    for (size_t off = 0; off + 192 <= size; off += 192) {
        uint8_t *ts = data + off + 4;
        if (ts[0] != 0x47)
            continue;
        const uint16_t pid = uint16_t((ts[1] & 0x1f) << 8 | ts[2]);
        if (pid == kPmtPid) {
            const int afc = (ts[3] >> 4) & 3;
            int p = 4 + ((afc & 2) ? 1 + ts[4] : 0);
            if (!(afc & 1) || p >= 188)
                continue;
            if (ts[1] & 0x40) {
                // Zeigerfeld: so viele Bytes gehören noch zum vorigen Abschnitt
                int tail = ts[p++];
                for (; tail > 0 && p < 188; --tail, ++p)
                    if (m_pos >= 0)
                        pmtByte(ts[p]);
                m_pos = m_map.isEmpty() ? -1 : 0;
            }
            for (; p < 188 && m_pos >= 0; ++p)
                pmtByte(ts[p]);
            continue;
        }
        const auto it = m_map.constFind(pid);
        if (it != m_map.constEnd()) {
            ts[1] = uint8_t((ts[1] & 0xe0) | (it.value() >> 8));
            ts[2] = uint8_t(it.value() & 0xff);
        }
    }
}
