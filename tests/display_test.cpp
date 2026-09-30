// Automatische Bildschirmzuordnung (Wiedergabe / Steuerung) für typische Aufbauten
#include "DisplayManager.h"

#include <QCoreApplication>

#include <cstdio>

namespace {
int g_fail = 0;
QVariantMap screen(const char *id, int w, int h, int hz, bool primary, bool hdr = false)
{
    return {{"id", id}, {"width", w}, {"height", h}, {"refresh", hz}, {"primary", primary}, {"hdrSupported", hdr}};
}
void expect(const char *what, const QVariantList &outs, const char *main, const char *control)
{
    const QString m = DisplayManager::pickMain(outs);
    const QString c = DisplayManager::pickControl(outs, m);
    const bool ok = m == QLatin1String(main) && c == QLatin1String(control);
    std::printf("%s %s: Wiedergabe=%s Steuerung=%s\n", ok ? "OK  " : "FAIL", what, qPrintable(m), qPrintable(c));
    if (!ok)
        ++g_fail;
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    expect("Laptop + 4K-Projektor", {screen("laptop", 1920, 1200, 60, true), screen("beamer", 3840, 2160, 60, false)}, "beamer", "laptop");
    expect("Monitor 144 Hz + 4K-HDR-TV", {screen("monitor", 2560, 1440, 144, true), screen("tv", 3840, 2160, 60, false, true)}, "tv", "monitor");
    expect("drei Bildschirme", {screen("laptop", 1920, 1080, 60, true), screen("monitor", 2560, 1440, 60, false),
                                screen("dci", 4096, 2160, 24, false)}, "dci", "laptop");
    expect("gleiche Auflösung: zweiter Bildschirm spielt", {screen("a", 1920, 1080, 60, true), screen("b", 1920, 1080, 60, false)}, "b", "a");
    expect("gleiche Auflösung, höhere Rate", {screen("a", 1920, 1080, 120, false), screen("b", 1920, 1080, 60, true)}, "a", "b");
    expect("Projektor als Hauptbildschirm eingerichtet", {screen("beamer", 3840, 2160, 60, true), screen("tablet", 1280, 800, 60, false)}, "beamer", "tablet");
    expect("ein Bildschirm", {screen("only", 2560, 1440, 60, true)}, "only", "");
    std::printf("%s (%d Fehler)\n", g_fail ? "FEHLGESCHLAGEN" : "BESTANDEN", g_fail);
    return g_fail ? 1 : 0;
}
