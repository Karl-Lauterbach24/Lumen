// Übertragung an Empfänger im Netz, mit echter mpv-Wiedergabe und Testgeräten:
//
//   cast_test <python> <tools/mock_cast_devices.py> <openssl> <sync.mp4>
//
// sync.mp4: jede Sekunde ein weißes Bild und gleichzeitig ein Ton, dazu dauerhaft eine weiße
// Marke oben links (siehe CI-Schritt).
// Geprüft wird
//   - der Sendestrom selbst (über HTTP gelesen und dekodiert): H.264 + AAC, Bildrate,
//     Ton und Bild gleichzeitig (Blitz gegen Ton)
//   - Geräteerkennung aus SSDP- und mDNS-Antworten
//   - DLNA, Chromecast (CASTV2/TLS), AirPlay, Lumen-TV-App gegen tools/mock_cast_devices.py
//   - Fehlerfall: Gerät lehnt ab -> Meldung, Player läuft normal weiter
#include "CastManager.h"
#include "CastOutput.h"
#include "CastRenderer.h"

#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSettings>
#include <QtEndian>

#include <mpv/client.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include <atomic>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <functional>
#include <thread>
#include <vector>

namespace {

int g_fail = 0;
void check(bool ok, const QString &what)
{
    std::printf("%s %s\n", ok ? "OK  " : "FAIL", qPrintable(what));
    std::fflush(stdout);
    if (!ok)
        ++g_fail;
}
bool waitFor(const std::function<bool()> &cond, int ms)
{
    QElapsedTimer t;
    t.start();
    while (!cond() && t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return cond();
}

// Einfacher Player: eine mpv-Instanz, die die Testdatei in Schleife spielt – normal (ohne
// Ausgabe) oder in den Encoder
class TestPlayer : public CastOutput
{
public:
    explicit TestPlayer(const QString &file)
        : m_file(file)
    {
        create(nullptr, {});
    }
    ~TestPlayer() override { destroy(); }

    void setCastOutput(CastEncoder *encoder, const QString &pcmPath) override
    {
        destroy();
        create(encoder, pcmPath);
        ++switches;
    }
    QString castTitle() const override { return QStringLiteral("Lumen Test"); }
    bool casting() const { return m_renderer != nullptr; }
    double position() const
    {
        double pos = -1;
        mpv_get_property(m_mpv, "time-pos", MPV_FORMAT_DOUBLE, &pos);
        return pos;
    }
    int switches = 0;

private:
    void create(CastEncoder *encoder, const QString &pcmPath)
    {
        m_mpv = mpv_create();
        QVariantMap opts{{"terminal", "no"}, {"config", "no"}, {"idle", "yes"}, {"loop-file", "inf"}, {"hwdec", "no"}};
        if (encoder) {
            const QVariantMap cast = castMpvOptions(pcmPath);
            for (auto it = cast.cbegin(); it != cast.cend(); ++it)
                opts.insert(it.key(), it.value());
        } else {
            opts.insert("vo", "null");
            opts.insert("ao", "null");
        }
        // Entwickler/CI: LUMEN_MPV_LOGFILE=<datei> schreibt das mpv-Protokoll der Übertragung mit
        if (encoder && qEnvironmentVariableIsSet("LUMEN_MPV_LOGFILE")) {
            // je Übertragung eine Datei: <datei>.1 ist die erste (deren Strom unten ausgewertet wird)
            opts.insert("log-file", QStringLiteral("%1.%2").arg(qEnvironmentVariable("LUMEN_MPV_LOGFILE")).arg(++m_session));
            opts.insert("msg-level", "all=v");
        }
        for (auto it = opts.cbegin(); it != opts.cend(); ++it)
            mpv_set_option_string(m_mpv, it.key().toUtf8().constData(), it.value().toString().toUtf8().constData());
        mpv_request_log_messages(m_mpv, qEnvironmentVariableIsSet("LUMEN_MPV_LOG") ? qgetenv("LUMEN_MPV_LOG").constData() : "no");
        mpv_initialize(m_mpv);
        if (encoder)
            m_renderer = new CastRenderer(m_mpv, encoder);
        const QByteArray f = m_file.toUtf8();
        const char *cmd[] = {"loadfile", f.constData(), nullptr};
        mpv_command(m_mpv, cmd);
    }
    void destroy()
    {
        if (m_renderer)
            m_renderer->releaseRenderContext();
        if (m_mpv)
            mpv_terminate_destroy(m_mpv);
        delete m_renderer;
        m_renderer = nullptr;
        m_mpv = nullptr;
    }

    QString m_file;
    int m_session = 0;
    mpv_handle *m_mpv = nullptr;
    CastRenderer *m_renderer = nullptr;
};

// Ergebnis der Stromanalyse
struct Analysis
{
    bool opened = false;
    QString video, audio;
    int width = 0, height = 0, sampleRate = 0, channels = 0;
    int frames = 0;
    double seconds = 0;
    std::vector<double> flashes, beeps; // Beginn in Sekunden (Zeitstempel des Stroms)
    int markTop = 0, markBottom = 0;    // dunkle Bilder mit heller Ecke oben links bzw. unten links
    QString error;
};

// Strom über HTTP lesen und rund `seconds` Sekunden dekodieren (eigener Thread: der Server
// läuft in der Ereignisschleife des Hauptthreads)
Analysis analyze(const QString &url, double seconds)
{
    Analysis a;
    AVFormatContext *fmt = nullptr;
    AVDictionary *opts = nullptr;
    av_dict_set(&opts, "rw_timeout", "15000000", 0);
    int err = avformat_open_input(&fmt, url.toUtf8().constData(), nullptr, &opts);
    av_dict_free(&opts);
    if (err < 0) {
        char buf[128];
        av_strerror(err, buf, sizeof buf);
        a.error = QString::fromUtf8(buf);
        return a;
    }
    a.opened = true;
    avformat_find_stream_info(fmt, nullptr);
    int vi = -1, ai = -1;
    AVCodecContext *vdec = nullptr, *adec = nullptr;
    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        const AVCodecParameters *par = fmt->streams[i]->codecpar;
        const AVCodec *codec = avcodec_find_decoder(par->codec_id);
        if (!codec)
            continue;
        AVCodecContext *ctx = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(ctx, par);
        if (avcodec_open2(ctx, codec, nullptr) < 0) {
            avcodec_free_context(&ctx);
            continue;
        }
        if (par->codec_type == AVMEDIA_TYPE_VIDEO && vi < 0) {
            vi = int(i);
            vdec = ctx;
            a.video = QString::fromLatin1(codec->name);
            a.width = par->width;
            a.height = par->height;
        } else if (par->codec_type == AVMEDIA_TYPE_AUDIO && ai < 0) {
            ai = int(i);
            adec = ctx;
            a.audio = QString::fromLatin1(codec->name);
            a.sampleRate = par->sample_rate;
            a.channels = par->ch_layout.nb_channels;
        } else {
            avcodec_free_context(&ctx);
        }
    }
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    double first = -1, last = -1;
    bool wasBright = false, wasLoud = false;
    while (av_read_frame(fmt, pkt) >= 0) {
        AVCodecContext *dec = pkt->stream_index == vi ? vdec : pkt->stream_index == ai ? adec : nullptr;
        if (dec && avcodec_send_packet(dec, pkt) >= 0) {
            const double tb = av_q2d(fmt->streams[pkt->stream_index]->time_base);
            while (avcodec_receive_frame(dec, frame) >= 0) {
                const double t = frame->best_effort_timestamp * tb;
                if (dec == vdec) {
                    if (first < 0)
                        first = t;
                    last = t;
                    ++a.frames;
                    // mittlere Helligkeit der Bildmitte
                    qint64 sum = 0;
                    int n = 0;
                    for (int y = frame->height / 4; y < frame->height * 3 / 4; y += 8)
                        for (int x = frame->width / 4; x < frame->width * 3 / 4; x += 8, ++n)
                            sum += frame->data[0][y * frame->linesize[0] + x];
                    const bool bright = n && sum / n > 120;
                    if (!bright) {
                        // Ausrichtung: die Marke der Quelle sitzt oben links
                        auto corner = [&](int y0) {
                            qint64 c = 0;
                            int m = 0;
                            for (int y = y0; y < y0 + frame->height / 16; y += 2)
                                for (int x = frame->width / 64; x < frame->width / 16; x += 2, ++m)
                                    c += frame->data[0][y * frame->linesize[0] + x];
                            return m ? int(c / m) : 0;
                        };
                        a.markTop += corner(frame->height / 64) > 150;
                        a.markBottom += corner(frame->height - frame->height / 16 - frame->height / 64) > 150;
                    }
                    if (bright && !wasBright)
                        a.flashes.push_back(t);
                    wasBright = bright;
                } else if (frame->format == AV_SAMPLE_FMT_FLTP) {
                    // Beginn des Tons auf wenige Millisekunden genau: in Blöcken zu 96 Samples
                    const auto *s = reinterpret_cast<const float *>(frame->data[0]);
                    for (int i = 0; i + 96 <= frame->nb_samples; i += 96) {
                        double e = 0;
                        for (int k = 0; k < 96; ++k)
                            e += double(s[i + k]) * s[i + k];
                        const bool loud = std::sqrt(e / 96) > 0.05;
                        if (loud && !wasLoud)
                            a.beeps.push_back(t + double(i) / frame->sample_rate);
                        wasLoud = loud;
                    }
                }
            }
        }
        av_packet_unref(pkt);
        if (first >= 0 && last - first >= seconds)
            break;
    }
    a.seconds = first >= 0 ? last - first : 0;
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&vdec);
    avcodec_free_context(&adec);
    avformat_close_input(&fmt);
    return a;
}

