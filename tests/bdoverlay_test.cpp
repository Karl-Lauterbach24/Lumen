// Test für BdOverlay: lässt libbluray die Untertitel (PG) eines Titels dekodieren und zeichnet die
// komprimierten Overlays in eine Ebene – derselbe Weg wie die Menügrafik im HDMV-Modus.
//
//   bdoverlay_test <disc|iso> <out.png> [max_MiB]
//
// Speichert die erste Ebene mit sichtbarem Inhalt und endet mit 0; ohne Grafik mit 1.
#include "../src/BdOverlay.h"

#include <libbluray/bluray.h>
#include <libbluray/overlay.h>
#include <libbluray/player_settings.h>

#include <QByteArray>
#include <cstdio>
#include <cstdlib>

namespace {
struct State
{
    QImage plane;
    QString out;
    int flushes = 0;
    bool saved = false;
};

void overlay(void *handle, const BD_OVERLAY *const ov)
{
    auto *s = static_cast<State *>(handle);
    if (!ov || ov->plane != BD_OVERLAY_PG)
        return;
    if (!BdOverlay::apply(s->plane, *ov) || s->plane.isNull())
        return;
    ++s->flushes;
    int visible = 0;
    for (int y = 0; y < s->plane.height(); ++y) {
        const auto *row = reinterpret_cast<const uint32_t *>(s->plane.constScanLine(y));
        for (int x = 0; x < s->plane.width(); ++x)
            visible += (row[x] >> 24) != 0;
    }
    if (visible > 200 && !s->saved) {
        s->saved = s->plane.save(s->out);
        std::printf("Ebene %dx%d, %d sichtbare Punkte -> %s\n", s->plane.width(), s->plane.height(), visible, qPrintable(s->out));
    }
}
} // namespace

int main(int argc, char **argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <disc|iso> <out.png> [max_MiB]\n", argv[0]);
        return 2;
    }
    BLURAY *bd = bd_open(argv[1], nullptr);
    if (!bd) {
        std::fprintf(stderr, "bd_open fehlgeschlagen\n");
        return 1;
    }
    State s;
    s.out = QString::fromLocal8Bit(argv[2]);
    const int64_t limit = int64_t((argc > 3 ? std::atof(argv[3]) : 64) * 1024 * 1024);
    bd_set_player_setting(bd, BLURAY_PLAYER_SETTING_DECODE_PG, 1);
    bd_register_overlay_proc(bd, &s, overlay);
    bd_get_titles(bd, TITLES_RELEVANT, 0);
    const int title = bd_get_main_title(bd);
    if (title < 0 || !bd_select_title(bd, uint32_t(title))) {
        std::fprintf(stderr, "Titel nicht wählbar\n");
        return 1;
    }
    bd_select_stream(bd, BLURAY_PG_TEXTST_STREAM, 1, 1);

    QByteArray buf(6144 * 32, Qt::Uninitialized);
    int64_t in = 0;
    while (!s.saved && in < limit) {
        BD_EVENT ev;
        const int r = bd_read_ext(bd, reinterpret_cast<unsigned char *>(buf.data()), int(buf.size()), &ev);
        while (bd_get_event(bd, &ev) && ev.event != BD_EVENT_NONE) {
        }
        if (r <= 0)
            break;
        in += r;
    }
    bd_register_overlay_proc(bd, nullptr, nullptr);
    bd_close(bd);
    std::printf("gelesen %lld Byte, %d Overlay-Abschlüsse\n", static_cast<long long>(in), s.flushes);
    return s.saved ? 0 : 1;
}
