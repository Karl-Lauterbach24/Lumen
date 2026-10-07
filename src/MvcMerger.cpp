#include "MvcMerger.h"

#include <libbluray/bluray.h>
#include <libbluray/filesystem.h>

#include <QtDebug>
#include <algorithm>
#include <cstring>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

constexpr int kPacket = 192;              // M2TS: 4 Byte Arrival-Timestamp + 188 Byte TS
constexpr int kUnit = 6144;               // Aligned Unit (Entschlüsselungs-/Seek-Granularität)
constexpr int kChunk = kUnit * 32;        // Leseblock der abhängigen Ansicht (192 KiB)
constexpr int kMaxChunksPerFrame = 24;    // max. ~4,5 MiB Vorlauf je Bild, sonst ohne 2. Ansicht weiter
constexpr int64_t kPtsTolerance = 90 * 4; // 4 ms

inline uint32_t be32(const uint8_t *p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
inline uint16_t be16(const uint8_t *p) { return uint16_t(p[0] << 8 | p[1]); }

// Von libbluray per malloc() gelieferte Puffer mit dessen C-Laufzeit freigeben
// (Lumen kann eine andere CRT verwenden als die libbluray-DLL).
void freeBlurayBuffer(void *p)
{
#ifdef Q_OS_WIN
    // MSYS2-UCRT-Builds allozieren über ucrtbase; ist die DLL nicht geladen,
    // nutzt libbluray dieselbe CRT wie Lumen.
    using FreeFn = void (*)(void *);
    static const FreeFn crtFree = [] {
        if (HMODULE m = GetModuleHandleW(L"ucrtbase.dll"))
            if (auto f = reinterpret_cast<FreeFn>(reinterpret_cast<void *>(GetProcAddress(m, "free"))))
                return f;
        return static_cast<FreeFn>(::free);
    }();
    crtFree(p);
#else
    ::free(p);
#endif
}

struct TsPacket
{
    const uint8_t *p = nullptr; // Anfang des 192-Byte-Pakets
    uint16_t pid = 0;
    bool pusi = false;
    bool hasPcr = false;
    const uint8_t *payload = nullptr;
    int payloadLen = 0;
};

bool parseTs(const uint8_t *p, TsPacket &t)
{
    const uint8_t *ts = p + 4;
    if (ts[0] != 0x47)
        return false;
    t.p = p;
    t.pid = uint16_t((ts[1] & 0x1f) << 8 | ts[2]);
    t.pusi = ts[1] & 0x40;
    const int afc = (ts[3] >> 4) & 3;
    int off = 4;
    t.hasPcr = false;
    if (afc & 2) {
        const int afLen = ts[4];
        if (afLen > 0 && (ts[5] & 0x10))
            t.hasPcr = true;
        off += 1 + afLen;
    }
    if (!(afc & 1) || off >= 188) {
        t.payload = nullptr;
        t.payloadLen = 0;
    } else {
        t.payload = ts + off;
        t.payloadLen = 188 - off;
    }
    return true;
}

} // namespace

MvcMerger::MvcMerger(bluray *bd)
    : m_bd(bd)
{
}

MvcMerger::~MvcMerger()
{
    closeDependent();
}

// ---------------------------------------------------------------------------
// Playlist: SS-Subpfad (Typ 8) -> Clip der abhängigen Ansicht je PlayItem
// ---------------------------------------------------------------------------

