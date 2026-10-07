// TsRemap: ein Strom, dessen Format sich von Playlist zu Playlist ändert, bekommt eine eigene PID –
// in den Paketen und in der Programmtabelle (PMT), deren Prüfsumme danach wieder stimmen muss.
//
//     tsremap_test
//
// Baut zwei "Clips" mit derselben PMT-Form (so lang, dass sie über zwei Pakete reicht): im ersten
// ist 0x1100 AC-3, im zweiten DTS-HD. Verarbeitet wird in Blöcken, die die PMT mittendurch teilen.
#include "../src/TsRemap.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

uint32_t crc32(const uint8_t *d, size_t n)
{
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < n; ++i) {
        crc ^= uint32_t(d[i]) << 24;
        for (int b = 0; b < 8; ++b)
            crc = (crc & 0x80000000u) ? (crc << 1) ^ 0x04C11DB7u : crc << 1;
    }
    return crc;
}

struct Es
{
    uint8_t type;
    uint16_t pid;
};

std::vector<uint8_t> pmtSection(const std::vector<Es> &streams)
{
    std::vector<uint8_t> s{0x02, 0, 0, 0x00, 0x01, 0xc1, 0x00, 0x00, 0xf0, 0x01, 0xf0, 0x04, 0x05, 0x02, 'H', 'D'};
    for (const Es &e : streams) {
        // jeder Eintrag mit 40 Byte Beschreibung: fünf Einträge reichen über ein Paket hinaus
        s.insert(s.end(), {e.type, uint8_t(0xe0 | (e.pid >> 8)), uint8_t(e.pid & 0xff), 0xf0, 40});
        for (int i = 0; i < 40; ++i)
            s.push_back(uint8_t(0x30 + (i % 10)));
    }
    const size_t len = s.size() + 4 - 3;
    s[1] = uint8_t(0xb0 | (len >> 8));
    s[2] = uint8_t(len & 0xff);
    const uint32_t crc = crc32(s.data(), s.size());
    s.insert(s.end(), {uint8_t(crc >> 24), uint8_t(crc >> 16), uint8_t(crc >> 8), uint8_t(crc)});
    return s;
}

void packet(std::vector<uint8_t> &out, uint16_t pid, bool start, const uint8_t *payload, size_t n, uint8_t cc)
{
    uint8_t p[192];
    std::memset(p, 0xff, sizeof p);
    p[0] = p[1] = p[2] = p[3] = 0;
    p[4] = 0x47;
    p[5] = uint8_t((start ? 0x40 : 0) | (pid >> 8));
    p[6] = uint8_t(pid & 0xff);
    p[7] = uint8_t(0x10 | (cc & 0x0f));
    std::memcpy(p + 8, payload, n);
    out.insert(out.end(), p, p + 192);
}

// PAT-freier Clip: PMT (über mehrere Pakete), dann je ein Paket jedes Stroms
std::vector<uint8_t> clip(const std::vector<Es> &streams)
{
    std::vector<uint8_t> out;
    std::vector<uint8_t> sec = pmtSection(streams);
    sec.insert(sec.begin(), 0x00); // Zeigerfeld
    uint8_t cc = 0;
    for (size_t off = 0; off < sec.size(); off += 184)
        packet(out, 0x0100, off == 0, sec.data() + off, std::min<size_t>(184, sec.size() - off), cc++);
    for (const Es &e : streams) {
        const uint8_t pes[] = {0, 0, 1, 0xbd, 0, 0, 0x80, 0, 0};
        packet(out, e.pid, true, pes, sizeof pes, 0);
    }
    return out;
}

// PMT-Abschnitt aus den Paketen wieder zusammensetzen
std::vector<uint8_t> section(const std::vector<uint8_t> &ts)
{
    std::vector<uint8_t> sec;
    for (size_t off = 0; off + 192 <= ts.size(); off += 192) {
        const uint8_t *p = ts.data() + off + 4;
        if ((((p[1] & 0x1f) << 8) | p[2]) != 0x0100)
            continue;
        sec.insert(sec.end(), p + 4 + ((p[1] & 0x40) ? 1 : 0), p + 188);
    }
    if (sec.size() >= 3)
        sec.resize(std::min<size_t>(sec.size(), 3 + (((sec[1] & 0x0f) << 8) | sec[2])));
    return sec;
}

std::vector<uint16_t> pids(const std::vector<uint8_t> &sec)
{
    std::vector<uint16_t> out;
    size_t pos = 12 + (((sec[10] & 0x0f) << 8) | sec[11]);
    while (pos + 5 <= sec.size() - 4) {
        out.push_back(uint16_t(((sec[pos + 1] & 0x1f) << 8) | sec[pos + 2]));
        pos += 5 + (((sec[pos + 3] & 0x0f) << 8) | sec[pos + 4]);
    }
    return out;
}

