#include "UiSynth.h"

#include <cmath>
#include <cstring>

namespace {

const double kTau = 6.283185307179586;

// Die vier Akkorde, jeder mit sechs Stimmen (Hz) und ihrem Gewicht
struct Chord
{
    double freq[6];
    float weight[6];
};
const Chord kChords[4] = {
    // (die tiefen Stimmen leise: ein Fernseher gibt sie nicht wieder, eine Anlage mit Basslautsprecher dröhnt
    // davon – getragen wird die Fläche von der Mitte)
    // D add9:   D2      A2      D3      F#3     A3      E4
    {{73.42, 110.00, 146.83, 185.00, 220.00, 329.63}, {0.34f, 0.50f, 0.90f, 0.84f, 0.80f, 0.62f}},
    // h-Moll 7: H1      H2      F#3     A3      D4      E4
    {{61.74, 123.47, 185.00, 220.00, 293.66, 329.63}, {0.22f, 0.62f, 0.90f, 0.82f, 0.70f, 0.58f}},
    // G maj7:   G2      D3      G3      H3      D4      F#4
    {{98.00, 146.83, 196.00, 246.94, 293.66, 369.99}, {0.42f, 0.84f, 0.88f, 0.80f, 0.70f, 0.54f}},
    // A sus4:   A2      E3      A3      D4      E4      H4
    {{110.00, 164.81, 220.00, 293.66, 329.63, 493.88}, {0.46f, 0.84f, 0.88f, 0.78f, 0.68f, 0.44f}},
};
const double kChordSeconds = UiSynth::kCycleSeconds / 4;
const double kFadeSeconds = 7.0;

// Glockentöne im Kreis: Zeit (s), Ton (Hz, D-Dur-Fünftonleiter), Seite (0 links … 1 rechts)
struct Bell
{
    double at, freq;
    float pan;
};
const Bell kBells[] = {
    {3.5, 880.00, 0.30f},  {9.0, 739.99, 0.72f},   {14.5, 1174.66, 0.42f}, {21.0, 587.33, 0.64f},  {26.5, 880.00, 0.24f},
    {33.0, 987.77, 0.70f}, {39.5, 739.99, 0.36f},  {44.0, 1174.66, 0.60f}, {50.5, 659.26, 0.28f},  {56.0, 880.00, 0.74f},
    {62.5, 587.33, 0.44f}, {67.5, 739.99, 0.58f},
};
const double kBellSeconds = 3.2;

// Bruchteil einer Periode -> Sinus, ohne dass die Phase je groß wird
inline float wave(double turns)
{
    return float(std::sin(kTau * (turns - std::floor(turns))));
}

// 0 … 1 weich (Kosinus)
inline float smooth(double x)
{
    if (x <= 0)
        return 0;
    if (x >= 1)
        return 1;
    return float(0.5 - 0.5 * std::cos(kTau * 0.5 * x));
}

} // namespace

UiSynth::UiSynth(int rate)
    : m_rate(rate > 8000 ? rate : 48000)
{
    m_delayL.assign(size_t(0.311 * m_rate), 0.0f);
    m_delayR.assign(size_t(0.467 * m_rate), 0.0f);
    m_pending.reserve(16);
}

void UiSynth::addNote(double delayMs, double freq, double freqEnd, double lengthMs, float amp, float pan, float send, float second)
{
    Note n;
    n.freq = freq;
    n.freqEnd = freqEnd;
    n.length = lengthMs / 1000.0;
    n.delay = delayMs / 1000.0;
    n.amp = amp;
    n.pan = pan;
    n.send = send;
    n.second = second;
    n.live = true;
    m_pending.push_back(n);
}

