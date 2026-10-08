#include "DiscScanner.h"
#include "Tr.h"
#include "DriveHelpers.h"
#include "DvdNav.h"
#include "MpvController.h"
#include "OpticalMedia.h"
#include "PathUtil.h"

#include <QCoreApplication>
#include <QPointer>
#include <QThreadPool>
#include <QUrl>

#ifdef LUMEN_HAVE_BLURAY
#include <libbluray/bluray.h>
#endif

namespace {

#ifdef LUMEN_HAVE_BLURAY
QString videoCodec(uint8_t t)
{
    switch (t) {
    case 0x01: case 0x02: return QStringLiteral("MPEG-2");
    case 0x1b: return QStringLiteral("AVC");
    case 0x20: return QStringLiteral("MVC 3D");
    case 0x24: return QStringLiteral("HEVC");
    case 0xea: return QStringLiteral("VC-1");
    default: return QStringLiteral("?");
    }
}

QString videoFormat(uint8_t f)
{
    switch (f) {
    case 1: return QStringLiteral("480i");
    case 2: return QStringLiteral("576i");
    case 3: return QStringLiteral("480p");
    case 4: return QStringLiteral("1080i");
    case 5: return QStringLiteral("720p");
    case 6: return QStringLiteral("1080p");
    case 7: return QStringLiteral("576p");
    case 8: return QStringLiteral("2160p");
    default: return QString();
    }
}

QString audioCodec(uint8_t t)
{
    switch (t) {
    case 0x80: return QStringLiteral("LPCM");
    case 0x81: return QStringLiteral("AC-3");
    case 0x82: return QStringLiteral("DTS");
    case 0x83: return QStringLiteral("TrueHD");
    case 0x84: case 0xa1: return QStringLiteral("E-AC-3");
    case 0x85: return QStringLiteral("DTS-HD HR");
    case 0x86: return QStringLiteral("DTS-HD MA");
    case 0xa2: return QStringLiteral("DTS-HD");
    default: return QStringLiteral("?");
    }
}
#endif

} // namespace

DiscScanner::DiscScanner(QObject *parent)
    : QObject(parent)
{
    // Eine Disc ist in Sekunden gelesen, auch eine mit Java-Menü und über MakeMKV in unter einer
    // Minute. Nach zwei Minuten kommt nichts mehr.
    m_watch.setSingleShot(true);
    m_watch.setInterval(qEnvironmentVariableIntValue("LUMEN_DISC_TIMEOUT") > 0 ? qEnvironmentVariableIntValue("LUMEN_DISC_TIMEOUT") * 1000 : 120000);
    connect(&m_watch, &QTimer::timeout, this, &DiscScanner::giveUp);
}

// Das Öffnen der Disc kehrt nicht zurück. Steht dabei der Hilfsprozess der AACS-Bibliothek im
// Laufwerk, hält er es auch für das System und jedes andere Programm besetzt: ihn beenden, dann
// ist es (meist) wieder frei. Der Aufruf der Bibliothek in diesem Prozess endet trotzdem nicht –
// jedes weitere Öffnen wartet hinter ihm. Das sagt die Oberfläche und bietet den Neustart an.
void DiscScanner::giveUp()
{
    if (!m_busy)
        return;
    const int ended = DriveHelpers::endOwn(m_helpers);
    qWarning("Lumen: Disc nach %d s nicht gelesen, %d Hilfsprozess(e) beendet", m_watch.interval() / 1000, ended);
    ++m_generation; // ein Ergebnis, das doch noch käme, gilt nicht mehr
    m_busy = false;
    m_stuck = true;
    m_info = QVariantMap{{"device", m_device},
                         {"kind", QStringLiteral("bluray")},
                         {"error", LTR("Das Laufwerk antwortet nicht mehr. Lumen muss neu gestartet werden; hilft das nicht, das Laufwerk aus- und wieder einschalten.")}};
    emit busyChanged();
    emit infoChanged();
}

DiscScanner::~DiscScanner()
{
    ++m_generation;
}

bool DiscScanner::available() const
{
    return true; // Blu-ray über libbluray, übrige Formate über libdvdnav/libcdio/eigene Parser
}

void DiscScanner::clear()
{
    ++m_generation;
    m_info.clear();
    emit infoChanged();
}

void DiscScanner::applyMetadata(const QVariantMap &meta)
{
    if (m_busy || m_info.value("device").toString().isEmpty())
        return;
    const QString device = meta.value("device").toString();
    if (!device.isEmpty() && device != m_info.value("device").toString())
        return; // gehört zu einer anderen (früheren) Disc

    QVariantMap clean;
    for (const char *key : {"title", "artist", "year", "source", "overview"}) {
        const QString v = meta.value(QLatin1String(key)).toString().simplified().left(400);
        if (!v.isEmpty())
            clean.insert(QLatin1String(key), v);
    }
    const QUrl cover(meta.value("cover").toString());
    if (cover.isValid() && (cover.scheme() == QLatin1String("https") || cover.scheme() == QLatin1String("http")))
        clean.insert(QStringLiteral("cover"), cover.toString());
    const QVariantList tracks = meta.value("tracks").toList();
    if (clean.isEmpty() && tracks.isEmpty())
        return;

    const QString title = clean.value("title").toString(), artist = clean.value("artist").toString();
    if (!title.isEmpty()) {
        if (!m_info.contains("discLabel"))
            m_info["discLabel"] = m_info.value("discName");
        m_info["discName"] = artist.isEmpty() ? title : artist + QStringLiteral(" – ") + title;
    }
    // Audio-CD: Tracknamen in die Titelliste
    if (m_info.value("kind") == QLatin1String("cdda") && !tracks.isEmpty()) {
        QVariantList titles = m_info.value("titles").toList();
        for (int i = 0; i < titles.size() && i < tracks.size(); ++i) {
            const QVariantMap track = tracks[i].toMap();
            const QString name = track.value("title").toString().simplified().left(300);
            if (name.isEmpty())
                continue;
            QVariantMap t = titles[i].toMap();
            t["label"] = QStringLiteral("%1. %2").arg(t.value("title", i + 1).toInt()).arg(name);
            t["name"] = name;
            const QString trackArtist = track.value("artist").toString().simplified().left(300);
            if (!trackArtist.isEmpty() && trackArtist != artist)
                t["artist"] = trackArtist;
            titles[i] = t;
        }
        m_info["titles"] = titles;
    }
    m_info["meta"] = clean;
    emit infoChanged();
}

