#include "DcpSubtitles.h"
#include "DcpStream.h"

#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Dcp {

namespace {

constexpr double kResX = 1920;
constexpr double kResY = 1080;

QString local(const QDomElement &e)
{
    const QString t = e.tagName();
    const int i = t.indexOf(QLatin1Char(':'));
    return i >= 0 ? t.mid(i + 1) : t;
}

// Attribut ohne Rücksicht auf Groß-/Kleinschreibung (Interop "VAlign", SMPTE "Valign")
QString attr(const QDomElement &e, const QString &name)
{
    const QDomNamedNodeMap attrs = e.attributes();
    for (int i = 0; i < attrs.count(); ++i) {
        const QDomAttr a = attrs.item(i).toAttr();
        QString n = a.name();
        const int c = n.indexOf(QLatin1Char(':'));
        if (c >= 0)
            n = n.mid(c + 1);
        if (n.compare(name, Qt::CaseInsensitive) == 0)
            return a.value().trimmed();
    }
    return {};
}

struct Style
{
    QString font;
    double size = 42;
    quint32 color = 0xFFFFFFFF;       // AARRGGBB
    quint32 effectColor = 0xFF000000;
    QString effect = QStringLiteral("border");
    bool italic = false;
    bool bold = false;
    bool underline = false;
};

void applyFont(const QDomElement &f, Style *s, const QHash<QString, QString> &fonts)
{
    const QString id = attr(f, QStringLiteral("Id")).isEmpty() ? attr(f, QStringLiteral("ID")) : attr(f, QStringLiteral("Id"));
    if (!id.isEmpty() && fonts.contains(id))
        s->font = fonts.value(id);
    bool ok = false;
    const double size = attr(f, QStringLiteral("Size")).toDouble(&ok);
    if (ok && size > 0)
        s->size = size;
    const QString color = attr(f, QStringLiteral("Color"));
    if (color.size() == 8)
        s->color = color.toUInt(nullptr, 16);
    else if (color.size() == 6)
        s->color = 0xFF000000 | color.toUInt(nullptr, 16);
    const QString ec = attr(f, QStringLiteral("EffectColor"));
    if (ec.size() == 8)
        s->effectColor = ec.toUInt(nullptr, 16);
    const QString effect = attr(f, QStringLiteral("Effect"));
    if (!effect.isEmpty())
        s->effect = effect.toLower();
    const QString italic = attr(f, QStringLiteral("Italic"));
    if (!italic.isEmpty())
        s->italic = italic == QLatin1String("yes") || italic == QLatin1String("true");
    const QString weight = attr(f, QStringLiteral("Weight"));
    if (!weight.isEmpty())
        s->bold = weight == QLatin1String("bold");
    const QString ul = attr(f, QStringLiteral("Underlined"));
    if (!ul.isEmpty())
        s->underline = ul == QLatin1String("yes") || ul == QLatin1String("true");
}

// ASS-Farbe &HAABBGGRR& (ASS-Alpha: 00 = deckend)
QString assColor(quint32 argb)
{
    const quint32 r = (argb >> 16) & 0xff, g = (argb >> 8) & 0xff, b = argb & 0xff;
    return QStringLiteral("&H%1%2%3&").arg(b, 2, 16, QLatin1Char('0')).arg(g, 2, 16, QLatin1Char('0')).arg(r, 2, 16, QLatin1Char('0')).toUpper();
}

QString assAlpha(quint32 argb)
{
    return QStringLiteral("&H%1&").arg(255 - ((argb >> 24) & 0xff), 2, 16, QLatin1Char('0')).toUpper();
}

// DCP-Schriftgröße (Punkt, 72 pt/Zoll auf 11 Zoll Bildhöhe) -> Pixel bei 1080 Zeilen
double assSize(double points)
{
    return points * kResY / (11.0 * 72.0);
}

QString styleTags(const Style &s)
{
    QString t = QStringLiteral("\\fs%1\\c%2\\1a%3").arg(qRound(assSize(s.size))).arg(assColor(s.color), assAlpha(s.color));
    if (!s.font.isEmpty())
        t += QStringLiteral("\\fn") + s.font;
    t += s.italic ? QStringLiteral("\\i1") : QStringLiteral("\\i0");
    t += s.bold ? QStringLiteral("\\b1") : QStringLiteral("\\b0");
    t += s.underline ? QStringLiteral("\\u1") : QStringLiteral("\\u0");
    if (s.effect == QLatin1String("border"))
        t += QStringLiteral("\\bord2\\shad0\\3c%1\\3a%2").arg(assColor(s.effectColor), assAlpha(s.effectColor));
    else if (s.effect == QLatin1String("shadow"))
        t += QStringLiteral("\\bord0\\shad2\\4c%1\\4a%2").arg(assColor(s.effectColor), assAlpha(s.effectColor));
    else
        t += QStringLiteral("\\bord0\\shad0");
    return t;
}

QString escapeText(QString s)
{
    s.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    s.replace(QLatin1Char('{'), QStringLiteral("\\{"));
    s.replace(QLatin1Char('}'), QStringLiteral("\\}"));
    s.replace(QLatin1Char('\n'), QStringLiteral("\\N"));
    return s;
}

// Textinhalt mit eingebetteten <Font>-Elementen -> ASS mit Override-Tags
QString textContent(const QDomNode &node, const Style &base, const QHash<QString, QString> &fonts)
{
    QString out;
    for (QDomNode n = node.firstChild(); !n.isNull(); n = n.nextSibling()) {
        if (n.isText() || n.isCDATASection()) {
            out += escapeText(n.nodeValue());
        } else if (n.isElement()) {
            const QDomElement e = n.toElement();
            const QString name = local(e);
            if (name == QLatin1String("Font")) {
                Style inner = base;
                applyFont(e, &inner, fonts);
                out += QStringLiteral("{%1}").arg(styleTags(inner)) + textContent(e, inner, fonts)
                       + QStringLiteral("{%1}").arg(styleTags(base));
            } else if (name == QLatin1String("Space")) {
                out += QStringLiteral("\\h");
            } else {
                out += textContent(e, base, fonts);
            }
        }
    }
    return out;
}

QString assTime(double s)
{
    s = std::max(0.0, s);
    const qint64 cs = qRound64(s * 100);
    return QStringLiteral("%1:%2:%3.%4").arg(cs / 360000).arg((cs / 6000) % 60, 2, 10, QLatin1Char('0'))
        .arg((cs / 100) % 60, 2, 10, QLatin1Char('0')).arg(cs % 100, 2, 10, QLatin1Char('0'));
}

struct Event
{
    double start, end;
    QString text;
};

struct Parser
{
    bool smpte = false;
    double rate = 24;       // SMPTE TimeCodeRate
    double startTime = 0;   // SMPTE StartTime
    QHash<QString, QString> fonts; // LoadFont-ID -> Familienname
    SubtitleSource src;
    QList<Event> *events = nullptr;
    int images = 0;