void UiSynth::trigger(const char *name)
{
    if (!name)
        return;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_pending.size() > 24)
        return; // jemand hält eine Taste: mehr als das ist nicht zu hören
    const auto is = [name](const char *other) { return std::strcmp(name, other) == 0; };
    if (is("move")) {
        addNote(0, 1250, 1010, 46, 0.085f, 0.5f, 0.10f);
    } else if (is("key")) {
        addNote(0, 930, 820, 34, 0.060f, 0.5f, 0.06f);
    } else if (is("select")) {
        addNote(0, 880.00, 880.00, 95, 0.110f, 0.44f, 0.30f);
        addNote(70, 1174.66, 1174.66, 190, 0.115f, 0.56f, 0.35f);
    } else if (is("back")) {
        addNote(0, 1174.66, 1174.66, 85, 0.095f, 0.56f, 0.25f);
        addNote(62, 880.00, 880.00, 170, 0.095f, 0.44f, 0.30f);
    } else if (is("on")) {
        addNote(0, 659.26, 659.26, 75, 0.095f, 0.45f, 0.25f);
        addNote(56, 987.77, 987.77, 170, 0.105f, 0.55f, 0.30f);
    } else if (is("off")) {
        addNote(0, 987.77, 987.77, 75, 0.085f, 0.55f, 0.22f);
        addNote(56, 659.26, 659.26, 170, 0.085f, 0.45f, 0.26f);
    } else if (is("error")) {
        addNote(0, 220.00, 196.00, 150, 0.150f, 0.5f, 0.15f, 0.30f);
        addNote(165, 220.00, 185.00, 240, 0.150f, 0.5f, 0.18f, 0.30f);
    } else if (is("done")) {
        addNote(0, 739.99, 739.99, 130, 0.095f, 0.40f, 0.35f);
        addNote(95, 880.00, 880.00, 130, 0.095f, 0.50f, 0.35f);
        addNote(190, 1174.66, 1174.66, 380, 0.105f, 0.60f, 0.45f);
    } else if (is("start")) {
        addNote(0, 293.66, 293.66, 1500, 0.085f, 0.40f, 0.50f, 0.20f);
        addNote(120, 440.00, 440.00, 1500, 0.080f, 0.60f, 0.50f, 0.15f);
        addNote(240, 587.33, 587.33, 1600, 0.080f, 0.35f, 0.55f);
        addNote(360, 739.99, 739.99, 1700, 0.070f, 0.65f, 0.55f);
        addNote(480, 880.00, 880.00, 1900, 0.065f, 0.45f, 0.60f);
        addNote(660, 1174.66, 1174.66, 2400, 0.050f, 0.55f, 0.65f);
    }
}

// Die Klangfläche an einer Stelle des Kreises: zwei Akkorde im Übergang, jede Stimme doppelt und leicht
// gegeneinander verstimmt (links tiefer, rechts höher), jede atmet in ihrem eigenen langsamen Takt.
void UiSynth::musicFrame(double cycle, float &left, float &right, float &sendLeft, float &sendRight)
{
    const int index = int(cycle / kChordSeconds) & 3;
    const double within = cycle - index * kChordSeconds;
    // In den letzten kFadeSeconds eines Akkords kommt der nächste schon auf
    const float next = smooth((within - (kChordSeconds - kFadeSeconds)) / kFadeSeconds);
    const float gains[2] = {std::sqrt(1.0f - next), std::sqrt(next)};
    float l = 0, r = 0;
    for (int which = 0; which < 2; ++which) {
        if (gains[which] < 1e-4f)
            continue;
        const int chordIndex = (index + which) & 3;
        const Chord &chord = kChords[chordIndex];
        for (int v = 0; v < 6; ++v) {
            const double f = chord.freq[v];
            // Atem: ein ganzes Vielfaches des Kreises, damit er sich schließt (3 … 8 Züge in 72 Sekunden)
            const double breaths = 3 + ((chordIndex * 5 + v * 2) % 6);
            const float breath = 0.74f + 0.26f * wave(cycle * breaths / UiSynth::kCycleSeconds + v * 0.17 + chordIndex * 0.31);
            const float a = chord.weight[v] * breath * gains[which];
            // Die Phase aus der Stelle im Kreis: f * 72 s ist für diese Töne keine ganze Zahl von Perioden,
            // darum wird jede Frequenz auf die nächste gerundet, die im Kreis aufgeht (höchstens 0,007 Hz daneben).
            const double fl = std::round(f * 0.9988 * UiSynth::kCycleSeconds) / UiSynth::kCycleSeconds;
            const double fr = std::round(f * 1.0012 * UiSynth::kCycleSeconds) / UiSynth::kCycleSeconds;
            const double tl = cycle * fl, tr = cycle * fr;
            l += a * (wave(tl) + 0.22f * wave(2 * tl) + 0.07f * wave(3 * tl));
            r += a * (wave(tr) + 0.22f * wave(2 * tr) + 0.07f * wave(3 * tr));
        }
    }
    l *= 0.040f;
    r *= 0.040f;
    float bl = 0, br = 0;
    for (const Bell &bell : kBells) {
        double age = cycle - bell.at;
        if (age < 0)
            age += UiSynth::kCycleSeconds; // ein Ton vom Ende des Kreises klingt in den Anfang hinein
        if (age >= kBellSeconds)
            continue;
        const double f = std::round(bell.freq * UiSynth::kCycleSeconds) / UiSynth::kCycleSeconds;
        const float attack = smooth(age / 0.012);
        const float env = attack * float(std::exp(-age * 1.9)) * smooth((kBellSeconds - age) / 0.4);
        const float s = env * (wave(age * f) + 0.16f * wave(age * f * 2.0) + 0.05f * wave(age * f * 3.0));
        bl += s * (1.0f - bell.pan);
        br += s * bell.pan;
    }
    bl *= 0.030f;
    br *= 0.030f;
    left = l + bl;
    right = r + br;
    sendLeft = 0.18f * l + 0.75f * bl;
    sendRight = 0.18f * r + 0.75f * br;
}