bool MvcMerger::parsePlaylistSs(const QByteArray &mpls, uint32_t playlist)
{
    const auto *d = reinterpret_cast<const uint8_t *>(mpls.constData());
    const int n = mpls.size();
    if (n < 20 || std::memcmp(d, "MPLS", 4) != 0)
        return false;
    const uint32_t extPos = be32(d + 16);
    if (!extPos || extPos + 12 > uint32_t(n))
        return false;

    // ExtensionData: length, data_block_start, 3 Byte Reserve, Anzahl Einträge
    const uint32_t extLen = be32(d + extPos);
    if (!extLen)
        return false;
    const int entries = d[extPos + 11];
    for (int e = 0; e < entries; ++e) {
        const uint8_t *ent = d + extPos + 12 + e * 12;
        if (ent + 12 > d + n)
            break;
        const uint16_t id1 = be16(ent), id2 = be16(ent + 2);
        const uint32_t start = extPos + be32(ent + 4);
        if (id1 != 2 || id2 != 2 || start + 6 > uint32_t(n))
            continue;

        // SubPath_entries_extension
        uint32_t pos = start + 4;
        const int subPaths = be16(d + pos);
        pos += 2;
        for (int s = 0; s < subPaths && pos + 10 <= uint32_t(n); ++s) {
            const uint32_t spLen = be32(d + pos);
            const uint32_t spStart = pos + 4;
            const int type = d[spStart + 1];
            const int items = d[spStart + 5];
            uint32_t ip = spStart + 6;
            for (int i = 0; i < items && ip + 2 <= uint32_t(n); ++i) {
                const uint16_t itemLen = be16(d + ip);
                const uint8_t *it = d + ip + 2;
                if (ip + 2 + itemLen > uint32_t(n) || itemLen < 25)
                    break;
                if (type == 8) { // SS: abhängige Ansicht (MVC)
                    const QString clip = QString::fromLatin1(reinterpret_cast<const char *>(it), 5);
                    // clip(5) codec(4) flags(4) stc(1) in(4) out(4) sync_PlayItem_id(2)
                    const int syncItem = be16(it + 22);
                    m_depClip.insert(syncItem, clip);
                }
                ip += 2 + itemLen;
            }
            pos = spStart + spLen;
        }
    }
    if (!m_depClip.isEmpty())
        qInfo("Lumen 3D: Playlist %05u – MVC-Ansicht in %d Clip(s)", playlist, int(m_depClip.size()));
    return !m_depClip.isEmpty();
}

bool MvcMerger::setPlaylist(uint32_t playlist, QByteArray &out, bool complete)
{
    // Das letzte Bild wartet noch auf den nächsten Paketstart: jetzt ausgeben, solange seine
    // abhängige Datei offen ist
    if (m_active && complete)
        emitBase(out);
    m_basePes = PesAssembler();
    if (playlist == m_playlist) {
        if (m_active) {
            m_playItem = 0;
            openDependent(0);
        }
        return m_active;
    }
    m_playlist = playlist;
    closeDependent();
    m_depClip.clear();
    m_playItem = -1;
    m_active = false;
    m_basePes = PesAssembler();

    void *data = nullptr;
    int64_t size = 0;
    const QByteArray path = QStringLiteral("BDMV/PLAYLIST/%1.mpls").arg(playlist, 5, 10, QLatin1Char('0')).toLatin1();
    if (!bd_read_file(m_bd, path.constData(), &data, &size) || !data)
        return false;
    const QByteArray mpls(static_cast<const char *>(data), int(size));
    freeBlurayBuffer(data);

    if (!parsePlaylistSs(mpls, playlist))
        return false;

    if (BLURAY_TITLE_INFO *ti = bd_get_playlist_info(m_bd, playlist, 0)) {
        m_baseRight = ti->mvc_base_view_r_flag;
        if (ti->clip_count > 0 && ti->clips[0].video_stream_count > 0)
            m_basePid = ti->clips[0].video_streams[0].pid;
        bd_free_title_info(ti);
    }
    m_active = true;
    m_playItem = 0;
    openDependent(0);
    return true;
}

// ---------------------------------------------------------------------------
// Abhängige Ansicht: Datei + EP-Map
// ---------------------------------------------------------------------------

void MvcMerger::closeDependent()
{
    if (m_depFile) {
        m_depFile->close(m_depFile);
        m_depFile = nullptr;
    }
    m_ep.clear();
    m_depRaw.clear();
    m_depPes = PesAssembler();
    m_depQueue.clear();
    m_depEof = false;
    m_needSeek = true;
}