Analysis analyzeWhileServing(const QString &url, double seconds)
{
    Analysis result;
    std::atomic<bool> done{false};
    std::thread worker([&] {
        result = analyze(url, seconds);
        done = true;
    });
    waitFor([&] { return done.load(); }, 90000);
    worker.join();
    return result;
}

// --- mDNS-Antwort, wie ein Chromecast und ein AirPlay-Gerät sie senden (mit Namenskompression) ---
QByteArray dnsLabels(const QString &name)
{
    QByteArray out;
    for (const QString &label : name.split(QLatin1Char('.'))) {
        const QByteArray l = label.toUtf8();
        out += char(l.size());
        out += l;
    }
    return out + '\0';
}
QByteArray be16(quint16 v)
{
    QByteArray b(2, '\0');
    qToBigEndian(v, b.data());
    return b;
}
QByteArray record(const QByteArray &name, quint16 type, const QByteArray &rdata)
{
    return name + be16(type) + be16(1) + QByteArray(4, '\0') + be16(quint16(rdata.size())) + rdata;
}
QByteArray txt(const QStringList &entries)
{
    QByteArray out;
    for (const QString &e : entries) {
        const QByteArray b = e.toUtf8();
        out += char(b.size());
        out += b;
    }
    return out;
}
QByteArray pointer(int offset)
{
    return QByteArray(1, char(0xC0 | (offset >> 8))) + char(offset & 0xFF);
}
QByteArray mdnsResponse()
{
    QByteArray d = QByteArray::fromHex("000084000000000800000000"); // Antwort, 8 Datensätze
    // Dienstnamen stehen am Anfang; spätere Namen verweisen per Zeiger (0xC0xx) darauf
    const int castService = d.size();
    d += record(dnsLabels("_googlecast._tcp.local"), 12, dnsLabels("Chromecast-abc").chopped(1) + pointer(castService));
    const QByteArray castInstance = dnsLabels("Chromecast-abc").chopped(1) + pointer(castService);
    d += record(castInstance, 33, be16(0) + be16(0) + be16(8009) + dnsLabels("cast-host.local"));
    d += record(castInstance, 16, txt({"id=abcdef0123", "fn=Wohnzimmer TV", "md=Chromecast Ultra"}));
    d += record(dnsLabels("cast-host.local"), 1, QByteArray::fromHex("c0a80132")); // 192.168.1.50
    const int airService = d.size();
    d += record(dnsLabels("_airplay._tcp.local"), 12, dnsLabels("Schlafzimmer").chopped(1) + pointer(airService));
    const QByteArray airInstance = dnsLabels("Schlafzimmer").chopped(1) + pointer(airService);
    d += record(airInstance, 33, be16(0) + be16(0) + be16(7000) + dnsLabels("appletv.local"));
    d += record(airInstance, 16, txt({"deviceid=AA:BB:CC:DD:EE:FF", "model=AppleTV6,2", "flags=0x244"}));
    d += record(dnsLabels("appletv.local"), 1, QByteArray::fromHex("c0a80133"));
    return d;
}

