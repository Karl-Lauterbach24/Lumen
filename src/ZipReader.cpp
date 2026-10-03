#include "ZipReader.h"

#include <QFile>
#include <QtEndian>

#ifdef LUMEN_HAVE_ZLIB
#include <zlib.h>
#endif

namespace Zip {

static quint16 u16(const QByteArray &d, qint64 at) { return qFromLittleEndian<quint16>(d.constData() + at); }
static quint32 u32(const QByteArray &d, qint64 at) { return qFromLittleEndian<quint32>(d.constData() + at); }

static bool inflateRaw(const QByteArray &in, qint64 size, QByteArray *out)
{
#ifdef LUMEN_HAVE_ZLIB
    out->resize(int(size));
    z_stream z{};
    if (inflateInit2(&z, -MAX_WBITS) != Z_OK)
        return false;
    z.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(in.constData()));
    z.avail_in = uInt(in.size());
    z.next_out = reinterpret_cast<Bytef *>(out->data());
    z.avail_out = uInt(out->size());
    const int r = inflate(&z, Z_FINISH);
    inflateEnd(&z);
    return r == Z_STREAM_END && z.total_out == uLong(size);
#else
    Q_UNUSED(in);
    Q_UNUSED(size);
    Q_UNUSED(out);
    return false;
#endif
}

QList<Entry> read(const QString &file, const QString &suffix, QString *error, qint64 maxBytes)
{
    QList<Entry> out;
    auto fail = [&](const QString &e) {
        if (error)
            *error = e;
        return QList<Entry>();
    };
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return fail(f.errorString());
    if (f.size() > maxBytes)
        return fail(QStringLiteral("archive too large"));
    const QByteArray d = f.readAll();
    // Ende des Inhaltsverzeichnisses: vom Dateiende rückwärts suchen (dahinter höchstens ein Kommentar)
    qint64 eocd = -1;
    for (qint64 i = d.size() - 22; i >= 0 && i >= d.size() - 22 - 65535; --i) {
        if (u32(d, i) == 0x06054b50) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0)
        return fail(QStringLiteral("not a ZIP archive"));
    const int count = u16(d, eocd + 10);
    qint64 at = u32(d, eocd + 16);
    qint64 total = 0;
    for (int n = 0; n < count; ++n) {
        if (at + 46 > d.size() || u32(d, at) != 0x02014b50)
            return fail(QStringLiteral("damaged ZIP directory"));
        const int flags = u16(d, at + 8), method = u16(d, at + 10);
        const qint64 packed = u32(d, at + 20), size = u32(d, at + 24);
        const int nameLen = u16(d, at + 28), extraLen = u16(d, at + 30), commentLen = u16(d, at + 32);
        const qint64 local = u32(d, at + 42);
        const QString name = QString::fromUtf8(d.mid(at + 46, nameLen));
        at += 46 + nameLen + extraLen + commentLen;
        if (name.endsWith(QLatin1Char('/')) || (!suffix.isEmpty() && !name.endsWith(suffix, Qt::CaseInsensitive)))
            continue;
        if ((flags & 1) || (method != 0 && method != 8))
            continue; // verschlüsselt oder unbekanntes Verfahren
        if (local + 30 > d.size() || u32(d, local) != 0x04034b50)
            return fail(QStringLiteral("damaged ZIP entry"));
        const qint64 data = local + 30 + u16(d, local + 26) + u16(d, local + 28);
        if (data + packed > d.size() || (total += size) > maxBytes)
            return fail(QStringLiteral("damaged or too large ZIP entry"));
        Entry e;
        e.name = name;
        if (method == 0)
            e.data = d.mid(data, packed);
        else if (!inflateRaw(d.mid(data, packed), size, &e.data))
            return fail(QStringLiteral("cannot unpack %1").arg(name));
        out.append(e);
    }
    return out;
}

} // namespace Zip