void UiSynth::render(float *out, int frames)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const Note &n : m_pending) {
            for (Note &slot : m_notes) {
                if (!slot.live) {
                    slot = n;
                    break;
                }
            }
        }
        m_pending.clear();
    }
    const double dt = 1.0 / m_rate;
    const bool musicWanted = m_musicWanted.load();
    const float musicLevel = m_musicLevel.load();
    const float effectLevel = m_effectLevel.load();
    // Ein- und Ausblenden der Klangfläche: gut zwei Sekunden hinein, eine halbe hinaus
    const float up = float(dt / 2.2), down = float(dt / 0.5);
    const float lpK = float(1.0 - std::exp(-kTau * 5200.0 * dt));
    const float dampK = float(1.0 - std::exp(-kTau * 2600.0 * dt));
    bool sounding = false;

    for (int i = 0; i < frames; ++i) {
        float l = 0, r = 0, sl = 0, sr = 0;

        if (musicWanted && m_musicGain < 1)
            m_musicGain = std::fmin(1.0f, m_musicGain + up);
        else if (!musicWanted && m_musicGain > 0)
            m_musicGain = std::fmax(0.0f, m_musicGain - down);
        if (m_musicGain > 0) {
            float ml, mr, msl, msr;
            musicFrame(m_cycle, ml, mr, msl, msr);
            m_cycle += dt;
            if (m_cycle >= kCycleSeconds)
                m_cycle -= kCycleSeconds;
            // (weich: zuerst das Quadrat des Reglers, dann der Tiefpass)
            const float g = m_musicGain * m_musicGain * musicLevel;
            m_lpL += lpK * (ml * g - m_lpL);
            m_lpR += lpK * (mr * g - m_lpR);
            l += m_lpL;
            r += m_lpR;
            sl += msl * g;
            sr += msr * g;
            sounding = true;
        } else {
            m_lpL = m_lpR = 0;
        }

        for (Note &n : m_notes) {
            if (!n.live)
                continue;
            sounding = true;
            if (n.delay > 0) {
                n.delay -= dt;
                continue;
            }
            const double x = n.age / n.length;
            if (x >= 1) {
                n.live = false;
                continue;
            }
            const double f = n.freq + (n.freqEnd - n.freq) * x;
            n.phase += f * dt;
            if (n.phase >= 1)
                n.phase -= std::floor(n.phase);
            // drei Millisekunden hinein, dann wie eine angeschlagene Saite hinaus (−60 dB am Ende)
            const float env = smooth(n.age / 0.003) * float(std::exp(-6.9 * x)) * smooth((1.0 - x) / 0.08);
            float s = wave(n.phase);
            if (n.second > 0)
                s += n.second * wave(2 * n.phase);
            s *= env * n.amp * effectLevel;
            l += s * (1.0f - n.pan) * 1.4f;
            r += s * n.pan * 1.4f;
            sl += s * n.send;
            sr += s * n.send;
            n.age += dt;
        }

        // Hall: was links hineingeht, kommt rechts wieder und umgekehrt
        const float dl = m_delayL[m_posL], dr = m_delayR[m_posR];
        m_dampL += dampK * (dl - m_dampL);
        m_dampR += dampK * (dr - m_dampR);
        m_delayL[m_posL] = sl + m_dampR * 0.43f;
        m_delayR[m_posR] = sr + m_dampL * 0.43f;
        if (++m_posL >= m_delayL.size())
            m_posL = 0;
        if (++m_posR >= m_delayR.size())
            m_posR = 0;
        l += dl * 0.55f;
        r += dr * 0.55f;
        if (std::fabs(dl) > 2e-5f || std::fabs(dr) > 2e-5f)
            sounding = true;

        // nie hart an die Grenze
        out[2 * i] = std::tanh(l * 1.2f) / 1.2f;
        out[2 * i + 1] = std::tanh(r * 1.2f) / 1.2f;
    }
    if (sounding) {
        m_quiet = 0;
        m_active.store(true);
    } else {
        m_quiet += frames * dt;
        if (m_quiet > 0.5)
            m_active.store(false);
    }
}
