// Gibt die Filterkette der 3D-Bildfolge aus (tools/test_seq3d.sh wendet sie mit FFmpeg an):
//
//   stereo_test <quellformat> <rate> <muster> [swap] [phase=<n>] [box=<tl|tr|bl|br>] [size=<prozent>] [color=<#rrggbb>] [level=<prozent>]
//
// Ausgabe: die lavfi-Kette ohne die mpv-Klammer "lavfi=[…]".
#include "Stereo3D.h"
#include "StereoSubs.h"

#include <QCoreApplication>

#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments().mid(1);
    // stereo_test --subs: Untertitel je Auge – Bereiche der Augen und Zeilenumbruch
    if (args.value(0) == QLatin1String("--subs")) {
        int failures = 0;
        auto check = [&failures](bool ok, const char *what) {
            std::printf("%s %s\n", ok ? "OK  " : "FAIL", what);
            failures += !ok;
        };
        using namespace StereoSubs;
        // Fenster 1920x1200, Bild 1920x1080 mit 60 Zeilen Rand oben und unten
        const QVariantMap osd{{"w", 1920}, {"h", 1200}, {"ml", 0}, {"mr", 0}, {"mt", 60}, {"mb", 60}};
        check(splitsEyes("sbs2l") && splitsEyes("abr") && splitsEyes("fp") && !splitsEyes("arcd") && !splitsEyes("seq") && !splitsEyes("none"),
              "eigene Bereiche je Auge: sbs, ab, fp – nicht Anaglyph, Bildfolge, 2D");
        QList<Eye> e = eyes("sbs2l", osd);
        check(e.size() == 2 && e[0].rect == QRectF(0, 60, 960, 1080) && e[1].rect == QRectF(960, 60, 960, 1080)
              && e[0].logical == QSizeF(1920, 1080) && e[0].left && !e[1].left, "Half-SBS: zwei Hälften, je auf volle Breite gezogen");
        e = eyes("sbsr", osd);
        check(e.size() == 2 && !e[0].left && e[1].left && e[0].logical == QSizeF(960, 1080), "Full-SBS, rechtes Auge zuerst");
        e = eyes("ab2l", osd);
        check(e.size() == 2 && e[0].rect == QRectF(0, 60, 1920, 540) && e[1].rect == QRectF(0, 600, 1920, 540)
              && e[0].logical == QSizeF(1920, 1080), "Half-Top-Bottom: zwei Hälften, je auf volle Höhe gezogen");
        e = eyes("fp", QVariantMap{{"w", 1920}, {"h", 2205}});
        check(e.size() == 2 && e[0].rect == QRectF(0, 0, 1920, 1080) && e[1].rect == QRectF(0, 1125, 1920, 1080), "Frame Packing: 45 Zeilen Lücke");
        check(eyes("arcd", osd).isEmpty() && eyes("sbs2l", {}).isEmpty(), "kein Bereich ohne geteiltes Format oder ohne Fenster");
        check(isBitmapCodec("hdmv_pgs_subtitle") && isBitmapCodec("dvd_subtitle") && !isBitmapCodec("subrip") && !isBitmapCodec("ass"),
              "Bild-Untertitel (PGS, VobSub) bleiben bei mpv, Text nicht");
        auto width = [](const QString &s) { return double(s.size()); };
        check(wrap("eins zwei drei vier", 9, width) == QStringList({"eins zwei", "drei vier"}), "Umbruch an Wortgrenzen");
        check(wrap("oben\n\n  unten  ", 20, width) == QStringList({"oben", "unten"}), "Zeilen bleiben Zeilen, leere fallen weg");
        check(wrap("Donaudampfschifffahrt ab", 5, width) == QStringList({"Donaudampfschifffahrt", "ab"}), "ein zu langes Wort bleibt ganz");
        check(wrap("", 10, width).isEmpty(), "kein Text: keine Zeile");
        std::printf("%s (%d Fehler)\n", failures ? "NICHT BESTANDEN" : "BESTANDEN", failures);
        return failures ? 1 : 0;
    }
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
