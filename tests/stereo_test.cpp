// Gibt die Filterkette der 3D-Bildfolge aus (tools/test_seq3d.sh wendet sie mit FFmpeg an):
//
//   stereo_test <quellformat> <rate> <muster> [swap] [phase=<n>] [box=<tl|tr|bl|br>] [size=<prozent>] [color=<#rrggbb>] [level=<prozent>]
//
// Ausgabe: die lavfi-Kette ohne die mpv-Klammer "lavfi=[…]".
#include "Stereo3D.h"

#include <QCoreApplication>

#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments().mid(1);
    if (args.size() < 3) {
        std::fprintf(stderr, "stereo_test <in> <rate> <pattern> [swap] [phase=n] [box=tl] [color=#rrggbb] [level=n]\n");
        return 2;
    }
    QVariantMap profile{{"stereoOut", "seq"}, {"seqRate", args[1].toInt()}, {"seqPattern", args[2]}};
    for (const QString &a : args.mid(3)) {
        if (a == QLatin1String("swap"))
            profile["seqSwap"] = true;
        else if (a.startsWith(QLatin1String("phase=")))
            profile["seqPhase"] = a.mid(6).toInt();
        else if (a.startsWith(QLatin1String("box=")))
            profile["seqBox"] = a.mid(4);
        else if (a.startsWith(QLatin1String("size=")))
            profile["seqBoxSize"] = a.mid(5).toInt();
        else if (a.startsWith(QLatin1String("color=")))
            profile["seqSyncColor"] = a.mid(6);
        else if (a.startsWith(QLatin1String("level=")))
            profile["seqSyncLevel"] = a.mid(6).toInt();
    }
    QString chain = Stereo3D::filter(args[0], profile);
    if (!chain.startsWith(QLatin1String("lavfi=[")) || !chain.endsWith(QLatin1Char(']')))
        return 1;
    chain = chain.mid(7, chain.size() - 8);
    std::printf("%s\n", qPrintable(chain));
    return 0;
}
