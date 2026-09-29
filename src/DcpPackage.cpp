#include "DcpPackage.h"
#include "Tr.h"

#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cstring>

namespace Dcp {

namespace {

// Elementname ohne Namensraum-Präfix ("cpl:MainPicture" -> "MainPicture")
QString local(const QDomElement &e)
{
    const QString t = e.tagName();
    const int i = t.indexOf(QLatin1Char(':'));
    return i >= 0 ? t.mid(i + 1) : t;
}

QDomElement child(const QDomElement &parent, const QString &name)
{
    for (QDomElement c = parent.firstChildElement(); !c.isNull(); c = c.nextSiblingElement())
        if (local(c) == name)
            return c;
    return {};
}

QList<QDomElement> children(const QDomElement &parent, const QString &name = QString())
{
    QList<QDomElement> out;
    for (QDomElement c = parent.firstChildElement(); !c.isNull(); c = c.nextSiblingElement())
        if (name.isEmpty() || local(c) == name)
            out.append(c);
    return out;
}

QString text(const QDomElement &parent, const QString &name)
{
    return child(parent, name).text().trimmed();
}

Rational rational(const QString &s, Rational def = {})
{
    const QStringList p = s.simplified().split(QLatin1Char(' '));
    Rational r;
    if (p.size() == 2) {
        r.num = p[0].toLongLong();
        r.den = p[1].toLongLong();
    } else if (p.size() == 1 && !p[0].isEmpty()) {
        r.num = p[0].toLongLong();
        r.den = 1;
    }
    return r.valid() ? r : def;
}

bool loadXml(const QString &file, QDomDocument *doc)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return bool(doc->setContent(f.readAll()));
}

QString findAssetMap(const QString &root)
{
    for (const char *n : {"ASSETMAP.xml", "ASSETMAP", "assetmap.xml", "assetmap"}) {
        const QString p = QDir(root).filePath(QLatin1String(n));
        if (QFileInfo(p).isFile())
            return p;
    }
    return {};
}

// ASSETMAP: UUID -> absoluter Pfad; PKL-Dateien merken
void readAssetMap(const QString &root, QHash<QString, Asset> *assets, QStringList *pkls)
{
    QDomDocument doc;
    if (!loadXml(findAssetMap(root), &doc))
        return;
    const QDomElement list = child(doc.documentElement(), QStringLiteral("AssetList"));
    for (const QDomElement &a : children(list, QStringLiteral("Asset"))) {
        const QString id = normalizeUuid(text(a, QStringLiteral("Id")));
        const QDomElement chunk = child(child(a, QStringLiteral("ChunkList")), QStringLiteral("Chunk"));
        QString rel = text(chunk, QStringLiteral("Path"));
        if (rel.startsWith(QLatin1String("file://")))
            rel = rel.mid(7);
        if (id.isEmpty() || rel.isEmpty())
            continue;
        const QString path = QDir::cleanPath(QDir(root).filePath(rel));
        if (!QFileInfo::exists(path))
            continue;
        Asset &as = (*assets)[id];
        if (as.path.isEmpty()) {
            as.id = id;
            as.path = path;
        }
        if (text(a, QStringLiteral("PackingList")).compare(QLatin1String("true"), Qt::CaseInsensitive) == 0)
            pkls->append(path);
    }
}

void readPkl(const QString &file, QHash<QString, Asset> *assets, QStringList *cpls)
{
    QDomDocument doc;
    if (!loadXml(file, &doc))
        return;
    const QDomElement list = child(doc.documentElement(), QStringLiteral("AssetList"));
    for (const QDomElement &a : children(list, QStringLiteral("Asset"))) {
        const QString id = normalizeUuid(text(a, QStringLiteral("Id")));
        if (!assets->contains(id))
            continue;
        Asset &as = (*assets)[id];
        as.size = text(a, QStringLiteral("Size")).toLongLong();
        as.hash = text(a, QStringLiteral("Hash"));
        as.type = text(a, QStringLiteral("Type"));
        if (as.type.contains(QLatin1String("CPL")) || as.type == QLatin1String("text/xml"))
            cpls->append(id);
    }
}

Kind kindFor(const QString &tag, const QDomElement &e)
{
    if (tag == QLatin1String("MainPicture"))
        return Kind::Picture;
    if (tag == QLatin1String("MainStereoscopicPicture"))
        return Kind::StereoPicture;
    if (tag == QLatin1String("MainSound"))
        return Kind::Sound;
    if (tag == QLatin1String("MainSubtitle"))
        return Kind::Subtitle;
    if (tag == QLatin1String("MainClosedCaption") || tag == QLatin1String("ClosedCaption"))
        return Kind::ClosedCaption;
    if (tag == QLatin1String("AuxData")) {
        // Dolby Atmos (IAB/Aux Data) – nicht dekodierbar, nur anzeigen
        const QString type = text(e, QStringLiteral("DataType"));
        if (type.contains(QLatin1String("0e090604"), Qt::CaseInsensitive) || type.contains(QLatin1String("atmos"), Qt::CaseInsensitive))
            return Kind::Atmos;
    }
    return Kind::Other;
}

bool parseCpl(const QString &file, const QHash<QString, Asset> &assets, Cpl *cpl)
{
    QDomDocument doc;
    if (!loadXml(file, &doc))
        return false;
    const QDomElement root = doc.documentElement();
    if (local(root) != QLatin1String("CompositionPlaylist"))
        return false;
    cpl->file = file;
    cpl->smpte = root.namespaceURI().contains(QLatin1String("429-7")) || root.attribute(QStringLiteral("xmlns")).contains(QLatin1String("429-7"))
                 || !root.attribute(QStringLiteral("xmlns")).contains(QLatin1String("digicine.com"));
    cpl->id = normalizeUuid(text(root, QStringLiteral("Id")));
    cpl->title = text(root, QStringLiteral("ContentTitleText"));
    cpl->annotation = text(root, QStringLiteral("AnnotationText"));
    cpl->issuer = text(root, QStringLiteral("Issuer"));
    cpl->creator = text(root, QStringLiteral("Creator"));
    cpl->issueDate = text(root, QStringLiteral("IssueDate"));
    cpl->contentKind = text(root, QStringLiteral("ContentKind"));
    cpl->contentVersion = text(child(root, QStringLiteral("ContentVersion")), QStringLiteral("LabelText"));
    for (const QDomElement &r : children(child(root, QStringLiteral("RatingList")), QStringLiteral("Rating"))) {
        const QString label = text(r, QStringLiteral("Label"));
        if (!label.isEmpty())
            cpl->ratings << QStringLiteral("%1 %2").arg(text(r, QStringLiteral("Agency")).section(QLatin1Char('/'), -1), label).trimmed();
    }

    for (const QDomElement &reelEl : children(child(root, QStringLiteral("ReelList")), QStringLiteral("Reel"))) {
        Reel reel;
        reel.id = normalizeUuid(text(reelEl, QStringLiteral("Id")));
        for (const QDomElement &a : children(child(reelEl, QStringLiteral("AssetList")))) {
            const QString tag = local(a);
            if (tag == QLatin1String("MainMarkers")) {
                for (const QDomElement &m : children(child(a, QStringLiteral("MarkerList")), QStringLiteral("Marker")))
                    reel.markers.append({text(m, QStringLiteral("Label")), text(m, QStringLiteral("Offset")).toLongLong()});
                continue;
            }
            if (tag == QLatin1String("CompositionMetadataAsset"))
                continue;
            ReelAsset ra;
            ra.kind = kindFor(tag, a);
            ra.id = normalizeUuid(text(a, QStringLiteral("Id")));
            ra.editRate = rational(text(a, QStringLiteral("EditRate")), cpl->editRate);
            ra.frameRate = rational(text(a, QStringLiteral("FrameRate")), ra.editRate);
            ra.intrinsic = text(a, QStringLiteral("IntrinsicDuration")).toLongLong();
            ra.entryPoint = text(a, QStringLiteral("EntryPoint")).toLongLong();
            const QString dur = text(a, QStringLiteral("Duration"));
            ra.duration = dur.isEmpty() ? std::max<qint64>(0, ra.intrinsic - ra.entryPoint) : dur.toLongLong();
            ra.keyId = normalizeUuid(text(a, QStringLiteral("KeyId")));
            ra.language = text(a, QStringLiteral("Language"));
            if (ra.kind == Kind::Picture || ra.kind == Kind::StereoPicture) {
                const QString aspect = text(a, QStringLiteral("ScreenAspectRatio"));
                if (!aspect.isEmpty() && cpl->aspect.isEmpty()) {
                    const Rational ar = rational(aspect);
                    cpl->aspect = ar.valid() ? QString::number(ar.value(), 'f', 2) + QStringLiteral(":1") : aspect;
                }
                if (!cpl->editRate.valid() || reel.assets.isEmpty())
                    cpl->editRate = ra.editRate;
            }
            const auto it = assets.constFind(ra.id);
            if (it != assets.constEnd())
                ra.file = it->path;
            else if (ra.kind != Kind::Other)
                cpl->missing << ra.id;
            if (!ra.file.isEmpty() && ra.file.endsWith(QLatin1String(".mxf"), Qt::CaseInsensitive))
                ra.mxf = probeMxf(ra.file);
            if (!ra.editRate.valid())
                ra.editRate = Rational{24, 1};
            reel.assets.append(ra);
        }
        // Rollendauer: Bild bestimmt die Länge, sonst die erste Spur
        const ReelAsset *pic = reel.find(Kind::Picture);
        if (!pic)
            pic = reel.find(Kind::StereoPicture);
        if (!pic && !reel.assets.isEmpty())
            pic = &reel.assets.constFirst();
        if (pic) {
            reel.editRate = pic->editRate;
            reel.duration = pic->duration;
        }
        cpl->reels.append(reel);
    }
    if (!cpl->editRate.valid())
        cpl->editRate = Rational{24, 1};
    if (cpl->title.isEmpty())
        cpl->title = cpl->annotation.isEmpty() ? QFileInfo(file).completeBaseName() : cpl->annotation;
    return !cpl->id.isEmpty();
}

// --- MXF-Kopf ---------------------------------------------------------------

quint64 rb(const uchar *p, int n)
{
    quint64 v = 0;
    for (int i = 0; i < n; ++i)
        v = (v << 8) | p[i];
    return v;
}

// BER-Länge ab p (max. end). Liefert Länge und setzt *hdr auf die Anzahl Bytes.
bool ber(const uchar *p, const uchar *end, quint64 *len, int *hdr)
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

const uchar kUlPrefix[4] = {0x06, 0x0e, 0x2b, 0x34};

} // namespace