// CPI mit EP-Map ab cpiStart: Länge(4), Reserve/Typ(2), EP-Map
void MvcMerger::parseEpMap(const uint8_t *d, int64_t size, uint32_t cpiStart)
{
    if (!cpiStart || int64_t(cpiStart) + 8 >= size || be32(d + cpiStart) == 0)
        return;
    const uint32_t epMap = cpiStart + 6;
    const int numPid = d[epMap + 1];
    for (int i = 0; i < numPid && int64_t(epMap) + 2 + (i + 1) * 12 <= size; ++i) {
        const uint8_t *e = d + epMap + 2 + i * 12;
        const uint16_t pid = be16(e);
        const uint32_t bits = be32(e + 2);               // 10 Reserve, 4 Typ, 16 Coarse, 2 Fine-Hi
        const int numCoarse = int((bits >> 2) & 0xffff);
        const int numFine = int(((bits & 3) << 16) | be16(e + 6));
        const uint32_t streamStart = be32(e + 8) + epMap;
        if (i > 0 && pid != m_depPid)
            continue;
        if (int64_t(streamStart) + 4 > size)
            break;
        const uint32_t fineStart = streamStart + be32(d + streamStart);
        if (int64_t(streamStart) + 4 + int64_t(numCoarse) * 8 > size || int64_t(fineStart) + int64_t(numFine) * 4 > size)
            break;
        m_depPid = pid;
        for (int c = 0; c < numCoarse; ++c) {
            const uint8_t *ce = d + streamStart + 4 + c * 8;
            const uint32_t w = be32(ce);
            const int refFine = int(w >> 14);
            const uint32_t ptsCoarse = w & 0x3fff;
            const uint32_t spnCoarse = be32(ce + 4);
            const int end = c + 1 < numCoarse ? int(be32(ce + 8) >> 14) : numFine;
            for (int f = refFine; f < end && f < numFine; ++f) {
                const uint32_t fw = be32(d + fineStart + f * 4);
                const uint32_t ptsFine = (fw >> 17) & 0x7ff;
                const uint32_t spnFine = fw & 0x1ffff;
                m_ep.push_back({int64_t((ptsCoarse & ~1u) << 18) + int64_t(ptsFine << 8),
                                (spnCoarse & ~0x1ffffu) + spnFine});
            }
        }
        break;
    }
}

bool MvcMerger::openDependent(int playItem)
{
    closeDependent();
    const QString clip = m_depClip.value(playItem);
    if (clip.isEmpty())
        return false;

    // EP-Map aus der CLPI der abhängigen Ansicht (eigene SPN-Zählung dieser Datei). Bei gepressten
    // Discs ist deren CPI leer und die Sprungmarken stehen in den Erweiterungsdaten (ID 2/6, "CPI_SS").
    void *data = nullptr;
    int64_t size = 0;
    const QByteArray clpiPath = QStringLiteral("BDMV/CLIPINF/%1.clpi").arg(clip).toLatin1();
    if (bd_read_file(m_bd, clpiPath.constData(), &data, &size) && data) {
        const auto *d = static_cast<const uint8_t *>(data);
        if (size >= 28) {
            parseEpMap(d, size, be32(d + 16));
            const uint32_t ext = be32(d + 24);
            if (m_ep.empty() && ext && int64_t(ext) + 12 <= size) {
                // ExtensionData: Länge, Datenbeginn, 3 Byte Reserve, Anzahl; je Eintrag ID1, ID2, Beginn, Länge
                const int entries = d[ext + 11];
                for (int e = 0; e < entries && int64_t(ext) + 12 + (e + 1) * 12 <= size; ++e) {
                    const uint8_t *ent = d + ext + 12 + e * 12;
                    if (be16(ent) == 2 && be16(ent + 2) == 6)
                        parseEpMap(d, size, ext + be32(ent + 4));
                }
            }
        }
        freeBlurayBuffer(data);
    }

    const QByteArray m2ts = QStringLiteral("BDMV/STREAM/%1.m2ts").arg(clip).toLatin1();
    m_depFile = bd_open_file_dec(m_bd, m2ts.constData());
    if (!m_depFile) {
        qWarning("Lumen 3D: abhängige Ansicht %s nicht lesbar", m2ts.constData());
        return false;
    }
    qInfo("Lumen 3D: abhängige Ansicht %s, PID 0x%04x, %d Sprungmarken", m2ts.constData(), m_depPid, int(m_ep.size()));
    return true;
}

void MvcMerger::setPlayItem(int index, QByteArray &out)
{
    if (!m_active || index == m_playItem)
        return;
    // Das letzte Bild des alten Clips wartet noch auf den nächsten Paketstart: jetzt ausgeben,
    // solange seine abhängige Datei offen ist – sonst bekäme es einen Partner aus dem neuen Clip
    emitBase(out);
    m_basePes = PesAssembler();
    m_playItem = index;
    openDependent(index);
}

void MvcMerger::reset()
{
    m_basePes = PesAssembler();
    m_depQueue.clear();
    m_depPes = PesAssembler();
    m_depRaw.clear();
    m_needSeek = true;
}

