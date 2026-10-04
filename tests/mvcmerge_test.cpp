// Test für MvcMerger: öffnet eine (synthetische) Blu-ray-3D-Struktur über
// libbluray, führt Basis- und abhängige Ansicht zusammen und schreibt den
// Ergebnis-TS. Prüfung danach mit FFmpeg-mvc:
//   ffmpeg -view_ids -1 -i merged.m2ts -f rawvideo -pix_fmt gray -
//
//   mvcmerge_test <disc> <playlist> <out.m2ts> [seek_sekunden] [max_MiB]
// (max_MiB: nach so vielen gelesenen MiB aufhören – für echte Discs)
#include "../src/MvcMerger.h"

#include <libbluray/bluray.h>

#include <QByteArray>
#include <cstdio>
#include <cstdlib>

int main(int argc, char **argv)
{
    if (argc < 4) {
        std::fprintf(stderr, "usage: %s <disc> <playlist> <out.m2ts> [seek_s] [max_MiB]\n", argv[0]);
        return 2;
    }
    BLURAY *bd = bd_open(argv[1], nullptr);
    if (!bd) {
        std::fprintf(stderr, "bd_open fehlgeschlagen\n");
        return 1;
    }
    const BLURAY_DISC_INFO *di = bd_get_disc_info(bd);
    std::printf("bluray_detected=%d content_exist_3D=%d titles=%u\n", di->bluray_detected, di->content_exist_3D, di->num_titles);

    const uint32_t playlist = uint32_t(std::atoi(argv[2]));
    bd_get_titles(bd, TITLES_ALL, 0);
    if (!bd_select_playlist(bd, playlist)) {
        std::fprintf(stderr, "bd_select_playlist fehlgeschlagen\n");
        return 1;
    }
    MvcMerger mvc(bd);
    const bool is3d = mvc.setPlaylist(playlist);
    std::printf("MVC aktiv=%d, Basis rechts=%d\n", is3d, mvc.baseViewIsRight());
    if (argc > 4 && std::atof(argv[4]) > 0) {
        bd_seek_time(bd, uint64_t(std::atof(argv[4]) * 90000));
        mvc.reset();
    }
    const int64_t limit = argc > 5 ? int64_t(std::atof(argv[5]) * 1024 * 1024) : 0;

    FILE *out = std::fopen(argv[3], "wb");
    QByteArray buf(6144 * 32, Qt::Uninitialized), merged;
    int64_t in = 0;
    for (;;) {
        BD_EVENT ev;
        const int r = bd_read_ext(bd, reinterpret_cast<unsigned char *>(buf.data()), buf.size(), &ev);
        merged.clear();
        do { // auch das Ereignis, das bd_read_ext() selbst liefert
            if (ev.event == BD_EVENT_PLAYITEM)
                mvc.setPlayItem(int(ev.param), merged);
        } while (bd_get_event(bd, &ev) && ev.event != BD_EVENT_NONE);
        if (r <= 0 || (limit > 0 && in >= limit))
            break;
        in += r;
        mvc.process(reinterpret_cast<const uint8_t *>(buf.constData()), size_t(r), merged);
        std::fwrite(merged.constData(), 1, size_t(merged.size()), out);
    }
    // letzte Zugriffseinheit ausgeben (PES endet erst mit dem nächsten Paketstart)
    merged.clear();
    mvc.flush(merged);
    std::fwrite(merged.constData(), 1, size_t(merged.size()), out);
    std::fclose(out);
    std::printf("gelesen %lld Byte, zusammengeführt %llu Bilder, ohne Partner %llu\n",
                static_cast<long long>(in), mvc.mergedFrames(), mvc.missingFrames());
    bd_close(bd);
    return mvc.mergedFrames() > 0 ? 0 : 1;
}