const ReelAsset *Reel::find(Kind k) const
{
    for (const ReelAsset &a : assets)
        if (a.kind == k)
            return &a;
    return nullptr;
}

bool Cpl::encrypted() const
{
    for (const Reel &r : reels)
        for (const ReelAsset &a : r.assets)
            if (a.encrypted())
                return true;
    return false;
}

bool Cpl::stereoscopic() const
{
    for (const Reel &r : reels)
        if (r.find(Kind::StereoPicture))
            return true;
    return false;
}

QStringList Cpl::keyIds() const
{
    QStringList ids;
    for (const Reel &r : reels)
        for (const ReelAsset &a : r.assets)
            if (a.encrypted() && !ids.contains(a.keyId))
                ids << a.keyId;
    return ids;
}

double Cpl::seconds() const
{
    double s = 0;
    for (const Reel &r : reels)
        s += r.seconds();
    return s;
}

QString kindName(Kind k)
{
    switch (k) {
    case Kind::Picture: return QStringLiteral("picture");
    case Kind::StereoPicture: return QStringLiteral("stereo");
    case Kind::Sound: return QStringLiteral("sound");
    case Kind::Subtitle: return QStringLiteral("subtitle");
    case Kind::ClosedCaption: return QStringLiteral("ccap");
    case Kind::Atmos: return QStringLiteral("atmos");
    default: return QStringLiteral("other");
    }
}