bool MvcMerger::seekDependent(int64_t pts)
{
    if (!m_depFile)
        return false;
    uint32_t spn = 0;
    const int64_t pts45 = (pts & ((int64_t(1) << 33) - 1)) / 2;
    for (const EpPoint &e : m_ep) {
        if (e.pts45 > pts45)
            break;
        spn = e.spn;
    }
    const int64_t offset = int64_t(spn) * kPacket / kUnit * kUnit;
    m_depFile->seek(m_depFile, offset, SEEK_SET);
    m_depRaw.clear();
    m_depPes = PesAssembler();
    m_depQueue.clear();
    m_depEof = false;
    m_needSeek = false;
    return true;
}

bool MvcMerger::readDependentChunk()
{
    if (!m_depFile || m_depEof)
        return false;
    // Je Aufruf genau eine Aligned Unit: Die entschlüsselnde Datei von libbluray (AACS, BD+)
    // liefert für jede andere Größe nichts ("read size != unit size")
    QByteArray chunk(kChunk, Qt::Uninitialized);
    int got = 0;
    while (got < kChunk) {
        const int64_t n = m_depFile->read(m_depFile, reinterpret_cast<uint8_t *>(chunk.data()) + got, kUnit);
        if (n <= 0)
            break;
        got += int(n);
        if (n < kUnit)
            break;
    }
    if (got <= 0) {
        // Dateiende: letzte (nicht mehr durch einen Paketstart beendete) Einheit übernehmen
        m_depEof = true;
        Au au;
        int hdr = 0;
        if (m_depPes.open && parsePes(m_depPes.buf, &au.pts, &hdr)) {
            au.es = m_depPes.buf.mid(hdr);
            m_depQueue.push_back(std::move(au));
        }
        m_depPes = PesAssembler();
        return !m_depQueue.empty();
    }
    m_depRaw.append(chunk.constData(), got);
    const int whole = m_depRaw.size() / kPacket * kPacket;
    for (int i = 0; i < whole; i += kPacket)
        feedDependent(reinterpret_cast<const uint8_t *>(m_depRaw.constData()) + i);
    m_depRaw.remove(0, whole);
    return true;
}

void MvcMerger::feedDependent(const uint8_t *pkt)
{
    TsPacket t;
    if (!parseTs(pkt, t) || t.pid != m_depPid)
        return;
    if (t.pusi) {
        if (m_depPes.open) {
            Au au;
            int hdr = 0;
            // Veraltete Einheiten verwirft findDependent(); der Vorlauf ist durch
            // kMaxChunksPerFrame begrenzt, daher hier kein Größenlimit.
            if (parsePes(m_depPes.buf, &au.pts, &hdr)) {
                au.es = m_depPes.buf.mid(hdr);
                m_depQueue.push_back(std::move(au));
            }
        }
        m_depPes.buf.clear();
        m_depPes.open = true;
    }
    if (m_depPes.open && t.payload)
        m_depPes.buf.append(reinterpret_cast<const char *>(t.payload), t.payloadLen);
}

const MvcMerger::Au *MvcMerger::findDependent(int64_t pts)
{
    if (!m_depFile)
        return nullptr;
    if (m_needSeek)
        seekDependent(pts);

    for (int chunks = 0;; ++chunks) {
        // Veraltete Einheiten verwerfen
        while (!m_depQueue.empty() && m_depQueue.front().pts + kPtsTolerance < pts)
            m_depQueue.pop_front();
        if (!m_depQueue.empty()) {
            const Au &f = m_depQueue.front();
            if (std::llabs(f.pts - pts) <= kPtsTolerance)
                return &f;
            if (f.pts > pts + 90000) { // Sprung zurück -> neu positionieren
                seekDependent(pts);
                continue;
            }
            if (f.pts > pts)
                return nullptr; // diese Basis-Einheit hat keinen Partner
        }
        if (chunks >= kMaxChunksPerFrame) {
            m_needSeek = true; // zu weit entfernt -> beim nächsten Bild per EP-Map springen
            return nullptr;
        }
        if (!readDependentChunk())
            return nullptr;
    }
}

// ---------------------------------------------------------------------------
// Basisansicht: PES sammeln, zusammenführen, neu paketieren
// ---------------------------------------------------------------------------

bool MvcMerger::parsePes(const QByteArray &pes, int64_t *pts, int *headerLen)
{
    const auto *p = reinterpret_cast<const uint8_t *>(pes.constData());
    if (pes.size() < 14 || p[0] != 0 || p[1] != 0 || p[2] != 1)
        return false;
    const int flags = p[7];
    *headerLen = 9 + p[8];
    if (*headerLen > pes.size())
        return false;
    *pts = -1;
    if (flags & 0x80)
        *pts = (int64_t(p[9] & 0x0e) << 29) | (int64_t(p[10]) << 22) | (int64_t(p[11] & 0xfe) << 14)
               | (int64_t(p[12]) << 7) | (p[13] >> 1);
    return true;
}

