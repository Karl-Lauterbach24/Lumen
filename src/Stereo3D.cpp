#include "Stereo3D.h"

#include <QColor>
#include <QStringList>

namespace Stereo3D {

QString cleanPattern(const QString &pattern)
{
    QString out;
    for (const QChar c : pattern.toUpper()) {
        if (QStringLiteral("LRSB").contains(c) && out.size() < 12)
            out += c;
    }
    return out.contains(QLatin1Char('L')) && out.contains(QLatin1Char('R')) ? out : QStringLiteral("LR");
}

// Ausdruck, der für die Bilder mit einem der Zeichen wahr ist: eq(mod(n+p,N),i)+…
static QString when(const QString &pattern, const QString &chars, int phase)
{
    QStringList terms;
    const int n = pattern.size();
    for (int i = 0; i < n; ++i) {
        if (chars.contains(pattern.at(i)))
            terms << QStringLiteral("eq(mod(n+%1\\,%2)\\,%3)").arg(phase).arg(n).arg(i);
    }
    return terms.isEmpty() ? QStringLiteral("0") : terms.join(QLatin1Char('+'));
}

QString sequential(const QString &in, const QVariantMap &profile)
{
    if (in == QLatin1String("none"))
        return QString();
    const QString pattern = cleanPattern(profile.value("seqPattern", "LR").toString());
    const int rate = qBound(48, profile.value("seqRate", 120).toInt(), 480);
    const int phase = ((profile.value("seqPhase").toInt() % pattern.size()) + pattern.size()) % pattern.size();
    const bool swap = profile.value("seqSwap").toBool();

    QStringList chain;
    // zuerst auf Side-by-Side in voller Breite bringen, linkes Auge links
    if (in != QLatin1String("sbsl"))
        chain << QStringLiteral("stereo3d=%1:sbsl").arg(in);
    // ein Bild je Bildwechsel
    chain << QStringLiteral("fps=%1").arg(rate);
    // Auge wählen: rechte Hälfte bei R (getauscht: bei L)
    chain << QStringLiteral("crop=w=iw/2:h=ih:x='(%1)*iw/2':y=0").arg(when(pattern, swap ? QStringLiteral("L") : QStringLiteral("R"), phase));

    if (pattern.contains(QLatin1Char('S'))) {
        QColor color(profile.value("seqSyncColor", "#ff0000").toString());
        if (!color.isValid())
            color = QColor(255, 0, 0);
        const double level = qBound(0.0, profile.value("seqSyncLevel", 100).toDouble(), 100.0) / 100.0;
        const QString hex = QStringLiteral("0x%1%2%3")
                                .arg(int(color.red() * level), 2, 16, QLatin1Char('0'))
                                .arg(int(color.green() * level), 2, 16, QLatin1Char('0'))
                                .arg(int(color.blue() * level), 2, 16, QLatin1Char('0'));
        chain << QStringLiteral("drawbox=x=0:y=0:w=iw:h=ih:c=%1:t=fill:enable='%2'").arg(hex, when(pattern, QStringLiteral("S"), phase));
    }
    if (pattern.contains(QLatin1Char('B')))
        chain << QStringLiteral("drawbox=x=0:y=0:w=iw:h=ih:c=black:t=fill:enable='%1'").arg(when(pattern, QStringLiteral("B"), phase));

    const QString box = profile.value("seqBox", "none").toString();
    if (box.size() == 2 && QStringLiteral("tb").contains(box.at(0)) && QStringLiteral("lr").contains(box.at(1))) {
        const double size = qBound(1.0, profile.value("seqBoxSize", 6).toDouble(), 30.0) / 100.0;
        const QString side = QStringLiteral("ih*%1").arg(size, 0, 'f', 3);
        const QString x = box.at(1) == QLatin1Char('l') ? QStringLiteral("0") : QStringLiteral("iw-%1").arg(side);
        const QString y = box.at(0) == QLatin1Char('t') ? QStringLiteral("0") : QStringLiteral("ih-%1").arg(side);
        const QString geometry = QStringLiteral("drawbox=x=%1:y=%2:w=%3:h=%3").arg(x, y, side);
        chain << geometry + QStringLiteral(":c=white:t=fill:enable='%1'").arg(when(pattern, QStringLiteral("L"), phase));
        chain << geometry + QStringLiteral(":c=black:t=fill:enable='%1'").arg(when(pattern, QStringLiteral("RSB"), phase));
    }
    return QStringLiteral("lavfi=[%1]").arg(chain.join(QLatin1Char(',')));
}

QString filter(const QString &in, const QVariantMap &profile)
{
    if (in == QLatin1String("none"))
        return QString();
    const QString out = profile.value("stereoOut", "none").toString();
    const QString target = out == QLatin1String("none") ? QStringLiteral("ml") : out;
    if (target == QLatin1String("seq"))
        return sequential(in, profile);
    // Frame Packing (HDMI 1.4, 1080p24): linkes Auge oben, 45 Zeilen Lücke, rechtes Auge unten
    // = 1920x2205. Erfordert am Ausgang einen 1920x2205-Anzeigemodus.
    if (target == QLatin1String("fp")) {
        // Zuerst auf Side-by-Side Full, linkes Auge links, normalisieren
        const QString norm = (in == QLatin1String("sbsl")) ? QString() : QStringLiteral("stereo3d=%1:sbsl,").arg(in);
        return QStringLiteral("lavfi=[%1split[a][b];[a]crop=iw/2:ih:0:0[l];[b]crop=iw/2:ih:iw/2:0,"
                              "pad=iw:ih+45:0:45[r];[l][r]vstack]").arg(norm);
    }
    if (target == in)
        return QString();
    return QStringLiteral("lavfi=[stereo3d=%1:%2]").arg(in, target);
}

} // namespace Stereo3D
