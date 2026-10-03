// Bild-Untertitel selbst lesen (BitmapSubs): Zeiten, Lücken, Sprung an eine spätere Stelle.
//
//   bitmapsubs_test <datei mit Bild-Untertiteln: 1,0–3,0 s, 4,0–6,0 s, 7,5–9,0 s>
//
// tools/test_seq3d.sh erzeugt die Datei mit FFmpeg (SRT -> VobSub).
#include "BitmapSubs.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>

#include <cstdio>

static int failures = 0;

static void check(bool ok, const QString &what)
{
    std::printf("%s %s\n", ok ? "OK  " : "FAIL", qPrintable(what));
    if (!ok)
        ++failures;
}

// warten, bis zu t ein Bild vorliegt (der Leser arbeitet in seinem eigenen Thread)
static bool waitFor(BitmapSubs &b, double t)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 5000) {
        if (!b.at(t, false).image.isNull())
            return true;
        QThread::msleep(20);
    }
    return false;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 2) {
        std::fprintf(stderr, "bitmapsubs_test <datei>\n");
        return 2;
    }
    const QString file = QString::fromLocal8Bit(argv[1]);
    {
        BitmapSubs b;
        b.start(file, -1, QString(), 0);
        b.setPosition(0);
        check(waitFor(b, 8.0), "drittes Bild gelesen");
        const BitmapSubs::Event e = b.at(1.5, false);
        check(!e.image.isNull() && e.start > 0.95 && e.start < 1.05 && e.end > 2.95 && e.end < 3.05,
              QStringLiteral("erstes Bild 1,0–3,0 s (gelesen %1–%2)").arg(e.start, 0, 'f', 3).arg(e.end, 0, 'f', 3));
        check(!e.canvas.isEmpty() && e.rect.width() > 0 && QRect(QPoint(0, 0), e.canvas).contains(e.rect), "Bild liegt auf seiner Fläche");
        check(b.at(0.5, false).image.isNull() && b.at(3.5, false).image.isNull() && b.at(6.5, false).image.isNull()
                  && b.at(9.5, false).image.isNull(),
              "vor, zwischen und nach den Untertiteln: kein Bild");
        check(!b.at(5.0, false).image.isNull() && !b.at(8.9, false).image.isNull(), "zweites und drittes Bild zu ihrer Zeit");
        check(b.at(1.5, true).image.isNull(), "nur erzwungene: gewöhnliche Bilder bleiben aus");
        b.stop();
    }
    {
        // Start mitten in der Datei: der Leser setzt dort an
        BitmapSubs b;
        b.start(file, -1, QString(), 0);
        b.setPosition(8.0);
        check(waitFor(b, 8.0), "nach einem Sprung: Bild an der neuen Stelle");
        check(b.at(9.5, false).image.isNull(), "… und es endet zu seiner Zeit");
    }
    std::printf("%s (%d Fehler)\n", failures ? "NICHT BESTANDEN" : "BESTANDEN", failures);
    return failures ? 1 : 0;
}