void MvcMerger::packetize(const QByteArray &pesHeader, const QByteArray &es, uint32_t ats, QByteArray &out)
{
    QByteArray pes = pesHeader;
    pes[4] = 0; // PES_packet_length = 0 (unbegrenzt, für Video zulässig)
    pes[5] = 0;
    pes.append(es);

    const auto *src = reinterpret_cast<const uint8_t *>(pes.constData());
    int remaining = pes.size();
    bool first = true;
    while (remaining > 0) {
        uint8_t pkt[kPacket];
        pkt[0] = uint8_t(ats >> 24); pkt[1] = uint8_t(ats >> 16); pkt[2] = uint8_t(ats >> 8); pkt[3] = uint8_t(ats);
        uint8_t *ts = pkt + 4;
        ts[0] = 0x47;
        ts[1] = uint8_t((first ? 0x40 : 0) | ((m_basePid >> 8) & 0x1f));
        ts[2] = uint8_t(m_basePid & 0xff);
        const int take = std::min(remaining, 184);
        if (take == 184) {
            ts[3] = uint8_t(0x10 | (m_cc++ & 0x0f));
            std::memcpy(ts + 4, src, 184);
        } else {
            // letztes Paket: mit Adaptation-Field auffüllen
            const int afLen = 183 - take;
            ts[3] = uint8_t(0x30 | (m_cc++ & 0x0f));
            ts[4] = uint8_t(afLen);
            if (afLen > 0) {
                ts[5] = 0x00;
                std::memset(ts + 6, 0xff, size_t(afLen - 1));
            }
            std::memcpy(ts + 5 + afLen, src, size_t(take));
        }
        out.append(reinterpret_cast<const char *>(pkt), kPacket);
        src += take;
        remaining -= take;
        first = false;
    }
}

void MvcMerger::emitBase(QByteArray &out)
{
    if (!m_basePes.open || m_basePes.buf.isEmpty())
        return;
    int64_t pts = -1;
    int hdr = 0;
    if (!parsePes(m_basePes.buf, &pts, &hdr)) {
        m_basePes.buf.clear();
        return;
    }
    QByteArray es = m_basePes.buf.mid(hdr);
    if (pts >= 0) {
        if (const Au *dep = findDependent(pts)) {
            // Annex B: Basis-NALs, gefolgt von den NALs der abhängigen Ansicht (Typ 15/20/…)
            es.append(dep->es);
            m_depQueue.pop_front();
            ++m_merged;
        } else {
            ++m_missing;
        }
    }
    packetize(m_basePes.buf.left(hdr), es, m_basePes.ats, out);
    m_basePes.buf.clear();
}

void MvcMerger::flush(QByteArray &out)
{
    if (m_active)
        emitBase(out);
    m_basePes = PesAssembler();
}

void MvcMerger::process(const uint8_t *data, size_t len, QByteArray &out)
{
    if (!m_active) {
        out.append(reinterpret_cast<const char *>(data), int(len));
        return;
    }
    for (size_t off = 0; off + kPacket <= len; off += kPacket) {
        const uint8_t *pkt = data + off;
        TsPacket t;
        if (!parseTs(pkt, t) || t.pid != m_basePid) {
            out.append(reinterpret_cast<const char *>(pkt), kPacket);
            continue;
        }
        if (t.hasPcr) {
            // PCR auf der Video-PID erhalten: reines Adaptation-Field-Paket weitergeben
            uint8_t pcrPkt[kPacket];
            std::memcpy(pcrPkt, pkt, kPacket);
            uint8_t *ts = pcrPkt + 4;
            const int afLen = ts[4];
            ts[1] &= ~0x40;
            ts[3] = uint8_t(0x20 | (ts[3] & 0x0f));
            std::memset(ts + 5 + afLen, 0xff, size_t(183 - afLen));
            ts[4] = 183;
            out.append(reinterpret_cast<const char *>(pcrPkt), kPacket);
        }
        if (t.pusi) {
            emitBase(out);
            m_basePes.open = true;
            m_basePes.ats = be32(pkt) & 0x3fffffff;
        }
        if (m_basePes.open && t.payload)
            m_basePes.buf.append(reinterpret_cast<const char *>(t.payload), t.payloadLen);
    }
}
