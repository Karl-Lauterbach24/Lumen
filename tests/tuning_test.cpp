// Anpassung an die Maschine: Einordnung der Grafikhardware, Wahl von Skalierungsstufe und
// Decoder-Weg, und der Governor, der bei verlorenen Bildern Stufen zurücknimmt.
#include "Tuning.h"

#include <QCoreApplication>

#include <cstdio>

static int failures = 0;

static void check(bool ok, const QString &what)
{
    std::printf("%s %s\n", ok ? "OK  " : "FAIL", qPrintable(what));
    if (!ok)
        ++failures;
}

static void gpu(const char *renderer, const char *vendor, Tuning::GpuClass expected)
{
    const Tuning::GpuClass got = Tuning::classifyGpu(QString::fromLatin1(renderer), QString::fromLatin1(vendor));
    check(got == expected, QStringLiteral("%1 -> %2").arg(QString::fromLatin1(renderer), Tuning::gpuClassName(got)));
}

// Governor mit gleichmäßig verlorenen Bildern füttern; liefert die Folge der Entscheidungen
static QString run(Tuning::Governor &g, int seconds, double lostPerSecond, double delayedPerSecond, bool software, double renderMs,
                   double fps = 24, bool steady = true, double pixelRate = 1920.0 * 1080 * 24)
{
    static double t = 0;
    static double vo = 0, delayed = 0;
    QString out;
    for (int i = 0; i < seconds; ++i) {
        t += 1;
        vo += lostPerSecond;
        delayed += delayedPerSecond;
        Tuning::Governor::Sample s;
        s.time = t;
        s.voDrops = qint64(vo);
        s.delayed = qint64(delayed);
        s.fps = fps;
        s.renderMs = renderMs;
        s.steady = steady;
        s.software = software;
        s.pixelRate = pixelRate;
        const Tuning::Governor::Action a = g.feed(s);
        if (a == Tuning::Governor::LowerRender)
            out += QLatin1Char('R');
        else if (a == Tuning::Governor::RelieveDecoder)
            out += QLatin1Char('D');
    }
    return out;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    using namespace Tuning;

    // Treiber-Zeichenketten, wie OpenGL sie meldet
    gpu("llvmpipe (LLVM 19.1.7, 256 bits)", "Mesa", GpuSoftware);
    gpu("Google SwiftShader", "Google Inc.", GpuSoftware);
    gpu("GDI Generic", "Microsoft Corporation", GpuSoftware);
    gpu("Microsoft Basic Render Driver", "Microsoft", GpuSoftware);
    gpu("Apple Software Renderer", "Apple Inc.", GpuSoftware);
    gpu("NVIDIA GeForce RTX 4070/PCIe/SSE2", "NVIDIA Corporation", GpuDiscrete);
    gpu("Quadro P2000/PCIe/SSE2", "NVIDIA Corporation", GpuDiscrete);
    gpu("AMD Radeon RX 6800 XT (radeonsi, navi21, LLVM 19.1.7, DRM 3.59)", "AMD", GpuDiscrete);
    gpu("AMD Radeon(TM) RX 580", "ATI Technologies Inc.", GpuDiscrete);
    gpu("AMD Radeon Pro W6600", "AMD", GpuDiscrete);
    gpu("AMD Radeon Graphics (radeonsi, renoir, LLVM 19.1.7)", "AMD", GpuIntegrated);
    gpu("AMD Radeon(TM) Graphics", "ATI Technologies Inc.", GpuIntegrated);
    gpu("AMD Radeon 780M Graphics", "AMD", GpuIntegrated);
    gpu("Radeon Vega 8 Graphics", "AMD", GpuIntegrated);
    gpu("Mesa Intel(R) UHD Graphics 620 (KBL GT2)", "Intel", GpuIntegrated);
    gpu("Intel(R) Iris(R) Xe Graphics", "Intel", GpuIntegrated);
    gpu("Intel(R) Arc(TM) Graphics", "Intel", GpuIntegrated);
    gpu("Intel(R) Arc(TM) A770 Graphics", "Intel", GpuDiscrete);
    gpu("Apple M3 Pro", "Apple", GpuDiscrete);
    gpu("Intel(R) Iris(TM) Plus Graphics 655", "Intel Inc.", GpuIntegrated);
    gpu("Adreno (TM) 690", "Qualcomm", GpuIntegrated);
    gpu("SVGA3D; build: RELEASE; LLVM;", "VMware, Inc.", GpuIntegrated);
    gpu("", "", GpuUnknown);

    Hardware soft, igpu, card;
    soft.gpu = GpuSoftware;
    igpu.gpu = GpuIntegrated;
    card.gpu = GpuDiscrete;

    // Skalierungsstufe
    const QVariantMap automatic{{"quality", "auto"}, {"hwdec", "smart"}};
    check(resolve(automatic, soft, {}).value("quality") == "fast", "auto, Software-Renderer: fast");
    check(resolve(automatic, igpu, {}).value("quality") == "balanced", "auto, integrierte Grafik: balanced");
    check(resolve(automatic, card, {}).value("quality") == "high", "auto, Grafikkarte: high");
    check(resolve({{"quality", "reference"}}, igpu, {}).value("quality") == "reference", "feste Stufe bleibt");
    Context lowered;
    lowered.renderLevel = 1;
    check(resolve(automatic, card, lowered).value("quality") == "balanced", "eine Stufe zurück: high -> balanced");
    lowered.renderLevel = 2;
    check(resolve(automatic, card, lowered).value("quality") == "fast", "zwei Stufen zurück: fast");
    check(!resolve(automatic, card, lowered).value("tuningMinimal").toBool(), "… noch nicht minimal");
    lowered.renderLevel = 3;
    check(resolve(automatic, card, lowered).value("tuningMinimal").toBool(), "drei Stufen zurück: minimal");
    check(reliefOptions(resolve(automatic, card, lowered), lowered).value("deband") == "no", "minimal: ohne Debanding");
    check(maxRenderLevel(automatic, card) == 3, "Grafikkarte: 3 Stufen möglich");
    check(maxRenderLevel(automatic, soft) == 1, "Software-Renderer: 1 Stufe möglich");
    check(maxRenderLevel({{"quality", "reference"}}, igpu) == 4, "Referenz: 4 Stufen möglich");

    // Decoder-Weg
    Context filter;
    filter.cpuFilter = true;
    Context mvc;
    mvc.mvc = true;
    check(resolve(automatic, card, {}).value("hwdec") == platformHwdec(false), "smart: Hardware-Decoder direkt");
    check(resolve(automatic, card, filter).value("hwdec") == platformHwdec(true), "smart mit CPU-Filter: kopierend");
    check(resolve(automatic, soft, {}).value("hwdec") == platformHwdec(true), "smart mit Software-Renderer: kopierend");
    check(platformHwdec(true).contains("copy") && !platformHwdec(false).contains("copy"), "Plattform-Verfahren: " + platformHwdec(false) + " / " + platformHwdec(true));
    check(resolve(automatic, card, mvc).value("hwdec") == "no", "smart bei MVC: Software");
    check(resolve({{"hwdec", "vaapi"}}, card, filter).value("hwdec") == "vaapi", "feste Wahl bleibt");
    Context relief;
    check(reliefOptions(resolve(automatic, card, relief), relief).value("vd-lavc-skiploopfilter") == "default", "ohne Entlastung: Decoder unverändert");
    check(!reliefOptions(resolve(automatic, card, relief), relief).contains("video-sync"), "ohne Entlastung: Abgleich wie im Profil");
    relief.decodeLevel = 1;
    check(reliefOptions(resolve(automatic, card, relief), relief).value("vd-lavc-skiploopfilter") == "nonref", "Stufe 1: Deblocking nur für Referenzbilder");
    relief.decodeLevel = 3;
    check(reliefOptions(resolve(automatic, card, relief), relief).value("framedrop") == "decoder+vo", "Stufe 3: Bilder im Decoder auslassen");

    // Governor
    Governor g;
    g.setLimits(3, kMaxDecodeLevel);
    check(run(g, 30, 0, 0, true, 5).isEmpty(), "keine Verluste: keine Änderung");
    check(run(g, 30, 0.2, 0, true, 5).isEmpty(), "vereinzelte Aussetzer: keine Änderung");
    check(run(g, 8, 3, 0, true, 35) == "R", "Renderer zu langsam (35 ms bei 24 fps): Stufe zurück");
    check(g.renderLevel() == 1, "… Renderstufe 1");
    check(run(g, 3, 3, 0, true, 35).isEmpty(), "danach Wartezeit");
    check(run(g, 40, 3, 0, true, 35) == "RR", "bleibt es dabei: bis zur letzten Stufe, dann Schluss");
    g.reset();
    g.setLimits(3, kMaxDecodeLevel);
    check(run(g, 8, 3, 0, true, 4) == "D", "Renderer schnell, Software-Decoder: Decoder entlasten");
    check(run(g, 60, 3, 0, true, 4) == "DD", "… bis Stufe 3");
    check(g.decodeLevel() == 3 && g.renderLevel() == 0, "Renderer bleibt unangetastet");
    g.reset();
    g.setLimits(3, kMaxDecodeLevel);
    check(run(g, 40, 3, 0, false, 4).isEmpty(), "Renderer schnell, Hardware-Decoder: nichts zu tun");
    check(run(g, 8, 1, 3, false, 4) == "R", "verspätete Bilder (Abgleich mit dem Bildschirm): Renderer");
    g.reset();
    g.setLimits(2, kMaxDecodeLevel);
    check(run(g, 60, 3, 0, true, -1) == "RDRDD", "Renderzeit unbekannt: abwechselnd, Renderer zuerst");
    g.reset();
    g.setLimits(2, kMaxDecodeLevel);
    check(run(g, 60, 3, 0, true, -1, 24, true, 3840.0 * 2160 * 60) == "DRDRD", "… bei 4K60 im Software-Decoder: Decoder zuerst");
    g.reset();
    g.setLimits(3, kMaxDecodeLevel);
    check(run(g, 3, 30, 0, true, 35, 60) == "R", "die Hälfte der Bilder verloren: schon nach zwei Sekunden");
    g.reset();
    g.setLimits(3, kMaxDecodeLevel);
    check(run(g, 20, 3, 0, true, 35, 24, false).isEmpty(), "Pause/Spulen/Puffern: nicht werten");
    g.reset();
    g.setLimits(3, kMaxDecodeLevel);
    check(run(g, 8, 3, 0, true, 35, 60).isEmpty(), "60 fps: 3 Bilder je Sekunde sind unter der Schwelle");
    check(run(g, 8, 8, 0, true, 35, 60) == "R", "60 fps: 8 je Sekunde nicht");

    check(loadClass(1920, 1080, 23.976) == "fhd-std", "1080p24 -> fhd-std");
    check(loadClass(3840, 2160, 59.94) == "uhd-hfr", "2160p60 -> uhd-hfr");
    check(loadClass(1280, 720, 50) == "hd-std", "720p50 -> hd-std");
    check(loadClass(7680, 4320, 24) == "8k-std", "4320p -> 8k-std");

    std::printf("%s (%d Fehler)\n", failures ? "NICHT BESTANDEN" : "BESTANDEN", failures);
    return failures ? 1 : 0;
}
