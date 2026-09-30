// Video-CD-Wiedergabesteuerung (PBC) mit echter mpv-Wiedergabe prüfen:
//
//   vcd_test <abbild.cue>
//
// Test-VCD (tools/make_test_vcd_pbc.py, mit vcdxbuild erstellt):
//   LID 1  Auswahl: Menü-Standbild, 1 = Film, 2 = Extras, 3 = Film ab Einsprungpunkt 2 s,
//          Default = Film
//   LID 2  Film (4 s)   -> Next Extras, Return Menü
//   LID 3  Extras (3 s) -> Next Menü, Prev Film
//   LID 4  Film ab 2 s  -> Next Menü
#include "VcdNav.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTimer>

#include <mpv/client.h>

#include <clocale>
#include <cmath>
#include <cstdio>
#include <functional>

namespace {
int g_fail = 0;
void check(bool ok, const QString &what)
{
    std::printf("%s %s\n", ok ? "OK  " : "FAIL", qPrintable(what));
    std::fflush(stdout);
    if (!ok)
        ++g_fail;
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::setlocale(LC_NUMERIC, "C");
    if (argc < 2) {
        std::fprintf(stderr, "usage: vcd_test <image.cue>\n");
        return 2;
    }
    const QString image = QString::fromLocal8Bit(argv[1]);
    const Optical::VcdDisc disc = Optical::readVcdDisc(image);
    std::printf("     INFO %lld B, LOT %lld B, PSD %lld B, %lld Einsprungpunkte, %lld Tracks\n", qlonglong(disc.info.size()),
                qlonglong(disc.lot.size()), qlonglong(disc.psd.size()), qlonglong(disc.entries.size()), qlonglong(disc.tracks.size()));
    check(VcdNav::hasPbc(image), QStringLiteral("PBC erkannt (LOT/PSD)"));

    mpv_handle *mpv = mpv_create();
    for (auto [k, v] : std::initializer_list<std::pair<const char *, const char *>>{
             {"vo", "null"}, {"ao", "null"}, {"idle", "yes"}, {"keep-open", "yes"}, {"terminal", "no"}})
        mpv_set_option_string(mpv, k, v);
    mpv_request_log_messages(mpv, "error");
    if (mpv_initialize(mpv) < 0)
        return 1;
    Optical::attachProtocol(mpv);
    mpv_observe_property(mpv, 1, "eof-reached", MPV_FORMAT_FLAG);

    VcdNav nav;
    int loads = 0;
    bool loaded = false, lastStill = false;
    QObject::connect(&nav, &VcdNav::playRequested, [&](const QString &url, bool still) {
        const QByteArray u = url.toUtf8();
        const char *cmd[] = {"loadfile", u.constData(), nullptr};
        loaded = false;
        lastStill = still;
        ++loads;
        // keep-open pausiert am Dateiende – das nächste Element muss laufen
        mpv_set_property_string(mpv, "pause", "no");
        mpv_command(mpv, cmd);
    });

    auto pump = [&] {
        while (mpv_event *ev = mpv_wait_event(mpv, 0.01)) {
            if (ev->event_id == MPV_EVENT_NONE)
                break;
            if (ev->event_id == MPV_EVENT_FILE_LOADED)
                loaded = true;
            else if (ev->event_id == MPV_EVENT_LOG_MESSAGE)
                std::printf("     mpv[%s] %s", static_cast<mpv_event_log_message *>(ev->data)->prefix,
                            static_cast<mpv_event_log_message *>(ev->data)->text);
            else if (ev->event_id == MPV_EVENT_PROPERTY_CHANGE) {
                auto *p = static_cast<mpv_event_property *>(ev->data);
                if (qEnvironmentVariableIsSet("VCD_TEST_DEBUG"))
                    std::printf("     [eof-reached=%d loaded=%d lid=%d]\n", p->format == MPV_FORMAT_FLAG ? *static_cast<int *>(p->data) : -1, loaded, nav.lid());
                if (p->format == MPV_FORMAT_FLAG && *static_cast<int *>(p->data) && loaded)
                    QTimer::singleShot(0, &nav, [&nav] { nav.itemFinished(); });
            }
        }
        QCoreApplication::processEvents();
    };
    auto waitFor = [&](const std::function<bool()> &cond, int ms) {
        QElapsedTimer t;
        t.start();
        while (!cond() && t.elapsed() < ms)
            pump();
        return cond();
    };
    auto duration = [&] {
        double d = 0;
        waitFor([&] { return loaded && mpv_get_property(mpv, "duration", MPV_FORMAT_DOUBLE, &d) >= 0 && d > 0; }, 5000);
        return d;
    };
    auto near = [](double a, double b) { return std::abs(a - b) < 0.35; };
    auto atLid = [&](int lid, int ms) { return waitFor([&] { return nav.lid() == lid; }, ms); };

    check(nav.start(image), QStringLiteral("Steuerung gestartet"));
    check(nav.lid() == 1 && nav.selection(), QStringLiteral("LID 1 = Auswahlmenü"));
    check(waitFor([&] { return loaded; }, 5000), QStringLiteral("Menübild geladen"));
    int64_t w = 0, h = 0;
    waitFor([&] { return mpv_get_property(mpv, "width", MPV_FORMAT_INT64, &w) >= 0 && w > 0; }, 3000);
    mpv_get_property(mpv, "height", MPV_FORMAT_INT64, &h);
    check(w == 352 && h == 288, QStringLiteral("Menü-Segment 352×288 (%1×%2), Standbild=%3").arg(w).arg(h).arg(lastStill));
    waitFor([&] { return nav.state().value("waiting").toBool(); }, 10000); // Bewegtmenü: erst nach Ablauf des Segments
    check(nav.state().value("waiting").toBool(), QStringLiteral("Menü wartet auf Eingabe"));
    check(nav.state().value("choices").toList().size() == 3, QStringLiteral("3 Auswahlpunkte ab BSN 1"));
    check(!nav.key(QStringLiteral("9")) && nav.lid() == 1, QStringLiteral("ungültige Auswahl 9 ignoriert"));

    check(nav.key(QStringLiteral("2")) && nav.lid() == 3, QStringLiteral("Taste 2 -> Extras (LID 3)"));
    check(near(duration(), 3.0), QStringLiteral("Extras 3 s"));
    check(atLid(1, 10000), QStringLiteral("nach Extras automatisch zurück ins Menü (Next)"));

    check(nav.key(QStringLiteral("3")) && nav.lid() == 4, QStringLiteral("Taste 3 -> Film ab Einsprungpunkt (LID 4)"));
    // MPEG-PS: am Sektor des Einsprungpunkts liegt bereits Ton kurz vor 2 s (verschränkt)
    const double fromEntry = duration();
    double start = -1;
    mpv_get_property(mpv, "demuxer-start-time", MPV_FORMAT_DOUBLE, &start);
    const double entryStart = start;
    check(atLid(1, 10000), QStringLiteral("zurück ins Menü"));

    check(nav.key(QStringLiteral("enter")) && nav.lid() == 2, QStringLiteral("Enter = Default -> Film (LID 2)"));
    check(near(duration(), 4.0), QStringLiteral("Film 4 s"));
    // Einsprungpunkt relativ zum Filmanfang (MPEG-PS beginnt nicht bei 0)
    double filmStart = 0;
    mpv_get_property(mpv, "demuxer-start-time", MPV_FORMAT_DOUBLE, &filmStart);
    const double rel = entryStart - filmStart;
    check(rel > 1.4 && rel <= 2.05 && fromEntry < 2.7,
          QStringLiteral("Einsprungpunkt: beginnt %1 s nach Filmanfang, noch %2 s").arg(rel, 0, 'f', 2).arg(fromEntry, 0, 'f', 2));
    check(nav.key(QStringLiteral("next")) && nav.lid() == 3, QStringLiteral("Next -> Extras"));
    check(nav.key(QStringLiteral("prev")) && nav.lid() == 2, QStringLiteral("Prev -> Film"));
    check(nav.key(QStringLiteral("return")) && nav.lid() == 1, QStringLiteral("Return -> Menü"));

    check(nav.key(QStringLiteral("1")) && nav.lid() == 2, QStringLiteral("Taste 1 -> Film"));
    check(atLid(3, 10000), QStringLiteral("Filmende -> Extras (Next)"));
    check(atLid(1, 10000), QStringLiteral("Extras-Ende -> Menü"));
    std::printf("     Ladevorgänge: %d\n", loads);

    nav.stop();
    mpv_terminate_destroy(mpv);
    std::printf("%s (%d Fehler)\n", g_fail ? "FEHLGESCHLAGEN" : "BESTANDEN", g_fail);
    return g_fail ? 1 : 0;
}
