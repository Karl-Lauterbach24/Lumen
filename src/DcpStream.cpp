#include "DcpStream.h"
#include "DcpCrypto.h"

#include <QFile>
#include <QHash>
#include <QList>
#include <QtDebug>

#include <mpv/client.h>
#include <mpv/stream_cb.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace Dcp {

std::atomic<int> g_keyErrors{0};
std::atomic<int> g_missingKeys{0};

namespace {
KeyProvider g_keyProvider;
}

void setKeyProvider(KeyProvider provider)
{
    g_keyProvider = std::move(provider);
}

QByteArray providedKey(const QString &keyId)
{
    if (!g_keyProvider)
        return {};
    QString hex = keyId;
    hex.remove(QStringLiteral("urn:uuid:"), Qt::CaseInsensitive).remove(QLatin1Char('-'));
    const QByteArray id = QByteArray::fromHex(hex.toLatin1());
    if (id.size() != 16)
        return {};
    const QByteArray key = g_keyProvider(id);
    return key.size() == 16 ? key : QByteArray();
}

namespace {

std::mutex g_mutex;
QHash<int, StreamSpec> g_streams;
int g_nextId = 1;

const uchar kUl[4] = {0x06, 0x0e, 0x2b, 0x34};
// Encrypted Triplet (SMPTE 429-6)
const uchar kTriplet[16] = {0x06, 0x0e, 0x2b, 0x34, 0x02, 0x04, 0x01, 0x07, 0x0d, 0x01, 0x03, 0x01, 0x02, 0x7e, 0x01, 0x00};
const uchar kFill[16] = {0x06, 0x0e, 0x2b, 0x34, 0x01, 0x01, 0x01, 0x02, 0x03, 0x01, 0x02, 0x10, 0x01, 0x00, 0x00, 0x00};
const uchar kCheck[16] = {'C', 'H', 'U', 'K', 'C', 'H', 'U', 'K', 'C', 'H', 'U', 'K', 'C', 'H', 'U', 'K'};

quint64 rb(const uchar *p, int n)
{
    quint64 v = 0;
    for (int i = 0; i < n; ++i)
        v = (v << 8) | p[i];
    return v;
}

bool berAt(const uchar *p, const uchar *end, quint64 *len, int *hdr)
{
    if (p >= end)
        return false;
    if (!(p[0] & 0x80)) {
        *len = p[0];
        *hdr = 1;
        return true;
    }
    const int n = p[0] & 0x7f;
    if (n == 0 || n > 8 || p + 1 + n > end)
        return false;
    *len = rb(p + 1, n);
    *hdr = 1 + n;
    return true;
}

// Minimale BER-Kodierung, die exakt 'bytes' Bytes belegt (für größengleichen Ersatz)
void putBer(uchar *p, quint64 value, int bytes)
{
    if (bytes == 1) {
        p[0] = uchar(value);
        return;
    }
    p[0] = uchar(0x80 | (bytes - 1));
    for (int i = bytes - 1; i >= 1; --i) {
        p[i] = uchar(value & 0xff);
        value >>= 8;
    }
}

int berSizeFor(quint64 value)
{
    if (value < 0x80)
        return 1;
    int n = 1;
    while (n < 8 && (value >> (8 * n)))
        ++n;
    return 1 + n;
}

// Fill-KLV, der genau 'total' Bytes belegt
bool writeFill(uchar *p, qint64 total)
{
    for (int berBytes = 1; berBytes <= 9; ++berBytes) {
        const qint64 value = total - 16 - berBytes;
        if (value < 0)
            return false;
        if (berSizeFor(quint64(value)) <= berBytes && (berBytes > 1 || value < 0x80)) {
            std::memcpy(p, kFill, 16);
            putBer(p + 16, quint64(value), berBytes);
            std::memset(p + 16 + berBytes, 0, size_t(value));
            return true;
        }
    }
    return false;
}

bool isPictureElement(const uchar *key, int *elementNumber)
{
    // 06.0e.2b.34.01.02.01.xx.0d.01.03.01.15.<count>.08.<number>  (JPEG 2000, frame-wrapped)
    if (std::memcmp(key, kUl, 4) != 0 || key[4] != 0x01 || key[5] != 0x02 || key[8] != 0x0d || key[9] != 0x01
        || key[10] != 0x03 || key[11] != 0x01 || key[12] != 0x15)
        return false;
    *elementNumber = key[15];
    return key[13] >= 2;
}

struct Klv
{
    qint64 start = 0;
    int headerLen = 0;
    qint64 valueLen = 0;
    uchar key[16] = {};
    qint64 end() const { return start + headerLen + valueLen; }
};

class Reader
{
public:
    explicit Reader(const StreamSpec &spec) : m_spec(spec)
    {
        m_file.setFileName(spec.file);
        m_ok = m_file.open(QIODevice::ReadOnly);
        m_size = m_ok ? m_file.size() : 0;
        if (!spec.key.isEmpty())
            m_aes.setKey(spec.key);
    }