QVariantMap Cpl::toVariant() const
{
    QVariantMap m;
    m["id"] = id;
    m["file"] = file;
    m["root"] = root;
    m["title"] = title;
    m["annotation"] = annotation;
    m["issuer"] = issuer;
    m["creator"] = creator;
    m["issueDate"] = issueDate;
    m["contentKind"] = contentKind;
    m["contentVersion"] = contentVersion;
    m["aspect"] = aspect;
    m["ratings"] = ratings;
    m["standard"] = smpte ? QStringLiteral("SMPTE") : QStringLiteral("Interop");
    m["editRate"] = editRate.value();
    m["reels"] = int(reels.size());
    m["duration"] = seconds();
    m["encrypted"] = encrypted();
    m["stereo"] = stereoscopic();
    m["missing"] = missing;
    m["keyIds"] = keyIds();

    int width = 0, height = 0, channels = 0, rate = 0;
    bool subs = false, atmos = false, ccap = false;
    QStringList langs;
    for (const Reel &r : reels) {
        for (const ReelAsset &a : r.assets) {
            if ((a.kind == Kind::Picture || a.kind == Kind::StereoPicture) && a.mxf.width)
                width = a.mxf.width, height = a.mxf.height;
            if (a.kind == Kind::Sound && a.mxf.channels)
                channels = a.mxf.channels, rate = a.mxf.sampleRate;
            subs |= a.kind == Kind::Subtitle;
            ccap |= a.kind == Kind::ClosedCaption;
            atmos |= a.kind == Kind::Atmos;
            if (a.kind == Kind::Subtitle && !a.language.isEmpty() && !langs.contains(a.language))
                langs << a.language;
        }
    }
    m["width"] = width;
    m["height"] = height;
    m["channels"] = channels;
    m["sampleRate"] = rate;
    m["subtitles"] = subs;
    m["subtitleLanguages"] = langs;
    m["closedCaptions"] = ccap;
    m["atmos"] = atmos;
    QString res;
    if (width >= 3800)
        res = QStringLiteral("4K");
    else if (width >= 1900)
        res = QStringLiteral("2K");
    m["resolution"] = res;
    QVariantList markers;
    double t = 0;
    for (const Reel &r : reels) {
        for (const Marker &mk : r.markers)
            markers.append(QVariantMap{{"label", mk.label}, {"time", t + (r.editRate.valid() ? mk.offset / r.editRate.value() : 0)}});
        t += r.seconds();
    }
    m["markers"] = markers;
    return m;
}

