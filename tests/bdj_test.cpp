// BD-J-Menü der Testdisc (tools/make_test_bdj.py) über libbluray starten und bedienen:
//
//     bdj_test <disc> [Ordner mit libbluray-j2se-<Version>.jar] [menu.png]
//
// Prüft die ganze Kette, die Lumen für Java-Menüs braucht: Java-Archiv und Java-Laufzeit werden
// gefunden (BdjSetup), die Java-VM startet im Prozess, das Menü kommt als ARGB-Ebene an, Tasten
// erreichen es, dahinter läuft der Film. Ergebnis 0 = alles in Ordnung, 77 = hier nicht prüfbar
// (keine Java-Laufzeit oder kein Archiv für diese libbluray), 1 = Fehler.
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QThread>

#include <libbluray/bluray.h>
#include <libbluray/keys.h>
#include <libbluray/overlay.h>

#include <cstdio>
#include <cstring>
#include <mutex>

#include "../src/BdjSetup.h"

namespace {

struct Plane {
    std::mutex mutex;
    QImage image;
    int flushes = 0;
};

void overlayProc(void *handle, const BD_ARGB_OVERLAY *const ov)
{
    auto *p = static_cast<Plane *>(handle);
    if (!ov)
        return;
    std::lock_guard<std::mutex> lock(p->mutex);
    switch (ov->cmd) {
    case BD_ARGB_OVERLAY_INIT:
        p->image = QImage(ov->w, ov->h, QImage::Format_ARGB32);
        p->image.fill(0);
        break;
    case BD_ARGB_OVERLAY_CLOSE:
        p->image = QImage();
        break;
    case BD_ARGB_OVERLAY_DRAW:
        if (!p->image.isNull() && ov->argb) {
            const QRect r = QRect(ov->x, ov->y, ov->w, ov->h).intersected(p->image.rect());
            for (int y = r.top(); y <= r.bottom(); ++y)
                std::memcpy(p->image.scanLine(y) + r.left() * 4, ov->argb + size_t(y - ov->y) * ov->stride + (r.left() - ov->x),
                            size_t(r.width()) * 4);
        }
        break;
    case BD_ARGB_OVERLAY_FLUSH:
        ++p->flushes;
        break;
    default:
        break;
    }
}

// Zustand des Menüs aus den Farben der Testdisc (tests/data/bdj/MenuXlet.java)
enum class Menu { None, First, Second, Other };

Menu menuState(Plane &p)
{
    std::lock_guard<std::mutex> lock(p.mutex);
    if (p.image.width() < 1920 || p.image.height() < 1080)
        return Menu::None;
    const QRgb panel = p.image.pixel(200, 280), b0 = p.image.pixel(520, 410), b1 = p.image.pixel(520, 630);
    const QRgb selected = qRgb(255, 208, 0), button = qRgb(96, 104, 120);
    if (qAlpha(panel) == 0 && qAlpha(b0) == 0 && qAlpha(b1) == 0)
        return Menu::None;
    if ((b0 | 0xff000000) == selected && (b1 | 0xff000000) == button)
        return Menu::First;
    if ((b0 | 0xff000000) == button && (b1 | 0xff000000) == selected)
        return Menu::Second;
    return Menu::Other;
}

const char *name(Menu m)
{
    switch (m) {
    case Menu::None: return "no menu";
    case Menu::First: return "first button selected";
    case Menu::Second: return "second button selected";
    default: return "something else";
    }
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 2) {
        std::fprintf(stderr, "usage: bdj_test <disc> [folder with libbluray-j2se-<version>.jar] [menu.png]\n");
        return 1;
    }
    BdjSetup::prepare(argc > 2 ? QStringList{QString::fromLocal8Bit(argv[2])} : QStringList{});
    std::printf("LIBBLURAY_CP=%s\nJAVA_HOME=%s\n", qgetenv("LIBBLURAY_CP").constData(), qgetenv("JAVA_HOME").constData());

    BLURAY *bd = bd_open(argv[1], nullptr);
    if (!bd) {
        std::fprintf(stderr, "cannot open %s\n", argv[1]);
        return 1;
    }
    const BLURAY_DISC_INFO *info = bd_get_disc_info(bd);
    std::printf("BD-J disc: %d, Java VM found: %d, BD-J usable: %d, first play: %d, top menu: %d\n", info->bdj_detected,
                info->libjvm_detected, info->bdj_handled, info->first_play_supported, info->top_menu_supported);
    if (!info->bdj_detected) {
        std::fprintf(stderr, "not a BD-J disc\n");
        bd_close(bd);
        return 1;
    }
    if (!info->bdj_handled) {
        std::printf("BD-J cannot run here (%s) - not checked\n",
                    info->libjvm_detected ? "no Java archive for this libbluray" : "no Java runtime");
        bd_close(bd);
        return 77;
    }

    Plane plane;
    bd_register_argb_overlay_proc(bd, &plane, overlayProc, nullptr);
    if (!bd_play(bd)) {
        std::fprintf(stderr, "bd_play failed\n");
        bd_close(bd);
        return 1;
    }

    // Lesen wie der Player: Ereignisse abholen, Daten verwerfen. Jeder Schritt hat 30 s Zeit.
    static unsigned char buf[6144 * 16];
    qint64 bytes = 0;
    int playlist = -1;
    const auto pump = [&](Menu want) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 30000) {
            BD_EVENT ev;
            const int n = bd_read_ext(bd, buf, sizeof buf, &ev);
            if (ev.event == BD_EVENT_PLAYLIST)
                playlist = int(ev.param);
            if (n > 0)
                bytes += n;
            else if (ev.event == BD_EVENT_NONE)
                QThread::msleep(10);
            if (n < 0)
                break;
            if (menuState(plane) == want)
                return true;
        }
        std::fprintf(stderr, "waited for \"%s\", menu shows \"%s\" (%d overlay updates)\n", name(want),
                     name(menuState(plane)), plane.flushes);
        return false;
    };

    bool ok = pump(Menu::First);
    if (ok) {
        std::printf("menu drawn: first button selected\n");
        if (argc > 3) {
            std::lock_guard<std::mutex> lock(plane.mutex);
            plane.image.save(QString::fromLocal8Bit(argv[3]));
        }
        bd_user_input(bd, -1, BD_VK_DOWN);
        ok = pump(Menu::Second);
    }
    if (ok) {
        std::printf("key down: second button selected\n");
        bd_user_input(bd, -1, BD_VK_ENTER);
        ok = pump(Menu::None);
    }
    if (ok)
        std::printf("key enter: menu closed\n");
    // der Film hinter dem Menü: die Playlist der Disc muss laufen und Daten liefern
    if (ok && (playlist != 0 || bytes < 6144)) {
        for (int i = 0; i < 200 && bytes < 6144; ++i) {
            BD_EVENT ev;
            const int n = bd_read_ext(bd, buf, sizeof buf, &ev);
            if (ev.event == BD_EVENT_PLAYLIST)
                playlist = int(ev.param);
            if (n > 0)
                bytes += n;
            else
                QThread::msleep(10);
        }
        ok = playlist == 0 && bytes >= 6144;
        if (!ok)
            std::fprintf(stderr, "the film behind the menu does not play (playlist %d, %lld bytes)\n", playlist,
                         static_cast<long long>(bytes));
    }
    if (ok)
        std::printf("film behind the menu: playlist %05d, %lld bytes read\n", playlist, static_cast<long long>(bytes));
    bd_close(bd);
    std::puts(ok ? "BD-J menu: ok" : "BD-J menu: FAILED");
    return ok ? 0 : 1;
}
