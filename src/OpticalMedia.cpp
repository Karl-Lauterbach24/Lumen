#include "OpticalMedia.h"
#include "Tr.h"

#include <QCollator>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QStandardPaths>

#include <mpv/client.h>
#include <mpv/stream_cb.h>

#ifdef LUMEN_HAVE_CDIO
#include <cdio/cdio.h>
#include <cdio/cdtext.h>
#include <cdio/iso9660.h>
#include <cdio/logging.h>
#endif

#include <algorithm>
#include <cstring>
#include <mutex>

namespace Optical {

namespace {

constexpr int kRawM2 = 2336;   // Subheader (8) + Nutzdaten (2324) + EDC (4)
constexpr int kForm2 = 2324;
constexpr double kSectorsPerSecond = 75.0;

bool isDriveRoot(const QString &path)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z]:[\\\\/]?$"));
    return re.match(path).hasMatch();
}

QString findCaseInsensitive(const QDir &dir, const QString &name)
{
    for (const QString &e : dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot))
        if (e.compare(name, Qt::CaseInsensitive) == 0)
            return dir.filePath(e);
    return {};
}

QString edlFile(const QString &s)
{
    return QStringLiteral("%%1%%2").arg(s.toUtf8().size()).arg(s);
}

QString tempDir(const QString &sub)
{
    const QString d = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/lumen-disc/") + sub;
    QDir().mkpath(d);
    return d;
}

bool writeText(const QString &path, const QString &text)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(text.toUtf8());
    return true;
}

QString chapterFile(const QString &dir, const QList<QPair<double, QString>> &chapters, double total)
{
    QString meta = QStringLiteral(";FFMETADATA1\n");
    for (int i = 0; i < chapters.size(); ++i) {
        const double end = i + 1 < chapters.size() ? chapters[i + 1].first : std::max(total, chapters[i].first + 1);
        meta += QStringLiteral("[CHAPTER]\nTIMEBASE=1/1000\nSTART=%1\nEND=%2\ntitle=%3\n")
                    .arg(qRound64(chapters[i].first * 1000)).arg(qRound64(end * 1000)).arg(chapters[i].second);
    }
    const QString path = QDir(dir).filePath(QStringLiteral("chapters.ffmeta"));
    return writeText(path, meta) ? path : QString();
}

// --------------------------------------------------------------------------
// libcdio
// --------------------------------------------------------------------------

#ifdef LUMEN_HAVE_CDIO

QByteArray cdioSource(const QString &path)
{
    if (isDriveRoot(path))
        return QStringLiteral("\\\\.\\%1:").arg(path.at(0)).toLocal8Bit();
    // libcdio findet die BIN-Datei neben der CUE nur mit "/" als Trenner
    return QDir::fromNativeSeparators(path).toLocal8Bit();
}

struct Cdio
{
    CdIo_t *p = nullptr;
    explicit Cdio(const QString &path)
    {
        cdio_loglevel_default = CDIO_LOG_ERROR;
        p = cdio_open(cdioSource(path).constData(), DRIVER_UNKNOWN);
    }
    ~Cdio()
    {
        if (p)
            cdio_destroy(p);
    }
    explicit operator bool() const { return p != nullptr; }
};

QByteArray readIsoFile(CdIo_t *p, const char *path)
{
    // ISO-9660-Namen tragen eine Versionsnummer ("INFO.VCD;1"); libcdio findet
    // Dateien ohne sie nicht
    iso9660_stat_t *st = iso9660_fs_stat_translate(p, path);
    if (!st)
        st = iso9660_fs_stat_translate(p, (QByteArray(path) + ";1").constData());
    if (!st)
        return {};
    const quint32 size = std::min<quint32>(st->size, 256 * 1024);
    const lsn_t lsn = st->lsn;
    iso9660_stat_free(st);
    QByteArray out(int((size + 2047) / 2048 * 2048), '\0');
    for (quint32 i = 0; i * 2048 < size; ++i)
        if (cdio_read_mode2_sector(p, out.data() + i * 2048, lsn_t(lsn + i), false) != DRIVER_OP_SUCCESS)
            return {};
    out.resize(int(size));
    return out;
}

int bcd(uchar v) { return (v >> 4) * 10 + (v & 0xf); }

struct Entry { int track; lsn_t lsn; };

QList<Entry> readEntries(CdIo_t *p, bool *svcd)
{
    QList<Entry> out;
    QByteArray e = readIsoFile(p, "/VCD/ENTRIES.VCD");
    *svcd = false;
    if (e.isEmpty()) {
        e = readIsoFile(p, "/SVCD/ENTRIES.SVD");
        *svcd = !e.isEmpty();
    }
    if (e.size() < 12 || !(e.startsWith("ENTRYVCD") || e.startsWith("ENTRYSVD")))
        return out;
    const auto *d = reinterpret_cast<const uchar *>(e.constData());
    const int count = (d[10] << 8) | d[11];
    for (int i = 0; i < count && 12 + i * 4 + 4 <= e.size(); ++i) {
        const uchar *en = d + 12 + i * 4;
        const lsn_t lsn = lsn_t((bcd(en[1]) * 60 + bcd(en[2])) * 75 + bcd(en[3]) - 150);
        out.append({bcd(en[0]), lsn});
    }
    return out;
}