    bool ok() const { return m_ok; }
    qint64 size() const { return m_size; }
    qint64 pos() const { return m_pos; }
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
        n = std::min(n, m_size - m_pos);
        Klv k;
        if (!locate(m_pos, &k)) {
            // Keine KLV-Struktur erkennbar (Run-In o. Ä.) -> roh durchreichen
            return rawRead(buf, n);
        }
        const qint64 avail = std::min(n, k.end() - m_pos);
        if (!transformed(k))
            return rawRead(buf, avail);
        if (m_cacheStart != k.start && !build(k))
            return rawRead(buf, avail);
        std::memcpy(buf, m_cache.constData() + (m_pos - k.start), size_t(avail));
        m_pos += avail;
        return avail;
    }

private:
    qint64 rawRead(char *buf, qint64 n)
    {
        if (!m_file.seek(m_pos))
            return -1;
        const qint64 got = m_file.read(buf, n);
        if (got > 0)
            m_pos += got;
        return got;
    }

    bool parseAt(qint64 p, Klv *k)
    {
        if (p + 17 > m_size || !m_file.seek(p))
            return false;
        uchar h[25];
        const qint64 got = m_file.read(reinterpret_cast<char *>(h), std::min<qint64>(25, m_size - p));
        if (got < 17 || std::memcmp(h, kUl, 4) != 0)
            return false;
        quint64 len = 0;
        int hdr = 0;
        if (!berAt(h + 16, h + got, &len, &hdr))
            return false;
        k->start = p;
        k->headerLen = 16 + hdr;
        k->valueLen = qint64(len);
        std::memcpy(k->key, h, 16);
        if (k->end() > m_size || k->valueLen < 0)
            return false;
        m_known[p] = *k;
        return true;
    }

    // KLV finden, das Position p enthält
    bool locate(qint64 p, Klv *k)
    {
        auto it = m_known.upper_bound(p);
        qint64 from = 0;
        if (it != m_known.begin()) {
            --it;
            if (p < it->second.end()) {
                *k = it->second;
                return true;
            }
            from = it->second.end();
        }
        if (parseAt(p, k))
            return true;
        // Von der letzten bekannten Grenze vorwärts laufen (nur Köpfe lesen)
        while (from <= p) {
            Klv c;
            if (!parseAt(from, &c))
                return false;
            if (p < c.end()) {
                *k = c;
                return true;
            }
            from = c.end();
        }
        return false;
    }

    bool transformed(const Klv &k) const
    {
        if (std::memcmp(k.key, kTriplet, 16) == 0)
            return true;
        int element = 0;
        return m_spec.eye && isPictureElement(k.key, &element) && element != m_spec.eye;
    }

