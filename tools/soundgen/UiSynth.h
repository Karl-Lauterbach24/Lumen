#pragma once

#include <array>
#include <atomic>
#include <mutex>
#include <vector>

// Der Klang der Oberfläche von LumenOS, gerechnet statt abgespielt: kurze Töne für das, was man tut
// (wählen, öffnen, zurück, ein Schalter, ein Fehler), und eine ruhige Klangfläche für die Zeit in den
// Menüs. Nichts davon liegt als Datei vor – es gibt also nichts zu laden und nichts, das jemandem gehört.
//
// Die Klangfläche ist ein Kreis aus vier Akkorden in D-Dur (D add9 – h-Moll 7 – G maj7 – A sus4), jeder
// 18 Sekunden, weich ineinander geblendet, dazu einzelne Glockentöne aus der Fünftonleiter. Nach 72
// Sekunden beginnt sie von vorn, ohne dass eine Naht zu hören wäre: Alles in ihr hängt nur von der
// Stelle im Kreis ab.
//
// Kein Qt hier: gerechnet wird im Tonfaden (UiAudio) und im Test (tests/uisound_test.cpp).
class UiSynth
{
public:
    explicit UiSynth(int rate = 48000);

    // Ein Ton der Oberfläche: "move", "key", "select", "back", "on", "off", "error", "done", "start".
    // Aus jedem Faden; unbekannte Namen tun nichts.
    void trigger(const char *name);
    void setMusic(bool on) { m_musicWanted.store(on); }
    void setMusicLevel(float level) { m_musicLevel.store(level); }   // 0 … 1
    void setEffectLevel(float level) { m_effectLevel.store(level); } // 0 … 1
    // true, solange etwas klingt oder ausklingt (danach kann der Ausgang geschlossen werden)
    bool active() const { return m_active.load(); }

    // frames Abtastwerte, links und rechts abwechselnd, −1 … 1
    void render(float *out, int frames);

    int rate() const { return m_rate; }
    static constexpr double kCycleSeconds = 72.0;

private:
    struct Note
    {
        double phase = 0;
        double freq = 0, freqEnd = 0;
        double age = 0, length = 0; // Sekunden
        double delay = 0;           // … bis er beginnt
        float amp = 0, pan = 0.5f, send = 0.2f;
        float second = 0;           // Anteil der Oktave darüber
        bool live = false;
    };
    void addNote(double delayMs, double freq, double freqEnd, double lengthMs, float amp, float pan = 0.5f, float send = 0.25f, float second = 0.0f);
    void musicFrame(double cycle, float &left, float &right, float &sendLeft, float &sendRight);

    int m_rate;
    std::mutex m_mutex; // schützt m_pending
    std::vector<Note> m_pending;
    std::array<Note, 32> m_notes{};

    std::atomic<bool> m_musicWanted{false};
    std::atomic<float> m_musicLevel{0.6f};
    std::atomic<float> m_effectLevel{0.8f};
    std::atomic<bool> m_active{false};
    float m_musicGain = 0;   // folgt m_musicWanted weich
    double m_cycle = 0;      // Stelle im Kreis, Sekunden
    double m_quiet = 0;      // so lange klingt nichts mehr

    // Hall: zwei Verzögerungen, die sich über Kreuz speisen, im Rücklauf gedämpft
    std::vector<float> m_delayL, m_delayR;
    size_t m_posL = 0, m_posR = 0;
    float m_dampL = 0, m_dampR = 0;
    // weicher Tiefpass auf der Klangfläche
    float m_lpL = 0, m_lpR = 0;
};