QVariantMap deviceByType(const QVariantList &devices, const QString &type, const QString &name = {})
{
    for (const QVariant &v : devices) {
        const QVariantMap d = v.toMap();
        if (d.value("type") == type && (name.isEmpty() || d.value("name") == name))
            return d;
    }
    return {};
}

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("LumenTest"));
    QCoreApplication::setApplicationName(QStringLiteral("cast_test"));
    std::setlocale(LC_NUMERIC, "C");
    if (argc < 5) {
        std::fprintf(stderr, "cast_test <python> <mock_cast_devices.py> <openssl> <sync.mp4>\n");
        return 2;
    }
    QSettings().clear();
    const QString python = QString::fromLocal8Bit(argv[1]);
    const QString script = QString::fromLocal8Bit(argv[2]);
    const QString openssl = QString::fromLocal8Bit(argv[3]);
    const QString media = QString::fromLocal8Bit(argv[4]);

    // ---- Testgeräte ----
    const QString dir = QDir::tempPath() + QStringLiteral("/lumen-cast-test");
    QDir().mkpath(dir);
    QProcess::execute(openssl, {"req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", dir + "/key.pem", "-out", dir + "/cert.pem",
                                "-days", "2", "-subj", "/CN=lumen-test"});
    QProcess mock;
    mock.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    mock.start(python, {script, dir + "/cert.pem", dir + "/key.pem"});
    QList<QJsonObject> events;
    QJsonObject ports;
    QObject::connect(&mock, &QProcess::readyReadStandardOutput, [&] {
        while (mock.canReadLine()) {
            const QByteArray line = mock.readLine().trimmed();
            const QJsonObject o = QJsonDocument::fromJson(line).object();
            if (o.contains("ready"))
                ports = o.value("ready").toObject();
            else if (!o.isEmpty()) {
                events << o;
                std::printf("     mock: %s\n", line.constData());
                std::fflush(stdout);
            }
        }
    });
    auto event = [&](const QString &device, const QString &name) -> QJsonObject {
        for (const QJsonObject &e : std::as_const(events))
            if (e.value("device") == device && e.value("event") == name)
                return e;
        return {};
    };
    auto waitEvent = [&](const QString &device, const QString &name, int ms = 20000) {
        waitFor([&] { return !event(device, name).isEmpty(); }, ms);
        return event(device, name);
    };
    check(waitFor([&] { return !ports.isEmpty(); }, 60000), "Testgeräte gestartet");
    if (ports.isEmpty())
        return 1;

    TestPlayer player(media);
    CastManager cast(&player);
    cast.setHeight(720);
    cast.setBitrate(4000);
    check(cast.available(), QStringLiteral("H.264-Encoder vorhanden: %1").arg(CastEncoder::videoEncoderName()));
    if (!cast.available())
        return 1;
    QStringList keys;
    QObject::connect(&cast, &CastManager::remoteKey, [&](const QString &k) { keys << k; });

    // ---- Geräteerkennung ----
    cast.discovery()->handleMdns(mdnsResponse());
    cast.discovery()->handleSsdp(QStringLiteral("HTTP/1.1 200 OK\r\nCACHE-CONTROL: max-age=1800\r\nST: urn:schemas-upnp-org:device:MediaRenderer:1\r\n"
                                                "USN: uuid:1::urn:schemas-upnp-org:device:MediaRenderer:1\r\nLocation: http://127.0.0.1:%1/desc.xml\r\n\r\n")
                                     .arg(ports.value("dlna").toInt()).toUtf8());
    const QVariantMap cc = deviceByType(cast.devices(), "chromecast");
    check(cc.value("name") == "Wohnzimmer TV" && cc.value("address") == "192.168.1.50" && cc.value("model") == "Chromecast Ultra",
          "mDNS: Chromecast mit Name, Modell und Adresse erkannt");
    const QVariantMap ap = deviceByType(cast.devices(), "airplay");
    check(ap.value("name") == "Schlafzimmer" && ap.value("address") == "192.168.1.51" && ap.value("needsPairing").toBool(),
          "mDNS: AirPlay-Gerät erkannt, Kopplungspflicht aus den Statusbits gelesen");
    check(waitFor([&] { return !deviceByType(cast.devices(), "dlna").isEmpty(); }, 10000), "SSDP: DLNA-Renderer über seine Beschreibung erkannt");
    const QVariantMap dlna = deviceByType(cast.devices(), "dlna");
    check(dlna.value("name") == "Test TV & Renderer" && dlna.value("model") == "MockRenderer", "DLNA: Name und Modell aus der Beschreibung");
    cast.addDevice("chromecast", QStringLiteral("127.0.0.1:%1").arg(ports.value("chromecast").toInt()));
    cast.addDevice("airplay", QStringLiteral("127.0.0.1:%1").arg(ports.value("airplay").toInt()));
    cast.addDevice("airplay", QStringLiteral("127.0.0.1:%1").arg(ports.value("locked").toInt()));
    check(!cast.addDevice("chromecast", "kein gültiger eintrag"), "Von Hand: ungültige Adresse abgelehnt");
    const QString idChromecast = QStringLiteral("chromecast:127.0.0.1:%1").arg(ports.value("chromecast").toInt());
    const QString idAirplay = QStringLiteral("airplay:127.0.0.1:%1").arg(ports.value("airplay").toInt());
    const QString idLocked = QStringLiteral("airplay:127.0.0.1:%1").arg(ports.value("locked").toInt());

    auto playing = [&] { return cast.state() == QLatin1String("playing"); };
    auto idle = [&] { return cast.state() == QLatin1String("idle"); };

    // ---- DLNA + Analyse des Stroms ----
    cast.start(dlna.value("id").toString());
    check(cast.active() && player.casting(), "DLNA: Player gibt in den Encoder aus");
    {
        const bool ok = waitFor(playing, 40000);
        check(ok, "DLNA: Wiedergabe läuft (" + cast.state() + " " + cast.message() + ")");
    }
    check(waitEvent("dlna", "seturi").value("ok").toBool(), "DLNA: SetAVTransportURI mit DLNA-Angaben");
    const QJsonObject dlnaPlay = waitEvent("dlna", "play");
    check(dlnaPlay.value("ok").toBool(), "DLNA: Gerät liest den fortlaufenden MPEG-TS-Strom (" + dlnaPlay.value("detail").toString() + ")");
    check(dlnaPlay.value("url").toString().contains("/stream/" + cast.server()->token() + "/live.ts"), "DLNA: Adresse enthält das Sitzungs-Token");

    if (playing()) {
        const Analysis a = analyzeWhileServing(cast.server()->hlsUrl(QHostAddress::LocalHost), 8.0);
        check(a.opened, "Strom (HLS) lässt sich öffnen " + a.error);
        const QStringList want = qEnvironmentVariable("LUMEN_CAST_SIZE", QStringLiteral("1280x720")).split(QLatin1Char('x'));
        check(a.video == "h264" && a.width == want.value(0).toInt() && a.height == want.value(1).toInt(),
              QStringLiteral("Bild: %1 %2x%3").arg(a.video).arg(a.width).arg(a.height));
        check(a.audio == "aac" && a.sampleRate == 48000 && a.channels == 2,
              QStringLiteral("Ton: %1 %2 Hz %3 Kanäle").arg(a.audio).arg(a.sampleRate).arg(a.channels));
        // OpenGL auf der CPU (CI ohne Grafikkarte) schafft die Bildrate der Quelle nicht und rendert
        // ungleichmäßig. Dort wird geprüft, dass der Strom stimmt (Bild, Ausrichtung, Ton, Abgleich im
        // Mittel) – Bildrate und Gleichmäßigkeit prüfen nur Läufe mit Grafikkarte.
        const bool softwareGl = qEnvironmentVariableIsSet("LUMEN_CAST_TEST_SOFTWARE_GL");
        check(a.markTop > (softwareGl ? 10 : a.frames / 2) && a.markBottom == 0,
              QStringLiteral("Bild steht richtig herum (Marke oben links in %1 Bildern, unten in %2)").arg(a.markTop).arg(a.markBottom));
        const double fps = a.seconds > 0 ? a.frames / a.seconds : 0;
        const QString rate = QStringLiteral("Bildrate %1 fps über %2 s").arg(fps, 0, 'f', 1).arg(a.seconds, 0, 'f', 1);
        if (softwareGl)
            std::printf("INFO %s (Software-OpenGL, nicht gewertet)\n", qPrintable(rate));
        else
            check(fps > 24 && fps < 33, rate);
        // Blitz und Ton gehören zusammen: zu jedem Blitz den nächsten Tonbeginn suchen
        std::vector<double> offsets;
        for (double f : a.flashes) {
            double best = 1e9;
            for (double b : a.beeps)
                if (std::abs(b - f) < std::abs(best))
                    best = b - f;
            if (std::abs(best) < 0.5)
                offsets.push_back(best);
        }
        QStringList ft, bt;
        for (double f : a.flashes) ft << QString::number(f, 'f', 3);
        for (double b : a.beeps) bt << QString::number(b, 'f', 3);
        std::printf("     Blitze: %s\n     Toene:  %s\n", qPrintable(ft.join(' ')), qPrintable(bt.join(' ')));
        const size_t wantFlashes = softwareGl ? 3 : 5;
        check(a.flashes.size() >= wantFlashes && a.beeps.size() >= 5,
              QStringLiteral("%1 Blitze und %2 Töne im Strom").arg(a.flashes.size()).arg(a.beeps.size()));
        double worst = 0, mean = 0;
        for (double o : offsets) {
            worst = std::max(worst, std::abs(o));
            mean += o / double(offsets.size());
        }
        const QString sync = QStringLiteral("Ton zu Bild: im Mittel %1 ms, größte Abweichung %2 ms (%3 Paare)")
                                 .arg(mean * 1000, 0, 'f', 0).arg(worst * 1000, 0, 'f', 0).arg(offsets.size());
        // die Abstände der Blitze: Echtzeit (1 s), nicht schneller oder langsamer
        bool realtime = a.flashes.size() >= 3;
        for (size_t i = 1; i < a.flashes.size(); ++i)
            realtime &= std::abs(a.flashes[i] - a.flashes[i - 1] - 1.0) < 0.12;
        if (softwareGl) {
            // einzelne Bilder lassen sich hier nicht auf Zehntelsekunden festlegen: der Mittelwert zählt
            check(offsets.size() >= 3 && std::abs(mean) < 0.15, sync + QStringLiteral(" – Software-OpenGL: nur der Mittelwert zählt"));
            std::printf("INFO Bilder im Sekundenabstand: %s (Software-OpenGL, nicht gewertet)\n", realtime ? "ja" : "nein");
        } else {
            check(offsets.size() >= 5 && worst < 0.09, sync);
            check(realtime, "Strom läuft in Echtzeit (Blitze im Sekundenabstand)");
        }
    }
    const QString firstToken = cast.server()->token();
    cast.stop();
    check(idle() && !player.casting(), "DLNA: beendet, Player gibt wieder normal aus");
    check(waitEvent("dlna", "stop").value("ok").toBool(), "DLNA: Stop an das Gerät");
    check(waitFor([&] { return player.position() >= 0; }, 10000), "Player spielt nach der Übertragung weiter");

    // ---- Chromecast ----
    events.clear();
    cast.start(idChromecast);
    {
        const bool ok = waitFor(playing, 40000);
        check(ok, "Chromecast: Wiedergabe läuft (" + cast.state() + " " + cast.message() + ")");
    }
    check(waitEvent("chromecast", "launch").value("ok").toBool(), "Chromecast: Medienempfänger gestartet (CONNECT + LAUNCH)");
    check(waitEvent("chromecast", "play").value("ok").toBool(), "Chromecast: LOAD mit HLS-Adresse, Gerät liest Wiedergabeliste und Segment");
    check(cast.server()->token() != firstToken && !cast.server()->token().isEmpty(), "Neue Sitzung, neues Token");
    cast.stop();
    check(waitEvent("chromecast", "stop").value("ok").toBool(), "Chromecast: STOP mit Sitzungskennung");

    // ---- AirPlay ----
    events.clear();
    cast.start(idAirplay);
    {
        const bool ok = waitFor(playing, 40000);
        check(ok, "AirPlay: Wiedergabe läuft (" + cast.state() + " " + cast.message() + ")");
    }
    const QJsonObject airPlay = waitEvent("airplay", "play");
    check(airPlay.value("ok").toBool() && airPlay.value("session").toBool(), "AirPlay: /play mit Adresse und Sitzungskennung, Gerät liest den Strom");
    cast.stop();
    check(waitEvent("airplay", "stop").value("ok").toBool(), "AirPlay: /stop");

    // ---- Gerät lehnt ab ----
    events.clear();
    const int before = player.switches;
    cast.start(idLocked);
    check(waitFor([&] { return cast.state() == QLatin1String("error"); }, 40000), "Gerät lehnt ab: Fehlerzustand");
    check(!cast.message().isEmpty() && !cast.active(), "Gerät lehnt ab: Meldung „" + cast.message() + "“");
    check(!player.casting() && player.switches == before + 2, "Gerät lehnt ab: Player gibt wieder normal aus");

    // ---- Lumen-TV-App ----
    events.clear();
    cast.openDialog();
    check(cast.listening(), "Server für TV-Apps erreichbar");
    mock.write(QStringLiteral("tv http://127.0.0.1:%1\n").arg(cast.server()->port()).toUtf8());
    check(waitEvent("tv", "hello").value("ok").toBool(), "TV-App: Anmeldung");
    check(waitEvent("tv", "page").value("ok").toBool(), "TV-App: Empfänger-Seite wird ausgeliefert");
    check(waitEvent("tv", "key-before-play").value("ok").toBool(), "TV-App: Tasten werden vor der Auswahl nicht angenommen");
    check(waitFor([&] { return !deviceByType(cast.devices(), "tv", "Mock TV").isEmpty(); }, 10000), "TV-App erscheint mit ihrem Namen als Empfänger");
    const QVariantMap tv = deviceByType(cast.devices(), "tv", "Mock TV");
    check(keys.isEmpty(), "Keine Taste vor der Auswahl weitergereicht");
    cast.start(tv.value("id").toString());
    {
        const bool ok = waitFor(playing, 40000);
        check(ok, "TV-App: Wiedergabe läuft (" + cast.state() + " " + cast.message() + ")");
    }
    check(waitEvent("tv", "play").value("ok").toBool(), "TV-App: liest HLS und fortlaufenden Strom");
    check(waitEvent("tv", "key").value("ok").toBool() && waitFor([&] { return keys.contains("enter"); }, 5000),
          "TV-App: Taste der Fernbedienung kommt bei Lumen an");
    cast.stop();
    check(waitEvent("tv", "stop").value("ok").toBool(), "TV-App: Stopp");
    cast.closeDialog();
    check(!cast.listening(), "Server geschlossen, wenn nichts mehr läuft");

    mock.write("quit\n");
    mock.waitForFinished(5000);
    mock.kill();
    cast.shutdown();
    std::printf("%s\n", g_fail ? "FEHLGESCHLAGEN" : "BESTANDEN");
    return g_fail ? 1 : 0;
}