    bool build(const Klv &k)
    {
        const qint64 total = k.end() - k.start;
        m_cache.resize(int(total));
        auto *out = reinterpret_cast<uchar *>(m_cache.data());
        m_cacheStart = -1;

        int element = 0;
        if (std::memcmp(k.key, kTriplet, 16) != 0) {
            // anderes Auge -> Fill
            if (!writeFill(out, total))
                return false;
            m_cacheStart = k.start;
            return true;
        }

        QByteArray value(int(k.valueLen), '\0');
        if (!m_file.seek(k.start + k.headerLen) || m_file.read(value.data(), k.valueLen) != k.valueLen)
            return false;
        const auto *v = reinterpret_cast<const uchar *>(value.constData());
        const uchar *end = v + value.size();
        const uchar *p = v;
        quint64 len = 0;
        int hdr = 0;
        // CryptographicContextLink
        if (!berAt(p, end, &len, &hdr)) return false;
        p += hdr + len;
        // PlaintextOffset
        if (!berAt(p, end, &len, &hdr) || len != 8) return false;
        const quint64 plaintextOffset = rb(p + hdr, 8);
        p += hdr + len;
        // SourceKey
        if (!berAt(p, end, &len, &hdr) || len != 16) return false;
        const uchar *sourceKey = p + hdr;
        p += hdr + len;
        // SourceLength
        if (!berAt(p, end, &len, &hdr) || len != 8) return false;
        const quint64 sourceLength = rb(p + hdr, 8);
        p += hdr + len;
        // EncryptedSourceValue: IV | Prüfwert | Klartext-Anteil | verschlüsselter Rest
        if (!berAt(p, end, &len, &hdr)) return false;
        const uchar *esv = p + hdr;
        const quint64 esvLen = len;
        if (esv + esvLen > end || esvLen < 32 + plaintextOffset || sourceLength > esvLen - 32)
            return false;

        const int headerBytes = 16 + berSizeFor(sourceLength);
        if (qint64(headerBytes + sourceLength) + 17 > total)
            return false;

        // Unerwünschtes Auge eines 3D-Triplets: gar nicht erst entschlüsseln
        if (m_spec.eye && isPictureElement(sourceKey, &element) && element != m_spec.eye) {
            if (!writeFill(out, total))
                return false;
            m_cacheStart = k.start;
            return true;
        }

        std::memcpy(out, sourceKey, 16);
        putBer(out + 16, sourceLength, headerBytes - 16);
        uchar *data = out + headerBytes;
        const qint64 encLen = qint64(esvLen) - 32 - qint64(plaintextOffset);
        if (m_spec.key.isEmpty()) {
            ++g_missingKeys;
            std::memcpy(data, esv + 32, size_t(sourceLength)); // unlesbar, aber strukturell gültig
        } else {
            uchar iv[16];
            std::memcpy(iv, esv, 16);
            uchar check[16];
            m_aes.decrypt(iv, esv + 16, check, 16);
            if (std::memcmp(check, kCheck, 16) != 0)
                ++g_keyErrors;
            std::memcpy(data, esv + 32, size_t(plaintextOffset));
            const qint64 blocks = encLen - encLen % 16;
            m_plain.resize(int(std::max<qint64>(blocks, 16)));
            m_aes.decrypt(iv, esv + 32 + plaintextOffset, reinterpret_cast<uchar *>(m_plain.data()), int(blocks));
            const qint64 copy = std::min<qint64>(qint64(sourceLength) - qint64(plaintextOffset), blocks);
            if (copy > 0)
                std::memcpy(data + plaintextOffset, m_plain.constData(), size_t(copy));
        }
        if (!writeFill(data + sourceLength, total - headerBytes - qint64(sourceLength)))
            return false;
        m_cacheStart = k.start;
        return true;
    }

    StreamSpec m_spec;
    QFile m_file;
    bool m_ok = false;
    qint64 m_size = 0;
    qint64 m_pos = 0;
    std::map<qint64, Klv> m_known;
    QByteArray m_cache;
    QByteArray m_plain;
    qint64 m_cacheStart = -1;
    DcpCrypto::AesCbc m_aes;
};

int openStream(void *, char *uri, mpv_stream_cb_info *info)
{
    const QString url = QString::fromUtf8(uri);
    const int id = url.section(QStringLiteral("://"), 1).section(QLatin1Char('/'), 0, 0).toInt();
    StreamSpec spec;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_streams.contains(id))
            return MPV_ERROR_LOADING_FAILED;
        spec = g_streams.value(id);
    }
    auto *r = new Reader(spec);
    if (!r->ok()) {
        delete r;
        return MPV_ERROR_LOADING_FAILED;
    }
    info->cookie = r;
    info->read_fn = [](void *c, char *buf, uint64_t n) -> int64_t { return static_cast<Reader *>(c)->read(buf, qint64(n)); };
    info->seek_fn = [](void *c, int64_t off) -> int64_t {
        return static_cast<Reader *>(c)->seek(off) ? off : int64_t(MPV_ERROR_GENERIC);
    };
    info->size_fn = [](void *c) -> int64_t { return static_cast<Reader *>(c)->size(); };
    info->close_fn = [](void *c) { delete static_cast<Reader *>(c); };
    return 0;
}

} // namespace

int registerStream(const StreamSpec &spec)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    // gleiche Datei/Schlüssel/Auge -> gleiche URL (mpv teilt dann den Demuxer)
    for (auto it = g_streams.cbegin(); it != g_streams.cend(); ++it)
        if (it->file == spec.file && it->key == spec.key && it->eye == spec.eye)
            return it.key();
    const int id = g_nextId++;
    g_streams.insert(id, spec);
    return id;
}

QString streamUrl(int id)
{
    return QStringLiteral("lumendcp://%1").arg(id);
}

void clearStreams()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_streams.clear();
    g_keyErrors = 0;
    g_missingKeys = 0;
}

void attachProtocol(mpv_handle *mpv)
{
    const int err = mpv_stream_cb_add_ro(mpv, "lumendcp", nullptr, [](void *ud, char *uri, mpv_stream_cb_info *info) {
        return openStream(ud, uri, info);
    });
    if (err < 0)
        qWarning("lumendcp: Protokoll nicht angemeldet: %s", mpv_error_string(err));
}

struct EssenceReader::Impl
{
    explicit Impl(const StreamSpec &spec) : reader(spec) {}
    Reader reader;
    std::vector<std::pair<qint64, qint64>> elements; // KLV-Start, Gesamtlänge
    QByteArray buffer;
};

