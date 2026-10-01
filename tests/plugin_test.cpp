// Plugin-System prüfen (Beispiel-Plugins aus plugins/examples, gebaut nach <build>/plugins):
//
//   plugin_test <plugin-ordner> [verschlüsseltes-test-dcp]
//
// Ablauf: Plugins finden und laden -> Aktion/Status des nativen Plugins ->
// mpv-Skript des Skript-Plugins läuft -> Wiedergabe über das URL-Schema des
// Plugins (xorfile://) -> DCP-Schlüssel vom Plugin, Spurdatei damit entschlüsselt.
#include "DcpStream.h"
#include "PluginManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QtEndian>

#include <mpv/client.h>

#include <clocale>
#include <cmath>
#include <cstdio>
#include <functional>

namespace {
int g_fail = 0;
void check(bool ok, const char *what)
{
    std::printf("%s %s\n", ok ? "OK  " : "FAIL", what);
    std::fflush(stdout);
    if (!ok)
        ++g_fail;
}

// 1 s Sinus, 48 kHz, 16 Bit mono als WAV
QByteArray makeWav()
{
    const int rate = 48000, n = rate;
    QByteArray pcm(n * 2, Qt::Uninitialized);
    for (int i = 0; i < n; ++i)
        qToLittleEndian<qint16>(qint16(8000 * std::sin(2 * 3.14159265358979 * 440 * i / rate)), pcm.data() + 2 * i);
    QByteArray h;
    auto u32 = [&](quint32 v) { char b[4]; qToLittleEndian(v, b); h.append(b, 4); };
    auto u16 = [&](quint16 v) { char b[2]; qToLittleEndian(v, b); h.append(b, 2); };
    h += "RIFF"; u32(36 + pcm.size()); h += "WAVEfmt "; u32(16); u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16);
    h += "data"; u32(pcm.size());
    return h + pcm;
}
} // namespace

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: plugin_test <plugin-dir> [dcp]\n");
        return 2;
    }
    const QString dcp = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString();
    qputenv("LUMEN_PLUGIN_PATH", argv[1]);
    qputenv("LUMEN_PLUGINS_ENABLE", "demo,osd-clock");
    if (!dcp.isEmpty())
        qputenv("LUMEN_DEMO_DCP_KEYS", QDir(dcp).filePath(QStringLiteral("keys.txt")).toLocal8Bit());

    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("LumenTest")); // eigene Einstellungen
    QCoreApplication::setApplicationName(QStringLiteral("plugin_test"));
    std::setlocale(LC_NUMERIC, "C");

    PluginManager pm;
    pm.loadEnabled();
    // Aktionen/Status kommen über die Ereignisschleife
    QElapsedTimer settle;
    settle.start();
    auto demoReady = [&] {
        for (const QVariant &v : pm.plugins())
            if (v.toMap().value("id") == QLatin1String("demo"))
                return !v.toMap().value("actions").toList().isEmpty() && !v.toMap().value("status").toString().isEmpty();
        return true; // Plugin fehlt -> Prüfungen unten schlagen fehl
    };
    while (!demoReady() && settle.elapsed() < 2000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);

    QVariantMap demo, clock, libs;
    for (const QVariant &v : pm.plugins()) {
        const QVariantMap p = v.toMap();
        std::printf("     %s loaded=%d kinds=%s error=%s status=%s\n", qPrintable(p.value("id").toString()), p.value("loaded").toBool(),
                    qPrintable(p.value("kinds").toStringList().join(QLatin1Char(','))), qPrintable(p.value("error").toString()),
                    qPrintable(p.value("status").toString()));
        (p.value("id") == QLatin1String("demo") ? demo : p.value("id") == QLatin1String("osd-clock") ? clock : libs) = p;
    }
    check(demo.value("loaded").toBool(), "natives Plugin geladen");
    check(clock.value("loaded").toBool(), "Skript-Plugin geladen");
    check(!libs.value("loaded").toBool(), "nicht aktiviertes Plugin bleibt aus");
    const QVariantList actions = demo.value("actions").toList();
    check(actions.size() == 1 && actions.first().toMap().value("id") == QLatin1String("info"), "Aktion registriert");
    check(demo.value("status").toString().contains(QLatin1String("Bereit")), "Status gesetzt");
    const QVariantMap opts = pm.mpvOptions();
    check(opts.value("scripts").toString().contains(QLatin1String("clock.lua")), "mpv-Skript in den Optionen");

    mpv_handle *mpv = mpv_create();
    for (auto [k, v] : std::initializer_list<std::pair<const char *, const char *>>{
             {"vo", "null"}, {"ao", "null"}, {"idle", "yes"}, {"terminal", "no"}, {"config", "no"}})
        mpv_set_option_string(mpv, k, v);
    for (auto it = opts.cbegin(); it != opts.cend(); ++it)
        mpv_set_option_string(mpv, it.key().toUtf8().constData(), it.value().toString().toUtf8().constData());
    const QByteArray logLevel = qgetenv("PLUGIN_TEST_LOG");
    mpv_request_log_messages(mpv, logLevel.isEmpty() ? "warn" : logLevel.constData());
    if (mpv_initialize(mpv) < 0)
        return 1;
    pm.attach(mpv);

    auto waitFor = [&](const std::function<bool()> &cond, int ms) {
        QElapsedTimer t;
        t.start();
        while (!cond() && t.elapsed() < ms) {
            while (mpv_event *ev = mpv_wait_event(mpv, 0.02)) {
                if (ev->event_id == MPV_EVENT_NONE)
                    break;
                if (ev->event_id == MPV_EVENT_LOG_MESSAGE) {
                    auto *m = static_cast<mpv_event_log_message *>(ev->data);
                    std::printf("     mpv[%s] %s", m->prefix, m->text);
                }
            }
            QCoreApplication::processEvents();
        }
        return cond();
    };
    auto prop = [&](const char *name) {
        char *s = mpv_get_property_string(mpv, name);
        const QString v = s ? QString::fromUtf8(s) : QString();
        mpv_free(s);
        return v;
    };

    const bool scriptRan = waitFor([&] { return prop("user-data/osd-clock/loaded").remove(QLatin1Char('"')) == QLatin1String("yes"); }, 5000);
    std::printf("     user-data/osd-clock/loaded=%s\n", qPrintable(prop("user-data/osd-clock/loaded")));
    check(scriptRan, "Lua-Skript des Plugins läuft");

    // Quelle über das URL-Schema des Plugins
    QTemporaryDir tmp;
    QByteArray wav = makeWav();
    for (char &c : wav)
        c = char(c ^ 0x5A);
    const QString xorPath = tmp.filePath(QStringLiteral("tone.xor"));
    {
        QFile f(xorPath);
        check(f.open(QIODevice::WriteOnly) && f.write(wav) == wav.size(), "XOR-Datei geschrieben");
    }
    const QByteArray url = "xorfile://" + QDir::toNativeSeparators(xorPath).toUtf8();
    const char *load[] = {"loadfile", url.constData(), nullptr};
    mpv_command(mpv, load);
    check(waitFor([&] { return prop("duration").toDouble() > 0.9; }, 8000), "xorfile:// geöffnet (Dauer 1 s)");
    std::printf("     duration=%s format=%s samplerate=%s\n", qPrintable(prop("duration")), qPrintable(prop("file-format")),
                qPrintable(prop("audio-params/samplerate")));
    check(prop("audio-params/samplerate") == QLatin1String("48000"), "Ton dekodiert (48 kHz)");

    pm.trigger(QStringLiteral("demo"), QStringLiteral("info"));
    pm.sendEvent(QStringLiteral("file-loaded"), {{"path", QString::fromUtf8(url)}, {"kind", "file"}});
    check(true, "Aktion und Ereignis zugestellt");

    // Disc-Metadaten vom nativen Plugin (set_disc_info)
    QVariantMap discInfo;
    QObject::connect(&pm, &PluginManager::discInfoProvided, [&](const QVariantMap &i) { discInfo = i; });
    pm.sendEvent(QStringLiteral("disc"), {{"device", "D:/"}, {"kind", "dvd"}, {"label", "LUMEN_DEMO_DISC"}});
    check(waitFor([&] { return !discInfo.isEmpty(); }, 10000) && discInfo.value("title") == QLatin1String("Demo disc")
              && discInfo.value("source") == QLatin1String("Demo plugin"),
          "Plugin liefert Disc-Metadaten");

    // DCP-Schlüssel aus dem Plugin
    if (!dcp.isEmpty()) {
        QFile keys(QDir(dcp).filePath(QStringLiteral("keys.txt")));
        check(keys.open(QIODevice::ReadOnly), "keys.txt des Test-DCP");
        const QList<QByteArray> first = keys.readLine().trimmed().split(' ');
        const QString keyId = QString::fromLatin1(first.value(0));
        const QByteArray key = Dcp::providedKey(QStringLiteral("urn:uuid:") + keyId);
        check(key.toHex() == first.value(1), "Plugin liefert den Inhaltsschlüssel");
        check(Dcp::providedKey(QStringLiteral("00000000-0000-0000-0000-000000000000")).isEmpty(), "unbekannte Key-ID -> kein Schlüssel");
        Dcp::g_keyErrors = 0;
        const QByteArray plain = Dcp::readAllTransformed({QDir(dcp).filePath(QStringLiteral("picture.mxf")), key, 0});
        std::printf("     picture.mxf: %lld Bytes, Prüfwertfehler=%d\n", qlonglong(plain.size()), Dcp::g_keyErrors.load());
        check(plain.size() > 0 && Dcp::g_keyErrors == 0, "Bildspur mit Plugin-Schlüssel entschlüsselt");
    }

    pm.detach();
    mpv_terminate_destroy(mpv);
    pm.unloadAll();
    std::printf("%s (%d Fehler)\n", g_fail ? "FEHLGESCHLAGEN" : "BESTANDEN", g_fail);
    return g_fail ? 1 : 0;
}