QString volumeId(CdIo_t *p)
{
    iso9660_pvd_t pvd;
    if (!iso9660_fs_read_pvd(p, &pvd))
        return {};
    char *id = iso9660_get_volume_id(&pvd);
    const QString s = id ? QString::fromLatin1(id).trimmed() : QString();
    if (id)
        cdio_free(id);
    return s;
}

// Registrierte VCD-Tracks: id -> Quelle + Sektorbereich
struct TrackSpec { QString source; lsn_t first; lsn_t last; };
std::mutex g_mutex;
QHash<int, TrackSpec> g_tracks;
int g_next = 1;

int registerTrack(const TrackSpec &t)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto it = g_tracks.cbegin(); it != g_tracks.cend(); ++it)
        if (it->source == t.source && it->first == t.first && it->last == t.last)
            return it.key();
    g_tracks.insert(g_next, t);
    return g_next++;
}

class TrackReader
{
public:
    explicit TrackReader(const TrackSpec &t) : m_cdio(t.source), m_spec(t)
    {
        m_size = qint64(t.last - t.first + 1) * kForm2;
    }
    bool ok() const { return bool(m_cdio) && m_size > 0; }
    qint64 size() const { return m_size; }
    bool seek(qint64 p)
    {
        if (p < 0 || p > m_size)
            return false;
        m_pos = p;
        return true;
    }
    qint64 read(char *buf, qint64 n)
    {
        if (m_pos >= m_size)
            return 0;
        const qint64 sector = m_pos / kForm2;
        const int off = int(m_pos % kForm2);
        if (sector < m_cacheFirst || sector >= m_cacheFirst + m_cacheCount) {
            const int count = int(std::min<qint64>(16, m_spec.last - m_spec.first + 1 - sector));
            m_raw.resize(count * kRawM2);
            if (cdio_read_mode2_sectors(m_cdio.p, m_raw.data(), lsn_t(m_spec.first + sector), true, uint32_t(count)) != DRIVER_OP_SUCCESS) {
                // Einzeln versuchen, unlesbare Sektoren als Stille überspringen
                for (int i = 0; i < count; ++i)
                    if (cdio_read_mode2_sector(m_cdio.p, m_raw.data() + i * kRawM2, lsn_t(m_spec.first + sector + i), true) != DRIVER_OP_SUCCESS)
                        std::memset(m_raw.data() + i * kRawM2, 0, kRawM2);
            }
            m_cacheFirst = sector;
            m_cacheCount = count;
        }
        const qint64 avail = std::min<qint64>(n, kForm2 - off);
        std::memcpy(buf, m_raw.constData() + (sector - m_cacheFirst) * kRawM2 + 8 + off, size_t(avail));
        m_pos += avail;
        return avail;
    }

private:
    Cdio m_cdio;
    TrackSpec m_spec;
    qint64 m_size = 0;
    qint64 m_pos = 0;
    QByteArray m_raw;
    qint64 m_cacheFirst = -1;
    int m_cacheCount = 0;
};

int openTrack(void *, char *uri, mpv_stream_cb_info *info)
{
    const int id = QString::fromUtf8(uri).section(QStringLiteral("://"), 1).toInt();
    TrackSpec spec;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_tracks.contains(id))
            return MPV_ERROR_LOADING_FAILED;
        spec = g_tracks.value(id);
    }
    auto *r = new TrackReader(spec);
    if (!r->ok()) {
        delete r;
        return MPV_ERROR_LOADING_FAILED;
    }
    info->cookie = r;
    info->read_fn = [](void *c, char *buf, uint64_t n) -> int64_t { return static_cast<TrackReader *>(c)->read(buf, qint64(n)); };
    info->seek_fn = [](void *c, int64_t off) -> int64_t { return static_cast<TrackReader *>(c)->seek(off) ? off : int64_t(MPV_ERROR_GENERIC); };
    info->size_fn = [](void *c) -> int64_t { return static_cast<TrackReader *>(c)->size(); };
    info->close_fn = [](void *c) { delete static_cast<TrackReader *>(c); };
    return 0;
}

// Audio-CD: Sektorbereich (je 2352 Byte, 44,1 kHz/16 Bit/Stereo) als WAV-Datei
constexpr int kAudioSector = 2352;
constexpr int kWavHeader = 44;

