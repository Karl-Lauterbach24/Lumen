// DVD-Navigation mit echter mpv-Ausgabe prüfen (Test-DVD: tools/make_test_dvd.py):
//
//   dvd_test <VIDEO_TS-Ordner> <ausgabeordner>
//
// Ablauf: Hauptmenü (Standbild, Button 1 hervorgehoben) -> Pfeil runter (Button 2)
// -> Pfeil hoch, Enter (Titel 1) -> Untertitel an -> Einblendung bei 1–2,5 s ->
// Titelende -> zurück ins Menü. Je Schritt ein Fenster-Screenshot mit Overlay.
#include "DvdNav.h"

#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QTimer>

#include <mpv/client.h>

#include <clocale>
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
} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    std::setlocale(LC_NUMERIC, "C");
    if (argc < 3) {
        std::fprintf(stderr, "usage: dvd_test <dvd> <outdir>\n");
        return 2;
    }
    const QString dvdPath = QString::fromLocal8Bit(argv[1]);
    const QString out = QString::fromLocal8Bit(argv[2]);
    QDir().mkpath(out);

    const QVariantMap scan = DvdNav::scan(dvdPath);
    check(scan.value("titles").toList().size() == 1, "scan: 1 Titel");
    std::printf("     scan: %s\n", QString::fromUtf8(QJsonDocument::fromVariant(scan).toJson(QJsonDocument::Compact)).toLocal8Bit().constData());

    mpv_handle *mpv = mpv_create();
    for (auto [k, v] : std::initializer_list<std::pair<const char *, const char *>>{
             {"vo", "gpu-next"}, {"ao", "null"}, {"force-window", "yes"}, {"geometry", "720x405"}, {"terminal", "no"},
             {"idle", "yes"}, {"keep-open", "yes"}, {"osc", "no"}, {"osd-level", "0"}})
        mpv_set_option_string(mpv, k, v);
    mpv_request_log_messages(mpv, "warn");
    if (mpv_initialize(mpv) < 0)
        return 1;
    mpv_observe_property(mpv, 1, "osd-dimensions", MPV_FORMAT_NODE);

    DvdNav nav;
    nav.attach(mpv);
    const QString url = nav.prepare(dvdPath, QStringLiteral("menu"));
    const QByteArray u = url.toUtf8();
    const char *load[] = {"loadfile", u.constData(), "replace", "-1",
                          "cache=no,demuxer-readahead-secs=0.4,demuxer-lavf-format=mpeg,demuxer-lavf-probesize=65536,"
                          "demuxer-lavf-analyzeduration=0.4,force-seekable=no,sid=no", nullptr};
    mpv_command(mpv, load);

    // mpv-Ereignisse im Qt-Takt abholen (osd-dimensions -> Overlay-Platzierung)
    QTimer events;
    QObject::connect(&events, &QTimer::timeout, [&] {
        for (;;) {
            mpv_event *ev = mpv_wait_event(mpv, 0);
            if (ev->event_id == MPV_EVENT_NONE)
                break;
            if (ev->event_id == MPV_EVENT_LOG_MESSAGE) {
                auto *m = static_cast<mpv_event_log_message *>(ev->data);
                std::printf("     mpv[%s] %s", m->prefix, m->text);
            } else if (ev->event_id == MPV_EVENT_PROPERTY_CHANGE) {
                auto *p = static_cast<mpv_event_property *>(ev->data);
                if (p->format == MPV_FORMAT_NODE) {
                    auto *n = static_cast<mpv_node *>(p->data);
                    QVariantMap m;
                    if (n->format == MPV_FORMAT_NODE_MAP)
                        for (int i = 0; i < n->u.list->num; ++i)
                            m.insert(QString::fromUtf8(n->u.list->keys[i]),
                                     n->u.list->values[i].format == MPV_FORMAT_INT64 ? QVariant(qlonglong(n->u.list->values[i].u.int64))
                                                                                   : QVariant(n->u.list->values[i].u.double_));
                    nav.setOsdDimensions(m);
                }
            }
        }
    });
    events.start(10);

    auto wait = [&](int ms) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms)
            app.processEvents(QEventLoop::AllEvents, 20);
    };
    auto waitFor = [&](const std::function<bool()> &cond, int ms) {
        QElapsedTimer t;
        t.start();
        while (!cond() && t.elapsed() < ms)
            app.processEvents(QEventLoop::AllEvents, 20);
        return cond();
    };
    auto shot = [&](const char *name) {
        const QByteArray f = QDir(out).filePath(QString::fromLatin1(name)).toUtf8();
        const char *cmd[] = {"screenshot-to-file", f.constData(), "window", nullptr};
        mpv_command(mpv, cmd);
    };

    check(waitFor([&] { return nav.active(); }, 5000), "Sitzung aktiv");
    check(waitFor([&] { return nav.menuVisible(); }, 6000), "Menü mit Buttons sichtbar");
    check(waitFor([&] { return nav.still(); }, 4000), "Standbild-Menü");
    wait(300);
    shot("1_menu_button1.png");
    check(nav.key(QStringLiteral("down")), "Pfeil runter");
    wait(600);
    shot("2_menu_button2.png");
    nav.key(QStringLiteral("up"));
    wait(300);
    check(nav.key(QStringLiteral("enter")), "Enter auf Button 1");
    check(waitFor([&] { return nav.title() == 1 && !nav.menuVisible(); }, 6000), "Titel 1 läuft");
    check(nav.chapters().size() == 2, "2 Kapitel");
    check(nav.audioStreams().size() == 2, "2 Tonspuren aus der IFO");
    check(nav.subtitleStreams().size() == 1, "1 Untertitelspur");
    for (const auto &a : nav.audioStreams())
        std::printf("     Ton: %s\n", a.toMap().value("label").toString().toLocal8Bit().constData());
    nav.selectSubtitle(0);
    double pos = 0;
    waitFor([&] { mpv_get_property(mpv, "time-pos", MPV_FORMAT_DOUBLE, &pos); return pos >= 1.9; }, 8000);
    shot("3_title_subtitle.png");
    check(pos >= 1.9, "Wiedergabe läuft (time-pos >= 1,9 s)");
    check(waitFor([&] { return nav.menuVisible(); }, 12000), "Nach Titelende zurück im Menü");
    wait(800);
    shot("4_menu_again.png");
    check(nav.key(QStringLiteral("down")) && nav.key(QStringLiteral("enter")), "Button 2 (Kapitel 2)");
    check(waitFor([&] { return nav.title() == 1 && nav.chapter() == 1; }, 6000), "Titel 1, Kapitel 2");

    events.stop();
    mpv_terminate_destroy(mpv);
    nav.detach();
    std::printf("%s (%d Fehler)\n", g_fail ? "FEHLGESCHLAGEN" : "BESTANDEN", g_fail);
    return g_fail ? 1 : 0;
}