    double time(const QString &s) const
    {
        if (s.isEmpty())
            return 0;
        // Interop-Fade als reine Zahl: Ticks zu 4 ms
        if (!s.contains(QLatin1Char(':')))
            return s.toDouble() / (smpte ? rate : 250.0);
        QString t = s;
        double frac = 0;
        const int dot = t.lastIndexOf(QLatin1Char('.'));
        if (dot > t.lastIndexOf(QLatin1Char(':'))) {
            frac = (QStringLiteral("0") + t.mid(dot)).toDouble();
            t = t.left(dot);
        }
        const QStringList p = t.split(QLatin1Char(':'));
        double v = 0;
        if (p.size() >= 3)
            v = p[0].toDouble() * 3600 + p[1].toDouble() * 60 + p[2].toDouble();
        if (p.size() == 4)
            v += smpte ? p[3].toDouble() / rate : p[3].toDouble() / 250.0;
        return v + frac;
    }

    void walk(const QDomElement &parent, Style style)
    {
        for (QDomElement e = parent.firstChildElement(); !e.isNull(); e = e.nextSiblingElement()) {
            const QString name = local(e);
            if (name == QLatin1String("Font")) {
                Style inner = style;
                applyFont(e, &inner, fonts);
                walk(e, inner);
            } else if (name == QLatin1String("Subtitle")) {
                subtitle(e, style);
            } else if (name == QLatin1String("SubtitleList")) {
                walk(e, style);
            }
        }
    }

    void subtitle(const QDomElement &sub, const Style &style)
    {
        double in = time(attr(sub, QStringLiteral("TimeIn"))) - (smpte ? startTime : 0);
        double out = time(attr(sub, QStringLiteral("TimeOut"))) - (smpte ? startTime : 0);
        const double fadeIn = time(attr(sub, QStringLiteral("FadeUpTime")));
        const double fadeOut = time(attr(sub, QStringLiteral("FadeDownTime")));
        // Auf den verwendeten Ausschnitt der Rolle beziehen
        in -= src.entry;
        out -= src.entry;
        if (out <= 0 || (src.length > 0 && in >= src.length))
            return;
        in = std::max(0.0, in);
        if (src.length > 0)
            out = std::min(out, src.length);
        in += src.reelStart;
        out += src.reelStart;

        texts(sub, style, in, out, fadeIn, fadeOut);
    }