void DiscScanner::scan(const QString &device)
{
    if (!available() || device.isEmpty())
        return;
    if (m_stuck) // hinter dem Aufruf, der nicht endet, wartete auch dieser
        return;
    const int gen = ++m_generation;
    m_busy = true;
    emit busyChanged();
    // Was ein abgestürzter Lauf am Laufwerk zurückgelassen hat, stört das Lesen
    DriveHelpers::endOrphans();
    m_device = device;
    m_helpers = DriveHelpers::own();
    m_watch.start();
    QPointer<DiscScanner> self(this);
    QThreadPool::globalInstance()->start([self, device, gen] {
        const QVariantMap info = scanBlocking(device);
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, info, gen] {
            if (!self || gen != self->m_generation)
                return;
            self->m_watch.stop();
            self->m_info = info;
            self->m_busy = false;
            emit self->busyChanged();
            emit self->infoChanged();
            emit self->scanned();
        }, Qt::QueuedConnection);
    });
}

QVariantMap DiscScanner::scanBlocking(const QString &device)
{
    QVariantMap m;
    m["device"] = device;
    const QString kind = MpvController::detectKind(device);
    if (kind == QLatin1String("dvd"))
        return DvdNav::scan(device);
    if (kind == QLatin1String("hddvd") || kind == QLatin1String("vcd") || kind == QLatin1String("svcd") || kind == QLatin1String("cdda"))
        return Optical::scan(device, kind);
    if (kind == QLatin1String("dcp") || kind == QLatin1String("file")) {
        m["kind"] = kind;
        return m;
    }
    m["kind"] = QStringLiteral("bluray");
#ifdef LUMEN_HAVE_BLURAY
    BLURAY *bd = nullptr;
    {
        QMutexLocker lock(&blurayOpenMutex());
        bd = bd_open(blurayPath(device).toUtf8().constData(), nullptr);
    }
    if (!bd) {
        m["error"] = LTR("libbluray konnte die Disc nicht öffnen");
        return m;
    }
    if (const BLURAY_DISC_INFO *di = bd_get_disc_info(bd)) {
        m["discName"] = di->disc_name ? QString::fromUtf8(di->disc_name) : QString();
        m["volumeId"] = di->udf_volume_id ? QString::fromUtf8(di->udf_volume_id) : QString();
        m["aacsDetected"] = bool(di->aacs_detected);
        m["aacsLibrary"] = bool(di->libaacs_detected);
        m["aacsHandled"] = bool(di->aacs_handled);
        m["aacsError"] = int(di->aacs_error_code);
        m["bdplusDetected"] = bool(di->bdplus_detected);
        m["bdplusHandled"] = bool(di->bdplus_handled);
        m["bdjDetected"] = bool(di->bdj_detected);
        m["has3d"] = bool(di->content_exist_3D);
        m["hdmvTitles"] = int(di->num_hdmv_titles);
        m["bdjTitles"] = int(di->num_bdj_titles);
    }

    // Titel mit mind. 60 s – Menü-Loops und Trailer-Schnipsel ausblenden
    const uint32_t count = bd_get_titles(bd, TITLES_RELEVANT, 60);
    const int mainTitle = bd_get_main_title(bd);
    QVariantList titles;
    for (uint32_t i = 0; i < count; ++i) {
        BLURAY_TITLE_INFO *ti = bd_get_title_info(bd, i, 0);
        if (!ti)
            continue;
        QVariantMap t;
        t["index"] = int(i);
        t["playlist"] = int(ti->playlist);
        t["duration"] = double(ti->duration) / 90000.0;
        t["chapters"] = int(ti->chapter_count);
        t["angles"] = int(ti->angle_count);
        t["main"] = int(i) == mainTitle;
        if (ti->clip_count > 0) {
            const BLURAY_CLIP_INFO &c = ti->clips[0];
            if (c.video_stream_count > 0)
                t["video"] = QStringLiteral("%1 %2").arg(videoFormat(c.video_streams[0].format), videoCodec(c.video_streams[0].coding_type)).trimmed();
            QStringList audio;
            for (int a = 0; a < c.audio_stream_count && a < 6; ++a)
                audio << QStringLiteral("%1 %2").arg(QString::fromLatin1(reinterpret_cast<const char *>(c.audio_streams[a].lang), 3).toUpper(),
                                                     audioCodec(c.audio_streams[a].coding_type));
            t["audio"] = audio.join(QStringLiteral(" · "));
            t["subtitles"] = int(c.pg_stream_count);
        }
        titles.append(t);
        bd_free_title_info(ti);
    }
    m["titles"] = titles;
    bd_close(bd);
#else
    m["error"] = LTR("Ohne libbluray gebaut");
#endif
    return m;
}