QString normalizeUuid(QString s)
{
    s = s.trimmed().toLower();
    if (s.startsWith(QLatin1String("urn:uuid:")))
        s = s.mid(9);
    return s;
}

bool isDcp(const QString &root)
{
    return !findAssetMap(root).isEmpty();
}

MxfInfo probeMxf(const QString &file)
{
    MxfInfo info;
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return info;
    // Header-Partition + Metadaten: bei DCPs typischerweise < 64 KiB
    const QByteArray head = f.read(4 * 1024 * 1024);
    const auto *p = reinterpret_cast<const uchar *>(head.constData());
    const uchar *end = p + head.size();
    if (head.size() < 20 || std::memcmp(p, kUlPrefix, 4) != 0)
        return info;
    info.valid = true;

    // Kopf-Partition: HeaderByteCount begrenzt den Suchbereich
    quint64 len = 0;
    int h = 0;
    if (!ber(p + 16, end, &len, &h))
        return info;
    const uchar *v = p + 16 + h;
    const uchar *pos = v + len;
    const uchar *limit = end;
    if (v + 40 <= end) {
        const quint64 headerBytes = rb(v + 32, 8);
        if (headerBytes > 0 && pos + headerBytes + 1024 < end)
            limit = pos + headerBytes + 1024;
        // Essence-Container-Liste: verschlüsselter Generic Container?
        if (v + 88 <= end) {
            const quint32 count = quint32(rb(v + 80, 4));
            const quint32 size = quint32(rb(v + 84, 4));
            for (quint32 i = 0; i < count && size == 16 && v + 88 + (i + 1) * 16 <= end; ++i) {
                const uchar *ul = v + 88 + i * 16;
                if (ul[8] == 0x0d && ul[9] == 0x01 && ul[10] == 0x03 && ul[11] == 0x01 && ul[12] == 0x02 && ul[13] == 0x0b)
                    info.encrypted = true;
            }
        }
    }

    bool j2k = false, wave = false, rgba = false, tt = false, dcdata = false;
    while (pos + 17 <= limit) {
        if (std::memcmp(pos, kUlPrefix, 4) != 0)
            break;
        if (!ber(pos + 16, limit, &len, &h))
            break;
        const uchar *key = pos;
        const uchar *val = pos + 16 + h;
        const uchar *next = val + len;
        if (next > limit)
            break;
        // Header-Metadaten (lokale Sets): 06.0e.2b.34.02.53.01.01.0d.01.01.01.01.01.XX.00
        if (key[4] == 0x02 && key[5] == 0x53 && key[8] == 0x0d && key[9] == 0x01) {
            const uchar type = key[14];
            if (key[10] == 0x04 && key[11] == 0x01 && key[12] == 0x02 && key[13] == 0x02)
                info.encrypted = true; // CryptographicContext
            if (key[12] == 0x01 && key[13] == 0x01) {
                if (type == 0x29 || type == 0x28)
                    rgba = true;
                else if (type == 0x5a)
                    j2k = true;
                else if (type == 0x48 || type == 0x42 || type == 0x47)
                    wave = true;
                else if (type == 0x64)
                    tt = true;
                else if (type == 0x5b)
                    dcdata = true;
                else if (type == 0x63)
                    info.stereoscopic = true;
            }
            for (const uchar *t = val; t + 4 <= next;) {
                const int tag = int(rb(t, 2));
                const int l = int(rb(t + 2, 2));
                const uchar *d = t + 4;
                if (d + l > next)
                    break;
                switch (tag) {
                case 0x3203: if (l == 4) info.width = int(rb(d, 4)); break;
                case 0x3202: if (l == 4) info.height = int(rb(d, 4)); break;
                case 0x3d07: if (l == 4) info.channels = int(rb(d, 4)); break;
                case 0x3d01: if (l == 4) info.bitsPerSample = int(rb(d, 4)); break;
                case 0x3d03:
                    if (l == 8 && rb(d + 4, 4))
                        info.sampleRate = int(rb(d, 4) / rb(d + 4, 4));
                    break;
                case 0x4804: // TrackNumber: 0x15 02 08 xx = zwei Bildelemente je Edit Unit (3D)
                    if (l == 4 && d[0] == 0x15 && d[1] == 0x02)
                        info.stereoscopic = true;
                    break;
                default: break;
                }
                t = d + l;
            }
        }
        // Erstes Essenz-Element erreicht -> Kopf zu Ende
        if (key[4] == 0x01 && key[5] == 0x02 && key[8] == 0x0d && key[9] == 0x01 && key[10] == 0x03)
            break;
        pos = next;
    }
    if (rgba || j2k)
        info.codec = QStringLiteral("J2K");
    else if (wave)
        info.codec = QStringLiteral("PCM");
    else if (tt)
        info.codec = QStringLiteral("TimedText");
    else if (dcdata)
        info.codec = QStringLiteral("DCData");
    return info;
}