class AudioReader
{
public:
    explicit AudioReader(const TrackSpec &t) : m_cdio(t.source), m_spec(t)
    {
        const qint64 data = qint64(t.last - t.first + 1) * kAudioSector;
        m_size = kWavHeader + data;
        auto u32 = [this](quint32 v) { for (int i = 0; i < 4; ++i) m_header.append(char(v >> (8 * i))); };
        auto u16 = [this](quint16 v) { for (int i = 0; i < 2; ++i) m_header.append(char(v >> (8 * i))); };
        m_header += "RIFF"; u32(quint32(36 + data)); m_header += "WAVEfmt "; u32(16);
        u16(1); u16(2); u32(44100); u32(44100 * 4); u16(4); u16(16);
        m_header += "data"; u32(quint32(data));
    }
    bool ok() const { return bool(m_cdio) && m_size > kWavHeader; }
    qint64 size() const { return m_size; }
    bool seek(qint64 p)
    {
        if (p < 0 || p > m_size)
            return false;
        m_pos = p;
        return true;
    }
    qint64 read(char *buf, qint64 n)
    {
        if (m_pos >= m_size)
            return 0;
        if (m_pos < kWavHeader) {
            const qint64 avail = std::min<qint64>(n, kWavHeader - m_pos);
            std::memcpy(buf, m_header.constData() + m_pos, size_t(avail));
            m_pos += avail;
            return avail;
        }
        const qint64 p = m_pos - kWavHeader;
        const qint64 sector = p / kAudioSector;
        const int off = int(p % kAudioSector);
        if (sector < m_cacheFirst || sector >= m_cacheFirst + m_cacheCount) {
            const int count = int(std::min<qint64>(20, m_spec.last - m_spec.first + 1 - sector));
            m_raw.resize(count * kAudioSector);
            if (cdio_read_audio_sectors(m_cdio.p, m_raw.data(), lsn_t(m_spec.first + sector), uint32_t(count)) != DRIVER_OP_SUCCESS) {
                // Einzeln versuchen, unlesbare Sektoren als Stille
                for (int i = 0; i < count; ++i)
                    if (cdio_read_audio_sector(m_cdio.p, m_raw.data() + i * kAudioSector, lsn_t(m_spec.first + sector + i)) != DRIVER_OP_SUCCESS)
                        std::memset(m_raw.data() + i * kAudioSector, 0, kAudioSector);
            }
            m_cacheFirst = sector;
            m_cacheCount = count;
        }
        const qint64 avail = std::min<qint64>(n, qint64(m_cacheFirst + m_cacheCount - sector) * kAudioSector - off);
        std::memcpy(buf, m_raw.constData() + (sector - m_cacheFirst) * kAudioSector + off, size_t(avail));
        m_pos += avail;
        return avail;
    }

private:
    Cdio m_cdio;
    TrackSpec m_spec;
    QByteArray m_header;
    qint64 m_size = 0;
    qint64 m_pos = 0;
    QByteArray m_raw;
    qint64 m_cacheFirst = -1;
    int m_cacheCount = 0;
};

int openAudio(void *, char *uri, mpv_stream_cb_info *info)
{
    const int id = QString::fromUtf8(uri).section(QStringLiteral("://"), 1).toInt();
    TrackSpec spec;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_tracks.contains(id))
            return MPV_ERROR_LOADING_FAILED;
        spec = g_tracks.value(id);
    }
    auto *r = new AudioReader(spec);
    if (!r->ok()) {
        delete r;
        return MPV_ERROR_LOADING_FAILED;
    }
    info->cookie = r;
    info->read_fn = [](void *c, char *buf, uint64_t n) -> int64_t { return static_cast<AudioReader *>(c)->read(buf, qint64(n)); };
    info->seek_fn = [](void *c, int64_t off) -> int64_t { return static_cast<AudioReader *>(c)->seek(off) ? off : int64_t(MPV_ERROR_GENERIC); };
    info->size_fn = [](void *c) -> int64_t { return static_cast<AudioReader *>(c)->size(); };
    info->close_fn = [](void *c) { delete static_cast<AudioReader *>(c); };
    return 0;
}

#endif // LUMEN_HAVE_CDIO

// --------------------------------------------------------------------------
// HD DVD
// --------------------------------------------------------------------------

QString localName(const QDomElement &e)
{
    const QString t = e.tagName();
    const int i = t.indexOf(QLatin1Char(':'));
    return i >= 0 ? t.mid(i + 1) : t;
}

void collect(const QDomElement &root, const QString &name, QList<QDomElement> *out)
{
    for (QDomElement c = root.firstChildElement(); !c.isNull(); c = c.nextSiblingElement()) {
        if (localName(c) == name)
            out->append(c);
        collect(c, name, out);
    }
}

// HH:MM:SS:FF in Bildern zu 'fps'
double hdTime(const QString &s, double fps)
{
    const QStringList p = s.trimmed().split(QLatin1Char(':'));
    if (p.size() < 3)
        return 0;
    double v = p[0].toDouble() * 3600 + p[1].toDouble() * 60 + p[2].toDouble();
    if (p.size() >= 4)
        v += p[3].toDouble() / fps;
    return v;
}

struct HdClip { QString file; double start = 0; double length = 0; };
struct HdTitle { int number = 0; QString name; double duration = 0; QList<HdClip> clips; QList<double> chapters; };

QString hdRoot(const QString &path)
{
    const QDir d(path);
    return findCaseInsensitive(d, QStringLiteral("HVDVD_TS")).isEmpty() ? QString() : path;
}

