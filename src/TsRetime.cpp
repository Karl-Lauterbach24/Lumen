#include "TsRetime.h"

namespace {

constexpr uint64_t kMask = (uint64_t(1) << 33) - 1;

// PTS/DTS: 5 Byte – 4 Bit Kennung, dann 3 + 15 + 15 Bit Zeit, jeweils gefolgt von einem Markierungsbit
void shiftStamp(uint8_t *t, uint64_t offset)
{
    uint64_t v = uint64_t((t[0] >> 1) & 7) << 30 | uint64_t(t[1]) << 22 | uint64_t(t[2] >> 1) << 15 | uint64_t(t[3]) << 7
                 | uint64_t(t[4] >> 1);
    v = (v + offset) & kMask;
    t[0] = uint8_t((t[0] & 0xf1) | ((v >> 29) & 0x0e));
    t[1] = uint8_t(v >> 22);
    t[2] = uint8_t(((v >> 14) & 0xfe) | 1);
    t[3] = uint8_t(v >> 7);
    t[4] = uint8_t(((v << 1) & 0xfe) | 1);
}

} // namespace

TsStamps scanM2ts(const uint8_t *data, size_t size, int pid)
{
    TsStamps st;
    for (size_t pos = 0; pos + 192 <= size; pos += 192) {
        const uint8_t *ts = data + pos + 4;
        if (ts[0] != 0x47 || !(ts[1] & 0x40) || (((ts[1] & 0x1f) << 8) | ts[2]) != pid)
            continue;
        const int afc = (ts[3] >> 4) & 3;
        const int payload = 4 + ((afc & 2) ? 1 + ts[4] : 0);
        if (!(afc & 1) || payload + 14 > 188)
            continue;
        const uint8_t *p = ts + payload;
        if (p[0] != 0 || p[1] != 0 || p[2] != 1 || (p[6] & 0xc0) != 0x80 || !(p[7] & 0x80))
            continue;
        const uint8_t *t = p + 9;
        const int64_t pts = int64_t((t[0] >> 1) & 7) << 30 | int64_t(t[1]) << 22 | int64_t(t[2] >> 1) << 15 | int64_t(t[3]) << 7
                            | int64_t(t[4] >> 1);
        if (st.first < 0)
            st.first = pts;
        if (pts > st.max)
            st.max = pts;
    }
    return st;
}

void retimeM2ts(uint8_t *data, size_t size, int64_t offset)
{
    const uint64_t off = uint64_t(offset) & kMask;
    if (!off)
        return;
    for (size_t pos = 0; pos + 192 <= size; pos += 192) {
        uint8_t *ts = data + pos + 4;
        if (ts[0] != 0x47)
            continue;
        const int afc = (ts[3] >> 4) & 3;
        int payload = 4;
        if (afc & 2) {
            const int afLen = ts[4];
            if (afLen >= 7 && (ts[5] & 0x10)) { // PCR: 33 Bit Basis, 6 Bit Reserve, 9 Bit Erweiterung
                uint64_t pcr = uint64_t(ts[6]) << 25 | uint64_t(ts[7]) << 17 | uint64_t(ts[8]) << 9 | uint64_t(ts[9]) << 1
                               | uint64_t(ts[10] >> 7);
                pcr = (pcr + off) & kMask;
                ts[6] = uint8_t(pcr >> 25);
                ts[7] = uint8_t(pcr >> 17);
                ts[8] = uint8_t(pcr >> 9);
                ts[9] = uint8_t(pcr >> 1);
                ts[10] = uint8_t((ts[10] & 0x7f) | ((pcr & 1) << 7));
            }
            payload += 1 + afLen;
        }
        // Beginn eines PES-Pakets mit Kopf: 00 00 01, Stromkennung, Länge, '10…', Flags
        if (!(ts[1] & 0x40) || !(afc & 1) || payload + 14 > 188)
            continue;
        uint8_t *p = ts + payload;
        if (p[0] != 0 || p[1] != 0 || p[2] != 1 || (p[6] & 0xc0) != 0x80)
            continue;
        if (p[7] & 0x80)
            shiftStamp(p + 9, off);
        if ((p[7] & 0xc0) == 0xc0 && payload + 19 <= 188)
            shiftStamp(p + 14, off);
    }
}