Package scan(const QString &input, const QStringList &extraRoots)
{
    Package pkg;
    QString root = input;
    QFileInfo fi(input);
    if (fi.isFile())
        root = fi.absolutePath();
    root = QDir::cleanPath(root);
    pkg.root = root;
    if (!isDcp(root)) {
        pkg.error = LTR("Kein DCP: ASSETMAP fehlt");
        return pkg;
    }

    QStringList pkls;
    readAssetMap(root, &pkg.assets, &pkls);
    QStringList cplIds;
    for (const QString &pkl : std::as_const(pkls))
        readPkl(pkl, &pkg.assets, &cplIds);

    // Weitere Pakete (Original Version zu einer Version File): benachbarte
    // DCP-Ordner und ausdrücklich angegebene Ordner einlesen
    QStringList roots = extraRoots;
    const QDir parent = QFileInfo(root).dir();
    for (const QString &d : parent.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        roots << parent.filePath(d);
    QSet<QString> seen{root};
    for (const QString &r : std::as_const(roots)) {
        const QString clean = QDir::cleanPath(r);
        if (seen.contains(clean) || !isDcp(clean))
            continue;
        seen.insert(clean);
        QHash<QString, Asset> other;
        QStringList otherPkls, ignore;
        readAssetMap(clean, &other, &otherPkls);
        for (const QString &pkl : std::as_const(otherPkls))
            readPkl(pkl, &other, &ignore);
        for (auto it = other.cbegin(); it != other.cend(); ++it)
            if (!pkg.assets.contains(it.key()))
                pkg.assets.insert(it.key(), it.value());
    }

    // CPLs: aus der PKL, notfalls alle XML-Dateien im Paket prüfen
    QStringList cplFiles;
    for (const QString &id : std::as_const(cplIds))
        if (pkg.assets.contains(id))
            cplFiles << pkg.assets.value(id).path;
    if (fi.isFile() && fi.suffix().compare(QLatin1String("xml"), Qt::CaseInsensitive) == 0 && !findAssetMap(input).endsWith(fi.fileName()))
        cplFiles = QStringList{fi.absoluteFilePath()};
    if (cplFiles.isEmpty())
        for (const QString &x : QDir(root).entryList({QStringLiteral("*.xml")}, QDir::Files))
            cplFiles << QDir(root).filePath(x);
    cplFiles.removeDuplicates();

    for (const QString &f : std::as_const(cplFiles)) {
        Cpl cpl;
        cpl.root = root;
        if (parseCpl(f, pkg.assets, &cpl))
            pkg.cpls.append(cpl);
    }
    // Spielfilm zuerst, dann nach Titel
    static const QStringList order = {"feature", "short", "trailer", "teaser", "advertisement", "transitional", "rating", "policy", "psa", "test"};
    std::stable_sort(pkg.cpls.begin(), pkg.cpls.end(), [](const Cpl &a, const Cpl &b) {
        const int ka = order.indexOf(a.contentKind.toLower()), kb = order.indexOf(b.contentKind.toLower());
        return (ka < 0 ? 99 : ka) < (kb < 0 ? 99 : kb);
    });
    if (pkg.cpls.isEmpty())
        pkg.error = LTR("Keine Composition Playlist (CPL) gefunden");
    return pkg;
}

} // namespace Dcp