QList<HdTitle> hdTitles(const QString &root)
{
    QList<HdTitle> titles;
    const QDir rootDir(root);
    const QDir ts(findCaseInsensitive(rootDir, QStringLiteral("HVDVD_TS")));
    const QString advPath = findCaseInsensitive(rootDir, QStringLiteral("ADV_OBJ"));

    // Advanced Content: Playlist(s) VPLST*.XPL
    if (!advPath.isEmpty()) {
        QStringList xpls = QDir(advPath).entryList({QStringLiteral("*.XPL"), QStringLiteral("*.xpl")}, QDir::Files, QDir::Name);
        for (const QString &x : std::as_const(xpls)) {
            QFile f(QDir(advPath).filePath(x));
            QDomDocument doc;
            if (!f.open(QIODevice::ReadOnly) || !doc.setContent(f.readAll()))
                continue;
            double fps = 60;
            QList<QDomElement> sets;
            collect(doc.documentElement(), QStringLiteral("TitleSet"), &sets);
            for (const QDomElement &s : std::as_const(sets)) {
                const QString tb = s.attribute(QStringLiteral("timeBase"));
                if (!tb.isEmpty())
                    fps = std::max(1.0, tb.left(tb.indexOf(QLatin1String("fps"))).toDouble());
            }
            QList<QDomElement> ts2;
            collect(doc.documentElement(), QStringLiteral("Title"), &ts2);
            for (const QDomElement &t : std::as_const(ts2)) {
                HdTitle title;
                title.number = t.attribute(QStringLiteral("titleNumber"), QString::number(titles.size() + 1)).toInt();
                title.name = t.attribute(QStringLiteral("displayName"), t.attribute(QStringLiteral("id")));
                title.duration = hdTime(t.attribute(QStringLiteral("titleDuration")), fps);
                QList<QDomElement> clips;
                collect(t, QStringLiteral("PrimaryAudioVideoClip"), &clips);
                for (const QDomElement &c : std::as_const(clips)) {
                    const QString src = c.attribute(QStringLiteral("src"));
                    const QString base = QFileInfo(src).completeBaseName();
                    const QString evo = findCaseInsensitive(ts, base + QStringLiteral(".EVO"));
                    if (evo.isEmpty())
                        continue;
                    const double begin = hdTime(c.attribute(QStringLiteral("titleTimeBegin")), fps);
                    const double end = hdTime(c.attribute(QStringLiteral("titleTimeEnd")), fps);
                    title.clips.append({evo, hdTime(c.attribute(QStringLiteral("clipTimeBegin")), fps), std::max(0.0, end - begin)});
                    title.duration = std::max(title.duration, end);
                }
                QList<QDomElement> chapters;
                collect(t, QStringLiteral("Chapter"), &chapters);
                for (const QDomElement &ch : std::as_const(chapters))
                    title.chapters.append(hdTime(ch.attribute(QStringLiteral("titleTimeBegin")), fps));
                if (!title.clips.isEmpty())
                    titles.append(title);
            }
            if (!titles.isEmpty())
                break; // erste gültige Playlist genügt
        }
    }
    // Standard Content / keine Playlist: EVO-Dateien (FEATURE_1.EVO, FEATURE_2.EVO …) gruppieren
    if (titles.isEmpty()) {
        QStringList evos = ts.entryList({QStringLiteral("*.EVO"), QStringLiteral("*.evo")}, QDir::Files);
        QCollator coll;
        coll.setNumericMode(true);
        std::sort(evos.begin(), evos.end(), [&](const QString &a, const QString &b) { return coll.compare(a, b) < 0; });
        QMap<QString, HdTitle> groups;
        static const QRegularExpression suffix(QStringLiteral("_?\\d+$"));
        for (const QString &e : std::as_const(evos)) {
            QString base = QFileInfo(e).completeBaseName();
            base.remove(suffix);
            HdTitle &t = groups[base.isEmpty() ? QFileInfo(e).completeBaseName() : base];
            t.name = base;
            // Dauer grob über die Größe (~ 30 Mbit/s), mpv liefert später die echte
            t.duration += QFileInfo(ts.filePath(e)).size() / (30e6 / 8);
            t.clips.append({ts.filePath(e), 0, 0});
        }
        int n = 1;
        for (auto it = groups.begin(); it != groups.end(); ++it) {
            it->number = n++;
            titles.append(*it);
        }
    }
    return titles;
}

} // namespace

