// Rechnet die Klänge der Oberfläche von LumenOS in Dateien (resources/sounds): jeden Ton für sich, bis er
// verklungen ist, und die Klangfläche als einen Kreis von 72 Sekunden, der ohne Naht von vorn beginnt.
//
//   c++ -O2 -std=c++17 tools/soundgen/*.cpp -o soundgen && ./soundgen <Ordner>
//
// Heraus kommen WAV-Dateien (48 kHz, 16 Bit, Stereo); tools/make_sounds.sh packt sie für das Programm.
// Wem sie nicht gefallen: LumenOS spielt, was in seinem Klangordner liegt – jede Datei lässt sich durch
// eine eigene ersetzen (os/README.md).
#include "UiSynth.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

static bool writeWav(const std::string &path, const std::vector<float> &samples, int rate)
{
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f)
        return false;
    const uint32_t bytes = uint32_t(samples.size() * 2);
    const auto u32 = [f](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    const auto u16 = [f](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f);
    u32(36 + bytes);
    std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16);
    u16(1);
    u16(2);
    u32(uint32_t(rate));
    u32(uint32_t(rate) * 4);
    u16(4);
    u16(16);
    std::fwrite("data", 1, 4, f);
    u32(bytes);
    for (float s : samples) {
        // (mit etwas Rauschen gerundet: ein leiser Ausklang zeigt sonst Stufen)
        static uint32_t seed = 22222;
        seed = seed * 1664525u + 1013904223u;
        const float dither = (float((seed >> 8) & 0xffff) / 65536.0f - 0.5f);
        const int v = int(std::lround(double(s) * 32767.0 + dither));
        const int16_t out = int16_t(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        std::fwrite(&out, 2, 1, f);
    }
    std::fclose(f);
    return true;
}

int main(int argc, char **argv)
{
    const std::string dir = argc > 1 ? argv[1] : ".";
    const int rate = 48000;
    const char *effects[] = {"move", "key", "select", "back", "on", "off", "error", "done", "start"};
    for (const char *name : effects) {
        UiSynth synth(rate);
        synth.setEffectLevel(1.8f); // (die Töne für sich: deutlich über der Klangfläche)
        synth.trigger(name);
        std::vector<float> all;
        std::vector<float> block(960);
        // bis nichts mehr klingt (der Hall eingeschlossen), höchstens sechs Sekunden
        for (int i = 0; i < 600; ++i) {
            synth.render(block.data(), 480);
            all.insert(all.end(), block.begin(), block.end());
            if (i > 2 && !synth.active())
                break;
        }
        // das Schweigen am Ende ab (bis auf 20 ms)
        size_t end = all.size();
        while (end > 2 && std::fabs(all[end - 1]) < 1.0f / 32768 && std::fabs(all[end - 2]) < 1.0f / 32768)
            end -= 2;
        end = std::min(all.size(), end + size_t(rate) / 50 * 2);
        all.resize(end);
        if (!writeWav(dir + "/" + name + ".wav", all, rate))
            return 1;
        std::printf("%s: %.2f s\n", name, all.size() / 2.0 / rate);
    }
    {
        // Zwei Kreise; der zweite ist die Datei – in seinem Anfang klingt schon, was am Ende noch nachhallt
        UiSynth synth(rate);
        synth.setMusicLevel(1.0f);
        synth.setMusic(true);
        const size_t cycle = size_t(UiSynth::kCycleSeconds * rate);
        std::vector<float> all(cycle * 2 * 2);
        for (size_t at = 0; at < cycle * 2; at += 480)
            synth.render(all.data() + at * 2, int(std::min<size_t>(480, cycle * 2 - at)));
        std::vector<float> loop(all.begin() + long(cycle * 2), all.end());
        if (!writeWav(dir + "/music.wav", loop, rate))
            return 1;
        std::printf("music: %.2f s\n", loop.size() / 2.0 / rate);
    }
    return 0;
}