std::vector<uint16_t> packetPids(const std::vector<uint8_t> &ts)
{
    std::vector<uint16_t> out;
    for (size_t off = 0; off + 192 <= ts.size(); off += 192) {
        const uint8_t *p = ts.data() + off + 4;
        const uint16_t pid = uint16_t(((p[1] & 0x1f) << 8) | p[2]);
        if (pid != 0x0100)
            out.push_back(pid);
    }
    return out;
}

bool check(const char *what, bool ok)
{
    std::printf("%s  %s\n", ok ? "OK  " : "FAIL", what);
    return ok;
}

} // namespace

int main()
{
    const std::vector<Es> menu{{0x1b, 0x1011}, {0x81, 0x1100}, {0x90, 0x1200}, {0x90, 0x1201}, {0x91, 0x1400}};
    const std::vector<Es> film{{0x1b, 0x1011}, {0x86, 0x1100}, {0x81, 0x1101}, {0x90, 0x1200}, {0x90, 0x1201}};
    auto list = [](const std::vector<Es> &v) {
        QList<TsRemap::Stream> l;
        for (const Es &e : v)
            l.append({e.pid, e.type});
        return l;
    };
    // in Blöcken von einem Paket: die PMT wird mittendurch geteilt
    auto run = [](TsRemap &r, std::vector<uint8_t> ts) {
        for (size_t off = 0; off < ts.size(); off += 192)
            r.process(ts.data() + off, 192);
        return ts;
    };

    bool ok = true;
    TsRemap r;
    r.setStreams(list(menu));
    const std::vector<uint8_t> in1 = clip(menu);
    ok &= check("PMT reicht über mehr als ein Paket", section(in1).size() > 184);
    ok &= check("erster Clip bleibt, wie er ist", run(r, in1) == in1);

    r.setStreams(list(film));
    const std::vector<uint8_t> out2 = run(r, clip(film));
    const std::vector<uint8_t> sec2 = section(out2);
    ok &= check("DTS-HD auf 0x1100 bekommt 0x1180, der Rest bleibt",
                pids(sec2) == std::vector<uint16_t>({0x1011, 0x1180, 0x1101, 0x1200, 0x1201}));
    ok &= check("Prüfsumme der umgeschriebenen PMT stimmt", crc32(sec2.data(), sec2.size()) == 0);
    ok &= check("Pakete tragen die neue PID", packetPids(out2) == std::vector<uint16_t>({0x1011, 0x1180, 0x1101, 0x1200, 0x1201}));
    ok &= check("map/unmap", r.map(0x1100) == 0x1180 && r.unmap(0x1180) == 0x1100 && r.map(0x1101) == 0x1101);

    // zurück ins Menü: sein Ton hat eine Playlist lang gefehlt und bekommt eine neue PID (im Demuxer
    // kann von ihm noch ein abgeschnittenes Paket liegen); Bild, Untertitel und Menügrafik bleiben
    r.setStreams(list(menu));
    const std::vector<uint8_t> sec3 = section(run(r, in1));
    ok &= check("zurück im Menü: neuer Ton 0x1181, der Rest bleibt",
                pids(sec3) == std::vector<uint16_t>({0x1011, 0x1181, 0x1200, 0x1201, 0x1400}) && crc32(sec3.data(), sec3.size()) == 0);
    // noch einmal der Film: auch seine Tonspuren haben gefehlt
    r.setStreams(list(film));
    const std::vector<uint8_t> sec4 = section(run(r, clip(film)));
    ok &= check("der Film noch einmal: Ton 0x1182 und 0x1183",
                pids(sec4) == std::vector<uint16_t>({0x1011, 0x1182, 0x1183, 0x1200, 0x1201}) && crc32(sec4.data(), sec4.size()) == 0);
    // derselbe Film gleich noch einmal (nächster Clip derselben Playlist): nichts ändert sich
    r.setStreams(list(film));
    ok &= check("nächster Clip mit denselben Strömen: dieselben PIDs", section(run(r, clip(film))) == sec4);
    // sind die eigenen PIDs eines Bereichs aufgebraucht, bleibt es bei der bekannten
    for (int i = 0; i < 80; ++i) {
        r.setStreams(list(menu));
        r.setStreams(list(film));
    }
    const std::vector<uint8_t> sec5 = section(run(r, clip(film)));
    ok &= check("nach 80 Wechseln: PMT weiter gültig", crc32(sec5.data(), sec5.size()) == 0 && pids(sec5).at(0) == 0x1011
                                                           && (pids(sec5).at(1) & 0xff00) == 0x1100);
    return ok ? 0 : 1;
}