QString edlUrl(const QString &edl)
{
    // Inline-EDL: gilt für mpv als direkte Eingabe, eigene Protokolle (lumendcp://,
    // lumenvcd://) sind darin erlaubt – anders als in EDL-Dateien
    QStringList lines;
    for (const QString &l : edl.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
        if (!l.startsWith(QLatin1Char('#')))
            lines << l;
    return QStringLiteral("edl://") + lines.join(QLatin1Char(';'));
}

bool cdioAvailable()
{
#ifdef LUMEN_HAVE_CDIO
    return true;
#else
    return false;
#endif
}

QString musicBrainzDiscId(int firstTrack, int lastTrack, int leadout, const QList<int> &offsets)
{
    // SHA-1 über Hex-Text: erster/letzter Track (2 Stellen), Lead-out und 99 Trackanfänge (je 8 Stellen)
    QByteArray text = QByteArray::number(firstTrack, 16).rightJustified(2, '0')
                      + QByteArray::number(lastTrack, 16).rightJustified(2, '0')
                      + QByteArray::number(leadout, 16).rightJustified(8, '0');
    for (int i = 0; i < 99; ++i)
        text += QByteArray::number(i < offsets.size() ? offsets[i] : 0, 16).rightJustified(8, '0');
    QByteArray id = QCryptographicHash::hash(text.toUpper(), QCryptographicHash::Sha1).toBase64();
    // MusicBrainz-Variante von Base64 (URL-tauglich)
    return QString::fromLatin1(id.replace('+', '.').replace('/', '_').replace('=', '-'));
}

bool isImage(const QString &path)
{
    const QString s = QFileInfo(path).suffix().toLower();
    return s == QLatin1String("cue") || s == QLatin1String("bin") || s == QLatin1String("nrg") || s == QLatin1String("toc");
}

QString detect(const QString &path)
{
    const QFileInfo fi(path);
    if (fi.isDir()) {
        const QDir d(path);
        if (!hdRoot(path).isEmpty())
            return QStringLiteral("hddvd");
        if (!findCaseInsensitive(d, QStringLiteral("SVCD")).isEmpty() || !findCaseInsensitive(d, QStringLiteral("MPEG2")).isEmpty())
            return QStringLiteral("svcd");
        if (!findCaseInsensitive(d, QStringLiteral("VCD")).isEmpty() || !findCaseInsensitive(d, QStringLiteral("MPEGAV")).isEmpty())
            return QStringLiteral("vcd");
        if (!d.entryList({QStringLiteral("*.cda")}, QDir::Files).isEmpty())
            return QStringLiteral("cdda");
    }
#ifdef LUMEN_HAVE_CDIO
    // Nur Abbilder per libcdio prüfen: gemountete Discs erkennt das Dateisystem
    // (Windows zeigt Audio-CDs als *.cda, VCDs mit MPEGAV) – das spart das
    // wiederholte Lesen des Inhaltsverzeichnisses bei jeder Laufwerksabfrage
    if (fi.isFile() && isImage(path)) {
        QString source = path;
        if (fi.suffix().compare(QLatin1String("bin"), Qt::CaseInsensitive) == 0) {
            const QString cue = fi.path() + QLatin1Char('/') + fi.completeBaseName() + QStringLiteral(".cue");
            if (QFileInfo::exists(cue))
                source = cue;
        }
        Cdio c(source);
        if (!c)
            return {};
        const track_t first = cdio_get_first_track_num(c.p);
        if (first == CDIO_INVALID_TRACK)
            return {};
        if (cdio_get_track_format(c.p, first) == TRACK_FORMAT_AUDIO)
            return QStringLiteral("cdda");
        bool svcd = false;
        if (!readEntries(c.p, &svcd).isEmpty())
            return svcd ? QStringLiteral("svcd") : QStringLiteral("vcd");
        // Ohne lesbare ENTRIES: Mode-2-Tracks hinter dem Datentrack = (S)VCD-Aufbau
        if (cdio_get_num_tracks(c.p) >= 2 && cdio_get_track_format(c.p, track_t(first + 1)) == TRACK_FORMAT_XA)
            return QStringLiteral("vcd");
    }
#endif
    return {};
}

void attachProtocol(mpv_handle *mpv)
{
#ifdef LUMEN_HAVE_CDIO
    mpv_stream_cb_add_ro(mpv, "lumenvcd", nullptr, [](void *ud, char *uri, mpv_stream_cb_info *info) { return openTrack(ud, uri, info); });
    mpv_stream_cb_add_ro(mpv, "lumencdda", nullptr, [](void *ud, char *uri, mpv_stream_cb_info *info) { return openAudio(ud, uri, info); });
#else
    Q_UNUSED(mpv)
#endif
}

VcdDisc readVcdDisc(const QString &path)
{
    VcdDisc d;
#ifdef LUMEN_HAVE_CDIO
    Cdio c(path);
    if (!c)
        return d;
    d.info = readIsoFile(c.p, "/VCD/INFO.VCD");
    if (d.info.isEmpty()) {
        d.info = readIsoFile(c.p, "/SVCD/INFO.SVD");
        d.svcd = !d.info.isEmpty();
    }
    const char *dir = d.svcd ? "/SVCD/" : "/VCD/";
    const char *ext = d.svcd ? ".SVD" : ".VCD";
    d.lot = readIsoFile(c.p, (QByteArray(dir) + "LOT" + ext).constData());
    d.psd = readIsoFile(c.p, (QByteArray(dir) + "PSD" + ext).constData());
    bool svcd = false;
    for (const Entry &e : readEntries(c.p, &svcd))
        d.entries.append({e.track, int(e.lsn)});
    const track_t first = cdio_get_first_track_num(c.p), n = cdio_get_num_tracks(c.p);
    for (track_t t = first; n != CDIO_INVALID_TRACK && t < first + n; ++t)
        d.tracks.insert(int(t), {int(cdio_get_track_lsn(c.p, t)), int(cdio_get_track_last_lsn(c.p, t))});
    d.ok = d.info.size() >= 56 && !d.psd.isEmpty();
#else
    Q_UNUSED(path)
#endif
    return d;
}

QString sectorUrl(const QString &path, int firstLsn, int lastLsn)
{
#ifdef LUMEN_HAVE_CDIO
    return QStringLiteral("lumenvcd://%1").arg(registerTrack({path, lsn_t(firstLsn), lsn_t(lastLsn)}));
#else
    Q_UNUSED(path)
    Q_UNUSED(firstLsn)
    Q_UNUSED(lastLsn)
    return {};
#endif
}

QVariantMap scan(const QString &path, const QString &kind)
{
    QVariantMap m;
    m["device"] = path;
    m["kind"] = kind;
    QVariantList titles;

    if (kind == QLatin1String("hddvd")) {
        const QList<HdTitle> list = hdTitles(path);
        int main = -1;
        double longest = 0;
        for (int i = 0; i < list.size(); ++i) {
            titles.append(QVariantMap{{"index", i}, {"title", list[i].number}, {"label", list[i].name},
                                      {"duration", list[i].duration}, {"chapters", int(list[i].chapters.size())}});
            if (list[i].duration > longest)
                longest = list[i].duration, main = i;
        }
        if (main >= 0) {
            QVariantMap t = titles[main].toMap();
            t["main"] = true;
            titles[main] = t;
        }
        m["aacs"] = !findCaseInsensitive(QDir(path), QStringLiteral("AACS")).isEmpty();
        if (list.isEmpty())
            m["error"] = LTR("Keine abspielbaren EVO-Dateien gefunden");
        m["titles"] = titles;
        return m;
    }

#ifdef LUMEN_HAVE_CDIO
    if (kind != QLatin1String("hddvd") && (isImage(path) || isDriveRoot(path))) {
        Cdio c(path);
        if (!c) {
            m["error"] = LTR("libcdio konnte die Disc/das Abbild nicht öffnen");
            return m;
        }
        m["discName"] = volumeId(c.p);
        const track_t first = cdio_get_first_track_num(c.p);
        const track_t count = cdio_get_num_tracks(c.p);
        bool svcd = false;
        const QList<Entry> entries = kind == QLatin1String("cdda") ? QList<Entry>() : readEntries(c.p, &svcd);
        double longest = 0;
        int main = -1;
        for (track_t t = first; t < first + count; ++t) {
            const bool audio = cdio_get_track_format(c.p, t) == TRACK_FORMAT_AUDIO;
            if (kind == QLatin1String("cdda") ? !audio : (audio || t == first))
                continue; // VCD: Track 1 ist das Dateisystem
            const lsn_t a = cdio_get_track_lsn(c.p, t), b = cdio_get_track_last_lsn(c.p, t);
            const double secs = (b - a + 1) / kSectorsPerSecond;
            int chapters = 0;
            for (const Entry &e : entries)
                chapters += e.track == t;
            if (secs > longest)
                longest = secs, main = int(titles.size());
            titles.append(QVariantMap{{"index", int(titles.size())}, {"title", int(t)}, {"label", LTR("Track %1").arg(t)},
                                      {"duration", secs}, {"chapters", std::max(1, chapters)}});
        }
        if (kind == QLatin1String("cdda")) {
            // Inhaltsverzeichnis + MusicBrainz-Disc-ID (für Plugins, die die CD nachschlagen).
            // Ein Datenteil hinter den Audio-Tracks (CD-Extra) zählt nicht mit: das Lead-out
            // der Audio-Sitzung liegt 11400 Sektoren vor dem Datentrack.
            track_t last = first + count - 1;
            int leadout = int(cdio_get_track_lsn(c.p, CDIO_CDROM_LEADOUT_TRACK)) + 150;
            while (last > first && cdio_get_track_format(c.p, last) != TRACK_FORMAT_AUDIO) {
                leadout = int(cdio_get_track_lsn(c.p, last)) + 150 - 11400;
                --last;
            }
            QList<int> offsets;
            QVariantList offsetList;
            for (track_t t = first; t <= last; ++t) {
                offsets << int(cdio_get_track_lsn(c.p, t)) + 150;
                offsetList << offsets.last();
            }
            if (!offsets.isEmpty() && leadout > offsets.last()) {
                m["toc"] = QVariantMap{{"first", int(first)}, {"last", int(last)}, {"leadout", leadout}, {"offsets", offsetList}};
                m["mbDiscId"] = musicBrainzDiscId(first, last, leadout, offsets);
            }
            // CD-Text (falls auf der Disc bzw. im CUE-Sheet vorhanden): Album, Interpret, Tracknamen
            if (cdtext_t *text = cdio_get_cdtext(c.p)) {
                auto field = [text](cdtext_field_t f, track_t t) {
                    const char *v = cdtext_get_const(text, f, t);
                    return v ? QString::fromUtf8(v).simplified() : QString();
                };
                const QString album = field(CDTEXT_FIELD_TITLE, 0), artist = field(CDTEXT_FIELD_PERFORMER, 0);
                bool any = !album.isEmpty();
                for (int i = 0; i < titles.size(); ++i) {
                    QVariantMap t = titles[i].toMap();
                    const track_t n = track_t(t.value("title").toInt());
                    const QString name = field(CDTEXT_FIELD_TITLE, n), performer = field(CDTEXT_FIELD_PERFORMER, n);
                    if (name.isEmpty())
                        continue;
                    any = true;
                    t["label"] = QStringLiteral("%1. %2").arg(n).arg(name);
                    t["name"] = name;
                    if (!performer.isEmpty() && performer != artist)
                        t["artist"] = performer;
                    titles[i] = t;
                }
                if (!album.isEmpty())
                    m["discName"] = artist.isEmpty() ? album : artist + QStringLiteral(" – ") + album;
                if (any) {
                    m["cdText"] = true;
                    QVariantMap meta{{"source", QStringLiteral("CD-Text")}};
                    if (!album.isEmpty())
                        meta["title"] = album;
                    if (!artist.isEmpty())
                        meta["artist"] = artist;
                    m["meta"] = meta;
                }
            }
        }
        if (main >= 0 && kind != QLatin1String("cdda")) {
            QVariantMap t = titles[main].toMap();
            t["main"] = true;
            titles[main] = t;
        }
        m["titles"] = titles;
        return m;
    }
#endif
    // Dateisystem-Zugriff (gemountete VCD/SVCD)
    const QDir d(path);
    const QString dirName = findCaseInsensitive(d, kind == QLatin1String("svcd") ? QStringLiteral("MPEG2") : QStringLiteral("MPEGAV"));
    const QStringList files = QDir(dirName).entryList(QDir::Files, QDir::Name);
    for (const QString &f : files) {
        const qint64 size = QFileInfo(QDir(dirName).filePath(f)).size();
        titles.append(QVariantMap{{"index", int(titles.size())}, {"label", f}, {"file", QDir(dirName).filePath(f)},
                                  {"duration", size / 2352.0 / kSectorsPerSecond}, {"chapters", 1}});
    }
    if (kind == QLatin1String("cdda")) {
        titles.clear();
        const QStringList cda = d.entryList({QStringLiteral("*.cda")}, QDir::Files, QDir::Name);
        for (int i = 0; i < cda.size(); ++i)
            titles.append(QVariantMap{{"index", i}, {"title", i + 1}, {"label", LTR("Track %1").arg(i + 1)}, {"duration", -1}, {"chapters", 1}});
    }
    m["titles"] = titles;
    return m;
}

Prepared prepare(const QString &path, const QString &kind, int title)
{
    Prepared p;
    const QString label = QFileInfo(QDir::cleanPath(path)).fileName();

#ifdef LUMEN_HAVE_CDIO
    if (kind == QLatin1String("cdda") && (isImage(path) || isDriveRoot(path))) {
        // Audio-Tracks sektorgenau über libcdio (Laufwerk oder Abbild), ein Kapitel je Track
        Cdio c(path);
        if (!c) {
            p.error = LTR("libcdio konnte die Disc/das Abbild nicht öffnen");
            return p;
        }
        const track_t first = cdio_get_first_track_num(c.p);
        const track_t count = cdio_get_num_tracks(c.p);
        cdtext_t *text = cdio_get_cdtext(c.p);
        auto field = [text](cdtext_field_t f, track_t t) {
            const char *v = text ? cdtext_get_const(text, f, t) : nullptr;
            return v ? QString::fromUtf8(v).simplified() : QString();
        };
        lsn_t a = CDIO_INVALID_LSN, b = CDIO_INVALID_LSN;
        QList<QPair<double, QString>> chapters;
        for (track_t t = first; count != CDIO_INVALID_TRACK && t < first + count; ++t) {
            if (cdio_get_track_format(c.p, t) != TRACK_FORMAT_AUDIO)
                continue;
            const lsn_t start = cdio_get_track_lsn(c.p, t);
            if (a == CDIO_INVALID_LSN)
                a = start;
            b = cdio_get_track_last_lsn(c.p, t);
            const QString name = field(CDTEXT_FIELD_TITLE, t);
            chapters.append({(start - a) / kSectorsPerSecond,
                             name.isEmpty() ? LTR("Track %1").arg(t) : QStringLiteral("%1. %2").arg(t).arg(name)});
        }
        if (chapters.isEmpty() || b <= a) {
            p.error = LTR("Keine Audio-Tracks auf der CD");
            return p;
        }
        p.url = QStringLiteral("lumencdda://%1").arg(registerTrack({QString::fromLocal8Bit(cdioSource(path)), a, b}));
        const QString cf = chapterFile(tempDir(QStringLiteral("cdda")), chapters, (b - a + 1) / kSectorsPerSecond);
        if (!cf.isEmpty())
            p.options["chapters-file"] = cf;
        if (title >= 0)
            p.options["start"] = QStringLiteral("#%1").arg(title + 1);
        const QString album = field(CDTEXT_FIELD_TITLE, 0);
        p.options["force-media-title"] = album.isEmpty() ? QStringLiteral("Audio-CD") : album;
        return p;
    }
#endif
    if (kind == QLatin1String("cdda")) {
        p.url = QStringLiteral("cdda://");
        QString dev = path;
        if (isDriveRoot(path))
            dev = path.left(2);
        p.options["cdda-device"] = QDir::toNativeSeparators(dev);
        if (title >= 0)
            p.options["start"] = QStringLiteral("#%1").arg(title + 1);
        p.options["force-media-title"] = QStringLiteral("Audio-CD");
        return p;
    }

    if (kind == QLatin1String("hddvd")) {
        const QList<HdTitle> titles = hdTitles(path);
        if (titles.isEmpty()) {
            p.error = LTR("HD DVD: keine abspielbaren EVO-Dateien");
            return p;
        }
        int idx = title;
        if (idx < 0 || idx >= titles.size()) {
            idx = 0;
            for (int i = 1; i < titles.size(); ++i)
                if (titles[i].duration > titles[idx].duration)
                    idx = i;
        }
        const HdTitle &t = titles[idx];
        const QString dir = tempDir(QStringLiteral("hddvd"));
        QString edl = QStringLiteral("# mpv EDL v0\n!no_chapters\n");
        for (const HdClip &c : t.clips) {
            if (c.length > 0)
                edl += QStringLiteral("%1,%2,%3\n").arg(edlFile(c.file)).arg(c.start, 0, 'f', 3).arg(c.length, 0, 'f', 3);
            else
                edl += edlFile(c.file) + QLatin1Char('\n');
        }
        writeText(QDir(dir).filePath(QStringLiteral("title.edl")), edl);
        p.url = edlUrl(edl);
        if (!t.chapters.isEmpty()) {
            QList<QPair<double, QString>> ch;
            for (int i = 0; i < t.chapters.size(); ++i)
                ch.append({t.chapters[i], LTR("Kapitel %1").arg(i + 1)});
            const QString cf = chapterFile(dir, ch, t.duration);
            if (!cf.isEmpty())
                p.options["chapters-file"] = cf;
        }
        p.options["force-media-title"] = label.isEmpty() ? QStringLiteral("HD DVD") : label;
        if (!findCaseInsensitive(QDir(path), QStringLiteral("AACS")).isEmpty())
            p.error = LTR("HD DVD mit AACS-Verzeichnis – Lumen entschlüsselt nicht; nur ungeschützte bzw. bereits entschlüsselte Inhalte sind abspielbar");
        return p;
    }

    // VCD / SVCD
#ifdef LUMEN_HAVE_CDIO
    if (isImage(path) || isDriveRoot(path)) {
        QString source = path;
        const QFileInfo fi(path);
        if (fi.suffix().compare(QLatin1String("bin"), Qt::CaseInsensitive) == 0) {
            const QString cue = fi.path() + QLatin1Char('/') + fi.completeBaseName() + QStringLiteral(".cue");
            if (QFileInfo::exists(cue))
                source = cue;
        }
        Cdio c(source);
        if (!c) {
            p.error = LTR("libcdio konnte die Video-CD nicht öffnen");
            return p;
        }
        const track_t first = cdio_get_first_track_num(c.p);
        const track_t count = cdio_get_num_tracks(c.p);
        bool svcd = false;
        const QList<Entry> entries = readEntries(c.p, &svcd);
        const QString dir = tempDir(QStringLiteral("vcd"));
        QString edl = QStringLiteral("# mpv EDL v0\n!no_chapters\n");
        QList<QPair<double, QString>> chapters;
        double t0 = 0;
        int n = 0;
        for (track_t t = first + 1; t < first + count; ++t) {
            if (cdio_get_track_format(c.p, t) == TRACK_FORMAT_AUDIO)
                continue;
            const int index = n++;
            if (title >= 0 && index != title)
                continue;
            const lsn_t a = cdio_get_track_lsn(c.p, t), b = cdio_get_track_last_lsn(c.p, t);
            const int id = registerTrack({QString::fromLocal8Bit(cdioSource(source)), a, b});
            edl += edlFile(QStringLiteral("lumenvcd://%1").arg(id)) + QLatin1Char('\n');
            int e = 0;
            for (const Entry &en : entries)
                if (en.track == t)
                    chapters.append({t0 + std::max(0, en.lsn - a) / kSectorsPerSecond,
                                     LTR("Track %1 · Einsprung %2").arg(t).arg(++e)});
            if (!e)
                chapters.append({t0, LTR("Track %1").arg(t)});
            t0 += (b - a + 1) / kSectorsPerSecond;
        }
        if (n == 0) {
            p.error = LTR("Keine MPEG-Tracks auf der Video-CD");
            return p;
        }
        writeText(QDir(dir).filePath(QStringLiteral("vcd.edl")), edl);
        p.url = edlUrl(edl);
        p.options["load-unsafe-playlists"] = "yes"; // lumenvcd:// in der eigenen EDL
        const QString cf = chapterFile(dir, chapters, t0);
        if (!cf.isEmpty())
            p.options["chapters-file"] = cf;
        const QString vol = volumeId(c.p);
        p.options["force-media-title"] = vol.isEmpty() ? (svcd ? LTR("Super Video-CD") : QStringLiteral("Video-CD")) : vol;
        return p;
    }
#endif
    // Dateisystem: MPEGAV/*.DAT bzw. MPEG2/*.MPG (Windows liefert RIFF/CDXA mit Rohsektoren – FFmpeg synchronisiert neu)
    const QVariantMap info = scan(path, kind);
    const QVariantList titles = info.value("titles").toList();
    if (titles.isEmpty()) {
        p.error = LTR("Keine Videodateien auf der Video-CD gefunden");
        return p;
    }
    const QString dir = tempDir(QStringLiteral("vcdfs"));
    QString edl = QStringLiteral("# mpv EDL v0\n");
    for (int i = 0; i < titles.size(); ++i) {
        if (title >= 0 && i != title)
            continue;
        const QVariantMap t = titles[i].toMap();
        edl += QStringLiteral("%1,title=%2\n").arg(edlFile(t.value("file").toString()), t.value("label").toString());
    }
    writeText(QDir(dir).filePath(QStringLiteral("vcd.edl")), edl);
    p.url = edlUrl(edl);
    p.options["force-media-title"] = kind == QLatin1String("svcd") ? LTR("Super Video-CD") : QStringLiteral("Video-CD");
    return p;
}

} // namespace Optical
