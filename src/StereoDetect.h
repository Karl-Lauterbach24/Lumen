#pragma once

#include <QList>
#include <QString>

#include <atomic>

// Erkennt, ob und wie eine Videodatei 3D enthält. Vier Quellen, in dieser Rangfolge:
//   1. Angaben im Container/Strom (Matroska StereoMode, MP4 st3d, H.264-SEI) – von mpv gelesen
//   2. Kennungen im Dateinamen ("3D", "SBS", "H-SBS", "HOU", "TAB" …)
//   3. das Bild selbst: linke/rechte bzw. obere/untere Bildhälfte zeigen fast dasselbe
//   4. die Bildgröße (3840x1080, 1920x2160) – nur, wenn sich das Bild nicht prüfen lässt
// Das Ergebnis ist ein Kürzel des FFmpeg-Filters stereo3d (sbsl, sbs2l, abl, ab2l, …r, irl, al …),
// wie es Stereo3D::filter() als Quellformat erwartet.
namespace StereoDetect {

enum Layout { NoLayout, SideBySide, TopBottom, Interleaved, Alternating, Mvc };

// Was sich ohne die Bildgröße sagen lässt
struct Hint
{
    bool is3d = false;       // 3D, auch wenn die Anordnung noch offen ist
    bool tagged3d = false;   // im Namen steht ausdrücklich "3D" (oder die Endung ist .mk3d/.ssif)
    Layout layout = NoLayout;
    int half = -1;           // 1 = halbe Auflösung je Auge, 0 = volle, -1 = unbekannt
    bool rightFirst = false; // rechtes Auge links bzw. oben
    bool decided() const { return layout != NoLayout; }
};

struct Result
{
    QString format = QStringLiteral("none"); // stereo3d-Kürzel
    QString source;                          // "metadata" | "name" | "picture" | "size"
    double confidence = 0;                   // 0 … 1
    // Der Strom trägt zwei Ansichten (H.264/MVC): kein Bildformat – der Decoder muss beide liefern
    // (nur mit FFmpeg-mvc erkennbar)
    bool mvc = false;
    bool found() const { return format != QLatin1String("none"); }
};

// Dateiname (ohne Pfad) -> Hinweis
Hint fromName(const QString &fileName);

// mpv-Eigenschaft video-params/stereo-in ("mono", "sbs2l", "ab2r", "irl", "al" …) -> Hinweis
Hint fromMetadata(const QString &stereoIn);

// Hinweis + Bildgröße -> Kürzel. Ob jedes Auge die volle oder die halbe Auflösung hat, ergibt
// sich aus dem Seitenverhältnis einer Hälfte, wenn der Hinweis es nicht sagt.
QString format(const Hint &hint, int width, int height);

// Nur aus der Bildgröße: doppelt breite oder doppelt hohe Bilder
Hint fromSize(int width, int height);

// Ähnlichkeit der Bildhälften eines Graustufenbilds, je -1 … 1; < -1: nicht auswertbar
// (zu dunkel oder zu gleichförmig)
struct Scores
{
    double sideBySide = -2;
    double topBottom = -2;
};
Scores compare(const unsigned char *gray, int width, int height, int stride);

// Mehrere Bilder zusammen. hint3d: der Dateiname sagt schon "3D" – dann genügt weniger.
Hint fromScores(const QList<Scores> &frames, bool hint3d, double *confidence = nullptr);

// Datei öffnen, einige Bilder über die Laufzeit verteilt dekodieren und vergleichen.
// Läuft synchron (für einen Arbeits-Thread); cancel bricht ab. width/height: Bildgröße der Datei.
Hint analyzeFile(const QString &path, bool hint3d, const std::atomic_bool *cancel = nullptr, double *confidence = nullptr,
                 int *width = nullptr, int *height = nullptr, QList<Scores> *scores = nullptr, bool *mvc = nullptr);

// Alles, was ohne mpv geht: Name, dann Bild, dann Größe
Result detectFile(const QString &path, const std::atomic_bool *cancel = nullptr);

} // namespace StereoDetect