    // Text-Elemente eines Subtitle, auch innerhalb umschließender <Font>-Elemente
    void texts(const QDomElement &parent, const Style &style, double in, double out, double fadeIn, double fadeOut)
    {
        for (QDomElement e = parent.firstChildElement(); !e.isNull(); e = e.nextSiblingElement()) {
            const QString name = local(e);
            if (name == QLatin1String("Image")) {
                ++images;
            } else if (name == QLatin1String("Text")) {
                text(e, style, in, out, fadeIn, fadeOut);
            } else if (name == QLatin1String("Font")) {
                Style inner = style;
                applyFont(e, &inner, fonts);
                texts(e, inner, in, out, fadeIn, fadeOut);
            }
        }
    }

    void text(const QDomElement &t, const Style &style, double in, double out, double fadeIn, double fadeOut)
    {
        const QString valign = attr(t, QStringLiteral("VAlign")).toLower();
        const QString halign = attr(t, QStringLiteral("HAlign")).toLower();
        const double vpos = attr(t, QStringLiteral("VPosition")).toDouble() / 100.0;
        const double hpos = attr(t, QStringLiteral("HPosition")).toDouble() / 100.0;
        int an = 2;
        double y = kResY * (1.0 - vpos);
        if (valign == QLatin1String("top")) {
            an = 8;
            y = kResY * vpos;
        } else if (valign == QLatin1String("center")) {
            an = 5;
            y = kResY * (0.5 + vpos);
        }
        double x = kResX * (0.5 + hpos);
        if (halign == QLatin1String("left")) {
            an -= 1;
            x = kResX * hpos;
        } else if (halign == QLatin1String("right")) {
            an += 1;
            x = kResX * (1.0 - hpos);
        }
        const QString body = textContent(t, style, fonts).trimmed();
        if (body.isEmpty())
            return;
        QString tags = QStringLiteral("\\an%1\\pos(%2,%3)").arg(an).arg(qRound(x)).arg(qRound(y)) + styleTags(style);
        if (fadeIn > 0 || fadeOut > 0)
            tags += QStringLiteral("\\fad(%1,%2)").arg(qRound(fadeIn * 1000)).arg(qRound(fadeOut * 1000));
        events->append({in, out, QStringLiteral("{%1}").arg(tags) + body});
    }
};

quint32 rd32(const uchar *p) { return (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | p[3]; }
quint16 rd16(const uchar *p) { return quint16((p[0] << 8) | p[1]); }

} // namespace

QString fontFamily(const QByteArray &font)
{
    const auto *d = reinterpret_cast<const uchar *>(font.constData());
    const qsizetype n = font.size();
    if (n < 12)
        return {};
    const int tables = rd16(d + 4);
    for (int i = 0; i < tables && 12 + (i + 1) * 16 <= n; ++i) {
        const uchar *rec = d + 12 + i * 16;
        if (std::memcmp(rec, "name", 4) != 0)
            continue;
        const quint32 off = rd32(rec + 8);
        if (off + 6 > quint32(n))
            return {};
        const uchar *name = d + off;
        const int count = rd16(name + 2);
        const quint32 strings = off + rd16(name + 4);
        QString mac;
        for (int r = 0; r < count && off + 6 + (r + 1) * 12 <= quint32(n); ++r) {
            const uchar *nr = name + 6 + r * 12;
            const int platform = rd16(nr), nameId = rd16(nr + 6), len = rd16(nr + 8), so = rd16(nr + 10);
            if (nameId != 1 || strings + so + len > quint32(n))
                continue;
            const uchar *s = d + strings + so;
            if (platform == 3 || platform == 0) {
                QString out;
                for (int k = 0; k + 1 < len; k += 2)
                    out += QChar(rd16(s + k));
                return out;
            }
            if (platform == 1 && mac.isEmpty())
                mac = QString::fromLatin1(reinterpret_cast<const char *>(s), len);
        }
        return mac;
    }
    return {};
}

SubtitleResult buildSubtitles(const QList<SubtitleSource> &sources, const QString &outDir, const QString &name)
{
    SubtitleResult res;
    QDir().mkpath(outDir);
    res.fontsDir = QDir(outDir).filePath(QStringLiteral("fonts"));
    QDir().mkpath(res.fontsDir);
    QList<Event> events;
    int fontCounter = 0;

    for (const SubtitleSource &src : sources) {
        QByteArray xml;
        QList<QByteArray> resources;
        if (src.file.endsWith(QLatin1String(".mxf"), Qt::CaseInsensitive)) {
            const TimedText tt = readTimedText(src.file, src.key);
            xml = tt.xml;
            resources = tt.resources;
        } else {
            QFile f(src.file);
            if (f.open(QIODevice::ReadOnly))
                xml = f.readAll();
        }
        QDomDocument doc;
        if (xml.isEmpty() || !doc.setContent(xml)) {
            res.error = QStringLiteral("Untertitel nicht lesbar: %1").arg(QFileInfo(src.file).fileName());
            continue;
        }
        const QDomElement root = doc.documentElement();
        Parser p;
        p.smpte = local(root) == QLatin1String("SubtitleReel");
        p.src = src;
        p.events = &events;
        if (res.language.isEmpty())
            res.language = src.language;
        for (QDomElement e = root.firstChildElement(); !e.isNull(); e = e.nextSiblingElement()) {
            const QString n = local(e);
            if (n == QLatin1String("TimeCodeRate"))
                p.rate = std::max(1.0, e.text().toDouble());
            else if (n == QLatin1String("Language") && res.language.isEmpty())
                res.language = e.text().trimmed();
        }
        for (QDomElement e = root.firstChildElement(); !e.isNull(); e = e.nextSiblingElement())
            if (local(e) == QLatin1String("StartTime"))
                p.startTime = p.time(e.text().trimmed());

        // Schriften: Interop referenziert Dateien, SMPTE bettet sie ins MXF ein
        QStringList families;
        for (const QByteArray &r : std::as_const(resources)) {
            if (r.startsWith("\x89PNG"))
                continue;
            const QString fam = fontFamily(r);
            QFile out(QDir(res.fontsDir).filePath(QStringLiteral("font%1.ttf").arg(++fontCounter)));
            if (out.open(QIODevice::WriteOnly))
                out.write(r);
            if (!fam.isEmpty())
                families << fam;
        }
        int loadIndex = 0;
        for (QDomElement e = root.firstChildElement(); !e.isNull(); e = e.nextSiblingElement()) {
            if (local(e) != QLatin1String("LoadFont"))
                continue;
            const QString id = attr(e, QStringLiteral("Id")).isEmpty() ? attr(e, QStringLiteral("ID")) : attr(e, QStringLiteral("Id"));
            const QString uri = attr(e, QStringLiteral("URI"));
            QString family;
            if (!uri.isEmpty()) {
                QFile ff(QFileInfo(src.file).dir().filePath(uri));
                if (ff.open(QIODevice::ReadOnly)) {
                    const QByteArray data = ff.readAll();
                    family = fontFamily(data);
                    QFile out(QDir(res.fontsDir).filePath(QStringLiteral("font%1.ttf").arg(++fontCounter)));
                    if (out.open(QIODevice::WriteOnly))
                        out.write(data);
                }
            } else if (loadIndex < families.size()) {
                family = families.value(loadIndex);
            }
            ++loadIndex;
            if (!id.isEmpty() && !family.isEmpty())
                p.fonts.insert(id, family);
        }
        p.walk(root, Style{});
        res.images += p.images;
    }

    if (events.isEmpty()) {
        if (res.error.isEmpty())
            res.error = res.images ? QStringLiteral("Nur Bilduntertitel (PNG) – nicht unterstützt") : QStringLiteral("Keine Untertitel");
        return res;
    }
    std::stable_sort(events.begin(), events.end(), [](const Event &a, const Event &b) { return a.start < b.start; });

    QString ass;
    ass += QStringLiteral("[Script Info]\nScriptType: v4.00+\nPlayResX: %1\nPlayResY: %2\nScaledBorderAndShadow: yes\nWrapStyle: 2\n"
                          "YCbCr Matrix: None\n\n").arg(int(kResX)).arg(int(kResY));
    ass += QStringLiteral("[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, "
                          "Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, "
                          "Alignment, MarginL, MarginR, MarginV, Encoding\n"
                          "Style: Default,Arial,%1,&H00FFFFFF,&H00FFFFFF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,0,2,0,0,0,1\n\n")
               .arg(qRound(assSize(42)));
    ass += QStringLiteral("[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n");
    for (const Event &e : std::as_const(events))
        ass += QStringLiteral("Dialogue: 0,%1,%2,Default,,0,0,0,,%3\n").arg(assTime(e.start), assTime(e.end), e.text);

    res.assFile = QDir(outDir).filePath(name + QStringLiteral(".ass"));
    QFile f(res.assFile);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        res.error = QStringLiteral("ASS-Datei konnte nicht geschrieben werden");
        res.assFile.clear();
        return res;
    }
    f.write(ass.toUtf8());
    res.events = int(events.size());
    return res;
}

} // namespace Dcp
