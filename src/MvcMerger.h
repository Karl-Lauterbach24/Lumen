#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <cstdint>
#include <deque>
#include <vector>

struct bluray;
struct bd_file_s;

// Blu-ray 3D (H.264/MVC): führt Basis- und Abhängigkeitsansicht zu einem
// gemeinsamen Videostrom zusammen, wie ihn ein MVC-Decoder (FFmpeg-mvc) erwartet.
//
//   Basisansicht (linkes/rechtes Auge, PID 0x1011)   <- libbluray bd_read_ext()
//   Abhängige Ansicht (PID 0x1012, eigene .m2ts)     <- bd_open_file_dec() (entschlüsselt
//                                                       durch die installierte AACS-Bibliothek)
//
// Pro Zugriffseinheit wird die abhängige Ansicht mit gleicher PTS an die
// Basisansicht angehängt (Annex-B: Basis-NALs + NAL 20 ...) und der Video-PES
// neu paketiert. Alle anderen PIDs (Ton, PGS, PCR) laufen unverändert durch.
// Welche Datei die abhängige Ansicht enthält, steht im SS-Subpfad der Playlist;
// Sprungmarken für die Suche liefert die EP-Map "cpi_ss" der Basis-CLPI.
class MvcMerger
{
public:
    explicit MvcMerger(struct bluray *bd);
    ~MvcMerger();

    // Liest den SS-Subpfad der Playlist. false = Playlist ist nicht 3D.
    bool setPlaylist(uint32_t playlist);
    // Wechsel des PlayItems (Clip) – öffnet die zugehörige abhängige Datei
    void setPlayItem(int index);
    // Nach Sprüngen (Seek, Titelwechsel): Pufferstände verwerfen
    void reset();

    bool active() const { return m_active; }
    bool baseViewIsRight() const { return m_baseRight; }
    quint64 mergedFrames() const { return m_merged; }
    quint64 missingFrames() const { return m_missing; }

    // Verarbeitet 192-Byte-M2TS-Pakete der Basisansicht; hängt Ausgabe an out an.
    void process(const uint8_t *data, size_t len, QByteArray &out);
    // Stromende: letzte Zugriffseinheit ausgeben
    void flush(QByteArray &out);

private:
    struct Au
    {
        int64_t pts = -1;
        QByteArray es;
    };

    struct PesAssembler
    {
        QByteArray buf;
        bool open = false;
        uint32_t ats = 0;
    };

    bool parsePlaylistSs(const QByteArray &mpls, uint32_t playlist);
    void closeDependent();
    bool openDependent(int playItem);
    bool seekDependent(int64_t pts);
    bool readDependentChunk();
    void feedDependent(const uint8_t *pkt);
    const Au *findDependent(int64_t pts);
    void emitBase(QByteArray &out);
    void packetize(const QByteArray &pesHeader, const QByteArray &es, uint32_t ats, QByteArray &out);
    static bool parsePes(const QByteArray &pes, int64_t *pts, int *headerLen);

    struct bluray *m_bd = nullptr;
    bool m_active = false;
    bool m_baseRight = false;
    uint16_t m_basePid = 0x1011;
    uint16_t m_depPid = 0x1012;
    uint32_t m_playlist = 0xffffffffu;
    QHash<int, QString> m_depClip; // PlayItem -> Clipname der abhängigen Ansicht ("00002")
    int m_playItem = -1;

    struct bd_file_s *m_depFile = nullptr;
    struct EpPoint { int64_t pts45; uint32_t spn; };
    std::vector<EpPoint> m_ep; // EP-Map der abhängigen Ansicht (45-kHz-PTS -> SPN)
    QByteArray m_depRaw;       // gelesene, noch nicht verarbeitete Bytes
    PesAssembler m_depPes;
    std::deque<Au> m_depQueue;
    bool m_depEof = false;
    bool m_needSeek = true;

    PesAssembler m_basePes;
    uint8_t m_cc = 0;
    quint64 m_merged = 0;
    quint64 m_missing = 0;
};
