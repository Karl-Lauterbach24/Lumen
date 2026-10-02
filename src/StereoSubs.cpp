#include "StereoSubs.h"

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace StereoSubs {

bool splitsEyes(const QString &out)
{
    return out.startsWith(QLatin1String("sbs")) || out.startsWith(QLatin1String("ab")) || out == QLatin1String("fp");
}

bool isBitmapCodec(const QString &codec)
{
    static const QStringList bitmap = {QStringLiteral("hdmv_pgs_subtitle"), QStringLiteral("dvd_subtitle"), QStringLiteral("dvb_subtitle"),
                                       QStringLiteral("xsub"), QStringLiteral("dvb_teletext")};
    return bitmap.contains(codec);
}

QList<Eye> eyes(const QString &out, const QVariantMap &osd)
{
    const double ow = osd.value("w").toDouble(), oh = osd.value("h").toDouble();
    const double ml = osd.value("ml").toDouble(), mt = osd.value("mt").toDouble();
    const double vw = ow - ml - osd.value("mr").toDouble();
    const double vh = oh - mt - osd.value("mb").toDouble();
    if (vw <= 0 || vh <= 0 || !splitsEyes(out))
        return {};
    // "…r": das rechte Auge steht im ersten Bereich
    const bool leftFirst = !out.endsWith(QLatin1Char('r'));
    QList<Eye> e;
    if (out.startsWith(QLatin1String("sbs"))) {
        // halbe Breite ("sbs2"): das Gerät zieht jede Hälfte auf die volle Breite
        const QSizeF logical(out.startsWith(QLatin1String("sbs2")) ? vw : vw / 2, vh);
        e << Eye{{ml, mt, vw / 2, vh}, logical, leftFirst} << Eye{{ml + vw / 2, mt, vw / 2, vh}, logical, !leftFirst};
    } else if (out.startsWith(QLatin1String("ab"))) {
        const QSizeF logical(vw, out.startsWith(QLatin1String("ab2")) ? vh : vh / 2);
        e << Eye{{ml, mt, vw, vh / 2}, logical, leftFirst} << Eye{{ml, mt + vh / 2, vw, vh / 2}, logical, !leftFirst};
    } else { // Frame Packing 1920x2205: 1080 Zeilen, 45 Zeilen Lücke, 1080 Zeilen; linkes Auge oben
        const double eh = vh * 1080 / 2205;
        e << Eye{{ml, mt, vw, eh}, {vw, eh}, true} << Eye{{ml, mt + vh * 1125 / 2205, vw, eh}, {vw, eh}, false};
    }
    return e;
}

QStringList wrap(const QString &text, double maxWidth, const std::function<double(const QString &)> &measure)
{
    QStringList lines;
    for (const QString &paragraph : text.split(QLatin1Char('\n'))) {
        const QString p = paragraph.simplified();
        if (p.isEmpty())
            continue;
        QString line;
        for (const QString &word : p.split(QLatin1Char(' '))) {
            const QString candidate = line.isEmpty() ? word : line + QLatin1Char(' ') + word;
            if (!line.isEmpty() && measure(candidate) > maxWidth) {
                lines << line;
                line = word;
            } else {
                line = candidate;
            }
        }
        lines << line;
    }
    return lines;
}

QImage render(const QString &text, const QSizeF &logical)
{
    const int width = int(std::clamp(std::round(logical.width()), 16.0, 4096.0));
    if (logical.height() < 16)
        return {};
    // wie mpvs Vorgabe: gut 5 % der Bildhöhe
    QFont font;
    font.setPixelSize(std::max(12, int(std::round(logical.height() * 0.052))));
    font.setWeight(QFont::DemiBold);
    const QFontMetricsF fm(font);
    const QStringList lines = wrap(text, width * 0.9, [&fm](const QString &s) { return fm.horizontalAdvance(s); });
    if (lines.isEmpty())
        return {};
    const double outline = std::max(1.5, font.pixelSize() * 0.07);
    const double lineHeight = fm.lineSpacing();
    const int height = int(std::ceil(lines.size() * lineHeight + 2 * outline + fm.descent()));

    QImage img(width, height, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    for (int i = 0; i < lines.size(); ++i) {
        const double w = fm.horizontalAdvance(lines.at(i));
        path.addText(QPointF((width - w) / 2, outline + i * lineHeight + fm.ascent()), font, lines.at(i));
    }
    p.setPen(QPen(QColor(0, 0, 0, 235), 2 * outline, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::white);
    p.drawPath(path);
    return img;
}

} // namespace StereoSubs
