// Automatische 3D-Erkennung.
//
//   stereodetect_test                     Kennungen im Dateinamen, Container-Angaben, Bildgrößen
//   stereodetect_test <datei> <erwartet>  Datei auswerten; erwartet = stereo3d-Kürzel oder "none"
//                                         (tools/test_stereodetect.sh erzeugt die Testdateien)
#include "StereoDetect.h"

#include <QCoreApplication>

#include <cstdio>

extern "C" {
#include <libavutil/log.h>
}

static int failures = 0;

static void check(bool ok, const QString &what)
{
    std::printf("%s %s\n", ok ? "OK  " : "FAIL", qPrintable(what));
    if (!ok)
        ++failures;
}

static void name(const char *file, const char *expected, int w = 1920, int h = 1080)
{
    const StereoDetect::Hint hint = StereoDetect::fromName(QString::fromUtf8(file));
    const QString got = hint.layout == StereoDetect::Mvc ? QStringLiteral("mvc")
                        : hint.decided()                 ? StereoDetect::format(hint, w, h)
                        : hint.is3d                      ? QStringLiteral("3d")
                                                         : QStringLiteral("none");
    check(got == QLatin1String(expected), QStringLiteral("Name %1 -> %2 (erwartet %3)").arg(QString::fromUtf8(file), got, QString::fromUtf8(expected)));
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    av_log_set_level(AV_LOG_QUIET);
    const QStringList args = app.arguments().mid(1);
    if (args.size() >= 2) {
        int w = 0, h = 0;
        double confidence = 0;
        QList<StereoDetect::Scores> scores;
        StereoDetect::analyzeFile(args[0], false, nullptr, &confidence, &w, &h, &scores);
        for (const auto &s : scores)
            std::printf("     nebeneinander %.2f  übereinander %.2f\n", s.sideBySide, s.topBottom);
        const StereoDetect::Result r = StereoDetect::detectFile(args[0]);
        // "mvc": zwei Ansichten in einem Strom (das Format selbst bleibt dann "none")
        const QString got = args[1] == QLatin1String("mvc") && r.mvc ? QStringLiteral("mvc") : r.format;
        check(got == args[1], QStringLiteral("%1: %2 (%3, %4) – erwartet %5")
                                       .arg(args[0], got, r.source.isEmpty() ? QStringLiteral("-") : r.source)
                                       .arg(r.confidence, 0, 'f', 2)
                                       .arg(args[1]));
        return failures ? 1 : 0;
    }

    // Kennungen, wie sie in Dateinamen vorkommen
    name("Avatar.2009.3D.HSBS.1080p.BluRay.x264.mkv", "sbs2l");
    name("Avatar (2009) 3D H-SBS.mkv", "sbs2l");
    name("Movie.3D.Half-SBS.mkv", "sbs2l");
    name("Movie.3D.SBS.mkv", "sbs2l");                 // 1920x1080: gestaucht
    name("Movie.3D.SBS.mkv", "sbsl", 3840, 1080);      // doppelt breit: volle Auflösung
    name("Movie.3D.Full-SBS.mkv", "sbsl");
    name("Movie.FSBS.mkv", "sbsl");
    name("Movie 3D side-by-side.mp4", "sbs2l");
    name("Movie.3D.HOU.mkv", "ab2l");
    name("Movie.3D.H-OU.1080p.mkv", "ab2l");
    name("Movie.3D.HTAB.mkv", "ab2l");
    name("Movie.3D.TAB.mkv", "ab2l");
    name("Movie.3D.OU.mkv", "abl", 1920, 2160);
    name("Movie.3D.Over-Under.mkv", "ab2l");
    name("Movie.3D.Top-and-Bottom.mkv", "ab2l");
    name("Movie.3DSBS.mkv", "sbs2l");
    name("Movie.3D.SBS.RL.mkv", "sbs2r");
    name("Delicious Food Fight.3D.SBS.m2ts", "sbs2l");
    name("Movie.3D.MVC.mkv", "mvc");
    name("00001.ssif", "mvc");
    name("Movie.mk3d", "3d");
    name("Movie.3D.1080p.mkv", "3d");
    // … und was keine ist
    name("Movie.1080p.mkv", "none");
    name("Tab.Hunter.Confidential.2015.mkv", "none");
    name("Le.jour.ou.la.terre.s.arreta.mkv", "none");
    name("Hou.De.Kharcha.mkv", "none");
    name("3D.Printing.Explained.mp4", "3d");           // nur der Hinweis – das Bild entscheidet
    name("Half.Baked.1998.mkv", "none");

    // Container-Angaben (mpv: video-params/stereo-in)
    auto meta = [](const char *in, int w, int h, const char *expected) {
        const StereoDetect::Hint hint = StereoDetect::fromMetadata(QString::fromUtf8(in));
        const QString got = hint.decided() ? StereoDetect::format(hint, w, h) : QStringLiteral("none");
        check(got == QLatin1String(expected), QStringLiteral("Angabe %1 bei %2x%3 -> %4").arg(QString::fromUtf8(in)).arg(w).arg(h).arg(got));
    };
    meta("mono", 1920, 1080, "none");
    meta("sbs2l", 1920, 1080, "sbs2l");
    meta("sbs2l", 3840, 1080, "sbsl");
    meta("sbs2r", 1920, 1080, "sbs2r");
    meta("ab2l", 1920, 1080, "ab2l");
    meta("ab2l", 1920, 2160, "abl");
    meta("ab2r", 1920, 1080, "ab2r");
    meta("irl", 1920, 1080, "irl");
    meta("al", 1920, 1080, "al");
    meta("checkl", 1920, 1080, "none");

    // Halb oder voll nach der Bildgröße
    auto shape = [](StereoDetect::Layout layout, int w, int h, const char *expected) {
        StereoDetect::Hint hint;
        hint.layout = layout;
        const QString got = StereoDetect::format(hint, w, h);
        check(got == QLatin1String(expected), QStringLiteral("Größe %1x%2 -> %3").arg(w).arg(h).arg(got));
    };
    shape(StereoDetect::SideBySide, 1920, 1080, "sbs2l");
    shape(StereoDetect::SideBySide, 1920, 800, "sbs2l");
    shape(StereoDetect::SideBySide, 3840, 2160, "sbs2l");
    shape(StereoDetect::SideBySide, 3840, 1080, "sbsl");
    shape(StereoDetect::SideBySide, 3840, 800, "sbsl");
    shape(StereoDetect::SideBySide, 2560, 720, "sbsl");
    shape(StereoDetect::TopBottom, 1920, 1080, "ab2l");
    shape(StereoDetect::TopBottom, 1920, 800, "ab2l");
    shape(StereoDetect::TopBottom, 1920, 2160, "abl");
    shape(StereoDetect::TopBottom, 1920, 1600, "abl");

    check(StereoDetect::fromSize(3840, 1080).layout == StereoDetect::SideBySide, QStringLiteral("3840x1080: nebeneinander"));
    check(StereoDetect::fromSize(1920, 2160).layout == StereoDetect::TopBottom, QStringLiteral("1920x2160: übereinander"));
    check(!StereoDetect::fromSize(1920, 1080).decided(), QStringLiteral("1920x1080: keine Aussage"));
    check(!StereoDetect::fromSize(1080, 1920).decided(), QStringLiteral("1080x1920 (Hochkant): keine Aussage"));
    check(!StereoDetect::fromSize(3840, 2160).decided(), QStringLiteral("3840x2160: keine Aussage"));

    std::printf("%s (%d Fehler)\n", failures ? "NICHT BESTANDEN" : "BESTANDEN", failures);
    return failures ? 1 : 0;
}