EssenceReader::EssenceReader(const StreamSpec &spec, std::function<bool(const unsigned char *key)> match)
    : d(new Impl(spec))
{
    if (!d->reader.ok())
        return;
    // Nur KLV-Köpfe der Rohdatei lesen (bei Triplets zusätzlich den SourceKey)
    QFile f(spec.file);
    if (!f.open(QIODevice::ReadOnly))
        return;
    const qint64 size = f.size();
    qint64 pos = 0;
    uchar h[128];
    while (pos + 17 <= size) {
        if (!f.seek(pos))
            break;
        const qint64 got = f.read(reinterpret_cast<char *>(h), std::min<qint64>(sizeof h, size - pos));
        if (got < 17 || std::memcmp(h, kUl, 4) != 0)
            break;
        quint64 len = 0;
        int hdr = 0;
        if (!berAt(h + 16, h + got, &len, &hdr))
            break;
        const qint64 total = 16 + hdr + qint64(len);
        if (pos + total > size)
            break;
        bool hit = false;
        if (std::memcmp(h, kTriplet, 16) == 0) {
            // CryptographicContextLink, PlaintextOffset, SourceKey
            const uchar *p = h + 16 + hdr, *end = h + got;
            quint64 l = 0;
            int bh = 0;
            if (berAt(p, end, &l, &bh) && (p += bh + l) < end && berAt(p, end, &l, &bh) && l == 8 && (p += bh + l) < end
                && berAt(p, end, &l, &bh) && l == 16 && p + bh + 16 <= end)
                hit = match(p + bh);
        } else {
            hit = match(h);
        }
        if (hit)
            d->elements.emplace_back(pos, total);
        pos += total;
    }
}

EssenceReader::~EssenceReader()
{
    delete d;
}

bool EssenceReader::ok() const
{
    return !d->elements.empty();
}

int EssenceReader::count() const
{
    return int(d->elements.size());
}

QByteArray EssenceReader::element(int index)
{
    if (index < 0 || index >= count())
        return {};
    const auto [start, total] = d->elements[size_t(index)];
    d->buffer.resize(int(total));
    if (!d->reader.seek(start))
        return {};
    qint64 got = 0;
    while (got < total) {
        const qint64 n = d->reader.read(d->buffer.data() + got, total - got);
        if (n <= 0)
            return {};
        got += n;
    }
    // Erstes KLV = (entschlüsseltes) Essenz-Element
    const auto *p = reinterpret_cast<const uchar *>(d->buffer.constData());
    quint64 len = 0;
    int hdr = 0;
    if (!berAt(p + 16, p + total, &len, &hdr) || 16 + hdr + qint64(len) > total)
        return {};
    return d->buffer.mid(16 + hdr, int(len));
}

QByteArray readAllTransformed(const StreamSpec &spec, qint64 maxBytes)
{
    Reader r(spec);
    if (!r.ok())
        return {};
    const qint64 total = maxBytes >= 0 ? std::min(maxBytes, r.size()) : r.size();
    QByteArray out(int(total), '\0');
    qint64 pos = 0;
    while (pos < total) {
        const qint64 got = r.read(out.data() + pos, std::min<qint64>(64 * 1024, total - pos));
        if (got <= 0)
            break;
        pos += got;
    }
    out.resize(int(pos));
    return out;
}

TimedText readTimedText(const QString &file, const QByteArray &key)
{
    TimedText tt;
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly) || f.size() > 512 * 1024 * 1024)
        return tt;
    // Durch den Leser schicken: entschlüsselt Triplets, danach Klartext-KLVs
    StreamSpec spec{file, key, 0};
    const QByteArray data = readAllTransformed(spec);
    const auto *p = reinterpret_cast<const uchar *>(data.constData());
    const uchar *end = p + data.size();
    while (p + 17 <= end && std::memcmp(p, kUl, 4) == 0) {
        quint64 len = 0;
        int hdr = 0;
        if (!berAt(p + 16, end, &len, &hdr) || p + 16 + hdr + len > end)
            break;
        const QByteArray value(reinterpret_cast<const char *>(p + 16 + hdr), int(len));
        // Essenz (Timed Text) oder Generic Stream Data (Fonts/Bilder); Header-Sets
        // beginnen nie mit diesen Signaturen
        if (p[4] == 0x01) {
            const QByteArray head = value.left(512);
            if (tt.xml.isEmpty() && (head.contains("<?xml") || head.contains("SubtitleReel")))
                tt.xml = value;
            else if (value.startsWith(QByteArray::fromHex("00010000")) || value.startsWith("OTTO") || value.startsWith("true")
                     || value.startsWith("\x89PNG"))
                tt.resources.append(value);
        }
        p += 16 + hdr + len;
    }
    return tt;
}

} // namespace Dcp
