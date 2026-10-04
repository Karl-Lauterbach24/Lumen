// Test für retimeM2ts: verschiebt die Zeitstempel einer M2TS-Datei (192-Byte-Pakete).
//
//   tsretime_test <in.m2ts> <out.m2ts> <offset in 90-kHz-Ticks>
//
// Prüfung danach mit ffprobe: jede PTS/DTS liegt genau um den Versatz später (tools/test_bd3d.sh).
#include "../src/TsRetime.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

int main(int argc, char **argv)
{
    if (argc < 4) {
        std::fprintf(stderr, "usage: %s <in.m2ts> <out.m2ts> <offset>\n", argv[0]);
        return 2;
    }
    FILE *in = std::fopen(argv[1], "rb");
    if (!in) {
        std::fprintf(stderr, "kann %s nicht lesen\n", argv[1]);
        return 1;
    }
    std::vector<uint8_t> data;
    uint8_t buf[192 * 512];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, in)) > 0;)
        data.insert(data.end(), buf, buf + n);
    std::fclose(in);
    // in zwei ungleichen Teilen, wie die Blöcke aus libbluray: Paketgrenzen bleiben erhalten
    const size_t half = data.size() / 192 / 3 * 192;
    const long long offset = std::atoll(argv[3]);
    retimeM2ts(data.data(), half, offset);
    retimeM2ts(data.data() + half, data.size() - half, offset);
    FILE *out = std::fopen(argv[2], "wb");
    if (!out || std::fwrite(data.data(), 1, data.size(), out) != data.size()) {
        std::fprintf(stderr, "kann %s nicht schreiben\n", argv[2]);
        return 1;
    }
    std::fclose(out);
    std::printf("%zu Pakete, Versatz %lld\n", data.size() / 192, offset);
    return 0;
}
