#pragma once

#include <QList>
#include <QString>
#include <QVariantMap>

// Wiedergabe an die Maschine anpassen: Welche Skalierungsqualität und welcher Decoder-Weg passen
// zur Grafikhardware, und was wird zurückgenommen, wenn Bilder verloren gehen.
//
// Im Profil:  quality  "auto"   Stufe nach der Grafikhardware, bei Überlast zur Laufzeit gesenkt
//             hwdec    "smart"  Hardware-Decoder, wo er dem Renderer direkt liefern kann; kopierend,
//                               wenn ein Filter auf der CPU rechnet (3D-Umrechnung); aus für MVC
//             adaptive true     Laufzeit-Anpassung (Governor) ein
namespace Tuning {

enum GpuClass { GpuUnknown, GpuSoftware, GpuIntegrated, GpuDiscrete };

struct Hardware
{
    QString renderer; // GL_RENDERER
    QString vendor;   // GL_VENDOR
    GpuClass gpu = GpuUnknown;
    int cores = 1;
};

// Aus den Zeichenketten des Grafiktreibers: Software-Renderer (llvmpipe, SwiftShader, Microsoft
// Basic Render Driver, Apple Software Renderer), integrierte oder eigenständige Grafik
GpuClass classifyGpu(const QString &renderer, const QString &vendor);
// OpenGL-Kontext ohne Fenster anlegen und abfragen (GUI-Thread); wird einmal ermittelt.
// LUMEN_GPU=<renderer> überschreibt das Ergebnis (Tests, Fehlersuche).
const Hardware &hardware();
QString gpuClassName(GpuClass c);

// Skalierungsstufen von der aufwendigsten zur sparsamsten; "minimal" = "fast" ohne Debanding,
// Dithering und Zwischenbilder
QStringList qualityTiers();
// Ausgangsstufe für quality "auto"
QString qualityFor(const Hardware &hw);

// Was die Wiedergabe gerade verlangt
struct Context
{
    bool cpuFilter = false;  // ein Filter rechnet auf der CPU (stereo3d)
    bool mvc = false;        // beide Ansichten eines MVC-Stroms: nur der Software-Decoder kann das
    bool sequential = false; // 3D-Bildfolge: ein Bild je Bildwechsel (120 Hz und mehr), keines darf fehlen
    int renderLevel = 0;     // Stufen, die der Governor zurückgenommen hat
    int decodeLevel = 0;
};

// Decoder-Verfahren für hwdec "smart" auf dieser Plattform (ohne/mit Kopie in den Arbeitsspeicher)
QString platformHwdec(bool copy);

// Profil -> Profil mit festen Werten für quality und hwdec (für ProfileManager::toMpvOptions)
QVariantMap resolve(const QVariantMap &profile, const Hardware &hw, const Context &context);
// Zusätzliche mpv-Optionen der zurückgenommenen Stufen (leer bei 0/0); immer vollständig, damit
// ein Zurücksetzen alle Werte wieder auf den Normalzustand bringt
QVariantMap reliefOptions(const QVariantMap &resolvedProfile, const Context &context);
int maxRenderLevel(const QVariantMap &profile, const Hardware &hw);
const int kMaxDecodeLevel = 3;

// Beobachtet verlorene Bilder und entscheidet, wann eine Stufe zurückgenommen wird.
// Einmal je Sekunde mit dem Stand der Zähler füttern.
class Governor
{
public:
    struct Sample
    {
        double time = 0;        // Sekunden (monoton)
        qint64 voDrops = 0;     // frame-drop-count
        qint64 decoderDrops = 0; // decoder-frame-drop-count
        qint64 delayed = 0;     // vo-delayed-frame-count
        double fps = 0;         // Bildrate der Quelle
        double renderMs = -1;   // Renderzeit je Bild (vo-passes), -1 = unbekannt
        double pixelRate = 0;   // Bildpunkte je Sekunde, die der Decoder liefern muss
        bool steady = false;    // spielt mit normaler Geschwindigkeit, puffert nicht
        bool software = false;  // Software-Decoder
    };
    enum Action { None, LowerRender, RelieveDecoder };

    void reset();
    // Nach Start, Sprung, Pause, Formatwechsel: Zähler so lange nicht werten
    void hold(double seconds, double now);
    void setLimits(int maxRender, int maxDecode);
    void setLevels(int render, int decode);
    int renderLevel() const { return m_render; }
    int decodeLevel() const { return m_decode; }
    Action feed(const Sample &s);

private:
    QList<Sample> m_window;
    double m_holdUntil = 0;
    int m_render = 0, m_decode = 0;
    int m_maxRender = 0, m_maxDecode = kMaxDecodeLevel;
};

// Schlüssel, unter dem gelernte Stufen gemerkt werden: Bildgröße und -rate in groben Klassen
QString loadClass(int width, int height, double fps);

// JPEG 2000 (DCP) rechnet nur die CPU. Stufen, die eine zu langsame Maschine entlasten: zuerst
// die feinsten Bit-Ebenen der Codeblöcke auslassen (volle Auflösung, die Abweichung liegt unter
// dem, was eine Ausgabe mit 8 oder 10 Bit zeigt: 51 dB bei 2 Ebenen, 46 dB bei 4), erst danach
// die Auflösung halbieren.
struct J2kRelief
{
    int lowres = 0;     // zusätzliche Halbierungen der Auflösung
    int skipPlanes = 0; // ausgelassene Bit-Ebenen je Codeblock
};
const int kMaxJ2kRelief = 5;
J2kRelief j2kRelief(int level);
// Ausgangsstufe, geschätzt aus der Datenrate des Bildes (Byte je Sekunde) und der Zahl der Kerne.
// Schätzt nur die Stufen ohne sichtbaren Verlust; alles Weitere entscheidet die Wiedergabe selbst.
int j2kStartLevel(double bytesPerSecond, int cores);

} // namespace Tuning
