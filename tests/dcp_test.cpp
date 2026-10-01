// DCP-Kette ohne GUI prüfen (siehe README, "Tests"):
//
//   dcp_test gencert <ordner>                 Zertifikatskette erzeugen (leaf.pem/leaf.key)
//   dcp_test info <dcp>                       CPLs, Rollen, Spurdateien
//   dcp_test kdm <kdm.xml> <leaf.key>         KDM auspacken, Schlüssel ausgeben
//   dcp_test decrypt <mxf> <schlüssel-hex> <out.mxf> [auge]
//   dcp_test subs <dcp> [schlüssel-hex]       Untertitel -> ASS
//   dcp_test play <dcp> [kdm leaf.key] [sekunden] [bild.png]
//                                             mit libmpv (vo=null) abspielen
#include "DcpCrypto.h"
#include "DcpIab.h"
#include "DcpPackage.h"
#include "DcpSignature.h"
#include "DcpStream.h"
#include "DcpSubtitles.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFile>
#include <QHash>
#include <QImage>
#include <QTemporaryDir>
#include <QThread>

#include <mpv/client.h>

#include <clocale>
#include <cmath>
#include <vector>
#include <cstdio>

namespace {

int usage()
{
    std::fprintf(stderr, "usage: dcp_test gencert|info|kdm|decrypt|subs|play ...\n");
    return 2;
}

void print(const QString &s)
{
    std::printf("%s\n", s.toLocal8Bit().constData());
    std::fflush(stdout);
}

QString edlFile(const QString &s)
{
    return QStringLiteral("%%1%%2").arg(s.toUtf8().size()).arg(s);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::setlocale(LC_NUMERIC, "C");
    const QStringList a = app.arguments().mid(1);
    if (a.isEmpty())
        return usage();
    const QString cmd = a[0];

    if (cmd == QLatin1String("gencert") && a.size() >= 2) {
        QString err;
        if (!DcpCrypto::createIdentity(a[1], QStringLiteral("Lumen Test"), &err)) {
            print(err);
            return 1;
        }
        const DcpCrypto::Identity id = DcpCrypto::loadIdentity(a[1]);
        print(QStringLiteral("valid=%1 serial=%2\nsubject=%3\nthumbprint=%4").arg(id.valid).arg(id.serial, id.subject, id.thumbprint));
        return id.valid ? 0 : 1;
    }

    // iab <mxf> <schlüssel-hex|-> <layout> [--check]: IAB/Atmos-Spur rendern, Pegel je
    // Kanal und Hälfte; --check prüft das Muster von iab_testgen (Objekt vorne
    // links -> oben vorne rechts, LFE-Ton)
    if (cmd == QLatin1String("iab") && a.size() >= 4) {
        const QByteArray key = a[2] == QLatin1String("-") ? QByteArray() : QByteArray::fromHex(a[2].toLatin1());
        Dcp::g_keyErrors = 0;
        Dcp::IabDecoder dec({a[1], key, 0}, a[3]);
        if (!dec.ok()) {
            print(QStringLiteral("Fehler: ") + dec.error());
            return 1;
        }
        const int n = dec.frames(), ch = dec.channels(), fs = dec.frameSamples();
        print(QStringLiteral("frames=%1 channels=%2 [%3] rate=%4 samples/frame=%5 mask=0x%6")
                  .arg(n).arg(ch).arg(dec.channelNames().join(QLatin1Char(' '))).arg(dec.sampleRate()).arg(fs)
                  .arg(dec.channelMask(), 0, 16));
        std::vector<float> buf(size_t(fs) * size_t(ch));
        std::vector<double> energy[2] = {std::vector<double>(size_t(ch)), std::vector<double>(size_t(ch))};
        for (int f = 0; f < n; ++f) {
            dec.render(f, buf.data());
            for (int i = 0; i < fs; ++i)
                for (int c = 0; c < ch; ++c)
                    energy[f >= n / 2][size_t(c)] += double(buf[size_t(i) * size_t(ch) + size_t(c)]) * buf[size_t(i) * size_t(ch) + size_t(c)];
        }
        const QStringList names = dec.channelNames();
        QString loudest[2];
        double lfe[2] = {};
        for (int h = 0; h < 2; ++h) {
            QStringList row;
            double best = -1;
            for (int c = 0; c < ch; ++c) {
                const double rms = std::sqrt(energy[h][size_t(c)] / std::max(1, (n / 2) * fs));
                row << QStringLiteral("%1=%2").arg(names[c]).arg(rms, 0, 'f', 4);
                if (names[c] == QLatin1String("LFE"))
                    lfe[h] = rms;
                else if (rms > best)
                    best = rms, loudest[h] = names[c];
            }
            print(QStringLiteral("%1. Hälfte: %2").arg(h + 1).arg(row.join(QLatin1Char(' '))));
        }
        print(QStringLiteral("lautester Kanal: %1 -> %2, Frame-Fehler=%3, Prüfwertfehler=%4")
                  .arg(loudest[0], loudest[1]).arg(dec.errors()).arg(Dcp::g_keyErrors.load()));
        if (!a.contains(QStringLiteral("--check")))
            return dec.errors() ? 1 : 0;
        const bool heights = names.contains(QStringLiteral("RFH"));
        const bool stereo = !names.contains(QStringLiteral("LFE"));
        const bool ok = dec.errors() == 0 && Dcp::g_keyErrors == 0 && loudest[0] == QLatin1String("L")
                        && loudest[1] == (heights ? QStringLiteral("RFH") : QStringLiteral("R"))
                        && (stereo || (lfe[0] > 0.01 && lfe[1] > 0.01));
        print(ok ? QStringLiteral("IAB-Muster korrekt") : QStringLiteral("IAB-Muster FALSCH"));
        return ok ? 0 : 1;
    }

    if (cmd == QLatin1String("info") && a.size() >= 2) {
        const Dcp::Package pkg = Dcp::scan(a[1]);
        if (!pkg.error.isEmpty())
            print(QStringLiteral("error: ") + pkg.error);
        for (const Dcp::Cpl &c : pkg.cpls) {
            print(QStringLiteral("CPL %1 \"%2\" kind=%3 %4 reels=%5 dur=%6s encrypted=%7 stereo=%8 missing=%9")
                      .arg(c.id, c.title, c.contentKind, c.smpte ? "SMPTE" : "Interop").arg(c.reels.size())
                      .arg(c.seconds(), 0, 'f', 3).arg(c.encrypted()).arg(c.stereoscopic()).arg(c.missing.size()));
            for (const Dcp::Reel &r : c.reels) {
                print(QStringLiteral("  reel %1 dur=%2 markers=%3").arg(r.id).arg(r.duration).arg(r.markers.size()));
                for (const Dcp::ReelAsset &ra : r.assets)
                    print(QStringLiteral("    %1 %2 entry=%3 dur=%4 key=%5 mxf[%6 %7x%8 ch=%9 enc=%10]")
                              .arg(Dcp::kindName(ra.kind), QFileInfo(ra.file).fileName()).arg(ra.entryPoint).arg(ra.duration)
                              .arg(ra.keyId.isEmpty() ? QStringLiteral("-") : ra.keyId, ra.mxf.codec).arg(ra.mxf.width).arg(ra.mxf.height)
                              .arg(ra.mxf.channels).arg(ra.mxf.encrypted));
            }
        }
        return pkg.cpls.isEmpty() ? 1 : 0;
    }

    if (cmd == QLatin1String("kdm") && a.size() >= 3) {
        const DcpCrypto::Kdm k = DcpCrypto::decryptKdm(a[1], a[2]);
        print(QStringLiteral("cpl=%1 title=%2 valid=%3..%4 error=%5").arg(k.cplId, k.title, k.notBefore.toString(Qt::ISODate), k.notAfter.toString(Qt::ISODate), k.error));
        for (const auto &key : k.keys)
            print(QStringLiteral("  %1 %2 %3").arg(key.type, key.keyId, QString::fromLatin1(key.key.toHex())));
        return k.keys.isEmpty() ? 1 : 0;
    }

    // signkdm <kdm.xml> <signer.key> <kette.pem> <out.xml>
    if (cmd == QLatin1String("signkdm") && a.size() >= 5) {
        auto read = [](const QString &f) { QFile x(f); return x.open(QIODevice::ReadOnly) ? x.readAll() : QByteArray(); };
        QByteArray out;
        QString err;
        if (!DcpSignature::sign(read(a[1]), {QStringLiteral("ID_AuthenticatedPublic"), QStringLiteral("ID_AuthenticatedPrivate")},
                                read(a[2]), read(a[3]), &out, &err)) {
            print(err);
            return 1;
        }
        QFile f(a[4]);
        f.open(QIODevice::WriteOnly);
        f.write(out);
        print(QStringLiteral("signiert"));
        return 0;
    }
    if (cmd == QLatin1String("verifykdm") && a.size() >= 2) {
        QFile f(a[1]);
        f.open(QIODevice::ReadOnly);
        const DcpSignature::Result r = DcpSignature::verify(f.readAll());
        print(QStringLiteral("present=%1 valid=%2 chain=%3 signer=%4 error=%5").arg(r.present).arg(r.valid).arg(r.chainValid).arg(r.signer, r.error));
        return r.valid && r.chainValid ? 0 : 1;
    }

    if (cmd == QLatin1String("decrypt") && a.size() >= 4) {
        Dcp::StreamSpec spec{a[1], QByteArray::fromHex(a[2].toLatin1()), a.size() > 4 ? a[4].toInt() : 0};
        const QByteArray data = Dcp::readAllTransformed(spec);
        QFile out(a[3]);
        if (!out.open(QIODevice::WriteOnly))
            return 1;
        out.write(data);
        print(QStringLiteral("%1 Bytes, Prüfwertfehler=%2").arg(data.size()).arg(Dcp::g_keyErrors.load()));
        return Dcp::g_keyErrors ? 1 : 0;
    }

    // play3d <dcp> <kdm> <leaf.key> <bild.png>: beide Augen dekodieren, nebeneinander,
    // und prüfen, dass links und rechts verschiedene Bilder stehen (rechts: rot)
    if (cmd == QLatin1String("play3d") && a.size() >= 5) {
        const Dcp::Package pkg = Dcp::scan(a[1]);
        if (pkg.cpls.isEmpty())
            return 1;
        const DcpCrypto::Kdm k = DcpCrypto::decryptKdm(a[2], a[3]);
        QHash<QString, QByteArray> keys;
        for (const auto &key : k.keys)
            keys.insert(key.keyId, key.key);
        QString eyes[2];
        for (const Dcp::Reel &r : pkg.cpls[0].reels) {
            const Dcp::ReelAsset *p = r.find(Dcp::Kind::StereoPicture);
            if (!p)
                return 1;
            for (int eye = 0; eye < 2; ++eye)
                eyes[eye] += QStringLiteral("%1,%2,%3;").arg(edlFile(Dcp::streamUrl(Dcp::registerStream({p->file, keys.value(p->keyId), eye + 1}))))
                                 .arg(p->startSeconds(), 0, 'f', 6).arg(r.seconds(), 0, 'f', 6);
        }
        const QByteArray url = (QStringLiteral("edl://!new_stream;") + eyes[0] + QStringLiteral("!new_stream;") + eyes[1]).toUtf8();
        mpv_handle *mpv = mpv_create();
        const QByteArray outDir = QFileInfo(a[4]).absolutePath().toUtf8();
        for (auto [key, val] : std::initializer_list<std::pair<const char *, const char *>>{
                 {"vo", "image"}, {"ao", "null"}, {"frames", "3"}, {"vo-image-format", "png"}, {"terminal", "no"},
                 {"load-unsafe-playlists", "yes"}, {"lavfi-complex", "[vid1][vid2]hstack[vo]"}, {"hwdec", "no"}})
            mpv_set_option_string(mpv, key, val);
        mpv_set_option_string(mpv, "vo-image-outdir", outDir.constData());
        if (mpv_initialize(mpv) < 0)
            return 1;
        Dcp::attachProtocol(mpv);
        const char *load[] = {"loadfile", url.constData(), nullptr};
        mpv_command(mpv, load);
        for (;;) {
            mpv_event *ev = mpv_wait_event(mpv, 30);
            if (ev->event_id == MPV_EVENT_END_FILE || ev->event_id == MPV_EVENT_NONE)
                break;
        }
        mpv_terminate_destroy(mpv);
        const QImage img(QFileInfo(a[4]).absolutePath() + QStringLiteral("/00000002.png"));
        if (img.isNull()) {
            print(QStringLiteral("kein Bild"));
            return 1;
        }
        const QColor left = img.pixelColor(img.width() / 4, img.height() / 2);
        const QColor right = img.pixelColor(img.width() * 3 / 4, img.height() / 2);
        print(QStringLiteral("SBS %1x%2 links=%3 rechts=%4 keyErrors=%5").arg(img.width()).arg(img.height())
                  .arg(left.name(), right.name()).arg(Dcp::g_keyErrors.load()));
        img.save(a[4]);
        const bool ok = img.width() == 4096 && right.red() > 200 && right.green() < 60 && right.blue() < 60
                        && !(left.red() > 200 && left.green() < 60 && left.blue() < 60) && Dcp::g_keyErrors == 0;
        return ok ? 0 : 1;
    }

    if ((cmd == QLatin1String("play") || cmd == QLatin1String("subs")) && a.size() >= 2) {
        const Dcp::Package pkg = Dcp::scan(a[1]);
        if (pkg.cpls.isEmpty()) {
            print(QStringLiteral("keine CPL: ") + pkg.error);
            return 1;
        }
        QHash<QString, QByteArray> keys;
        double seconds = 3;
        QString png;
        int argi = 2;
        if (cmd == QLatin1String("subs") && a.size() > 2) {
            for (const QString &id : pkg.cpls[0].keyIds())
                keys.insert(id, QByteArray::fromHex(a[2].toLatin1()));
        }
        if (cmd == QLatin1String("play") && a.size() >= 4 && a[2].endsWith(QLatin1String(".xml"))) {
            const DcpCrypto::Kdm k = DcpCrypto::decryptKdm(a[2], a[3]);
            for (const auto &key : k.keys)
                keys.insert(key.keyId, key.key);
            print(QStringLiteral("KDM: %1 Schlüssel %2").arg(k.keys.size()).arg(k.error));
            argi = 4;
        }
        if (cmd == QLatin1String("play") && a.size() > argi)
            seconds = a[argi].toDouble();
        if (cmd == QLatin1String("play") && a.size() > argi + 1 && !a[argi + 1].startsWith(QLatin1String("--")))
            png = a[argi + 1];
        // --iab: Immersive-Audio-Spur (7.1.4) als zweite Tonspur, ausgewählt
        const bool withIab = a.contains(QStringLiteral("--iab"));

        const Dcp::Cpl &cpl = pkg.cpls[0];
        QTemporaryDir tmp;
        QString pic, snd, iab;
        QList<Dcp::SubtitleSource> subs;
        double t = 0;
        for (const Dcp::Reel &r : cpl.reels) {
            const double len = r.seconds();
            const Dcp::ReelAsset *p = r.find(Dcp::Kind::Picture);
            if (!p)
                p = r.find(Dcp::Kind::StereoPicture);
            if (p) {
                const int id = Dcp::registerStream({p->file, keys.value(p->keyId), p->mxf.stereoscopic ? 1 : 0});
                pic += QStringLiteral("%1,%2,%3\n").arg(edlFile(Dcp::streamUrl(id))).arg(p->startSeconds(), 0, 'f', 6).arg(len, 0, 'f', 6);
            }
            if (const Dcp::ReelAsset *s = r.find(Dcp::Kind::Sound)) {
                const int id = Dcp::registerStream({s->file, keys.value(s->keyId), 0});
                snd += QStringLiteral("%1,%2,%3\n").arg(edlFile(Dcp::streamUrl(id))).arg(s->startSeconds(), 0, 'f', 6).arg(len, 0, 'f', 6);
            }
            if (const Dcp::ReelAsset *ia = r.find(Dcp::Kind::Atmos); ia && withIab) {
                const int id = Dcp::registerIabStream({ia->file, keys.value(ia->keyId), 0}, QStringLiteral("7.1.4"));
                iab += QStringLiteral("%1,%2,%3\n").arg(edlFile(Dcp::iabUrl(id))).arg(ia->startSeconds(), 0, 'f', 6).arg(len, 0, 'f', 6);
            }
            if (const Dcp::ReelAsset *st = r.find(Dcp::Kind::Subtitle))
                subs.append({st->file, keys.value(st->keyId), t, st->startSeconds(), len, st->language});
            t += len;
        }
        Dcp::SubtitleResult sr;
        if (!subs.isEmpty()) {
            sr = Dcp::buildSubtitles(subs, tmp.path(), QStringLiteral("subs"));
            print(QStringLiteral("Untertitel: %1 Einträge, %2 Bilder, Sprache %3, %4").arg(sr.events).arg(sr.images).arg(sr.language, sr.error));
            if (cmd == QLatin1String("subs")) {
                QFile f(sr.assFile);
                if (f.open(QIODevice::ReadOnly))
                    std::printf("%s\n", f.readAll().constData());
                return sr.events > 0 ? 0 : 1;
            }
        }

        const QString edlPath = tmp.filePath(QStringLiteral("c.edl"));
        {
            QFile f(edlPath);
            f.open(QIODevice::WriteOnly);
            f.write(QStringLiteral("# mpv EDL v0\n!no_chapters\n!new_stream\n%1!new_stream\n%2").arg(pic, snd).toUtf8());
        }
        mpv_handle *mpv = mpv_create();
        mpv_set_option_string(mpv, "vo", png.isEmpty() ? "null" : "image");
        mpv_set_option_string(mpv, "ao", "null");
        mpv_set_option_string(mpv, "hwdec", "no");
        mpv_set_option_string(mpv, "vd-lavc-threads", "0");
        mpv_set_option_string(mpv, "idle", "yes");
        mpv_set_option_string(mpv, "load-unsafe-playlists", "yes");
        mpv_set_option_string(mpv, "terminal", "no");
        mpv_request_log_messages(mpv, "warn");
        if (!png.isEmpty()) {
            mpv_set_option_string(mpv, "vo-image-format", "png");
            mpv_set_option_string(mpv, "vo-image-outdir", QFileInfo(png).absolutePath().toUtf8().constData());
            mpv_set_option_string(mpv, "frames", "1");
        }
        // am Dateiende anhalten statt schließen: auf langsamen Rechnern ist das Ende sonst erreicht,
        // bevor die Eigenschaften unten gelesen sind
        mpv_set_option_string(mpv, "keep-open", "yes");
        if (mpv_initialize(mpv) < 0)
            return 1;
        Dcp::attachProtocol(mpv);
        Dcp::attachIabProtocol(mpv);
        if (!iab.isEmpty())
            mpv_set_property_string(mpv, "aid", "2");
        // Inline-EDL (wie in Lumen): eigene Protokolle sind nur so erlaubt
        const QByteArray ep = (QStringLiteral("edl://!no_chapters;!new_stream;") + pic.trimmed().replace(QLatin1Char('\n'), QLatin1Char(';'))
                               + QStringLiteral(";!new_stream;") + snd.trimmed().replace(QLatin1Char('\n'), QLatin1Char(';'))
                               + (iab.isEmpty() ? QString() : QStringLiteral(";!new_stream;") + iab.trimmed().replace(QLatin1Char('\n'), QLatin1Char(';'))))
                                  .toUtf8();
        const char *load[] = {"loadfile", ep.constData(), nullptr};
        mpv_command(mpv, load);
        bool loaded = false, ended = false;
        double lastPos = -1;
        QElapsedTimer clock;
        clock.start();
        while (!ended && clock.elapsed() < (seconds + 20) * 1000) {
            mpv_event *ev = mpv_wait_event(mpv, 0.5);
            if (ev->event_id == MPV_EVENT_LOG_MESSAGE) {
                auto *m = static_cast<mpv_event_log_message *>(ev->data);
                std::printf("mpv[%s] %s", m->prefix, m->text);
            } else if (ev->event_id == MPV_EVENT_FILE_LOADED) {
                loaded = true;
            } else if (ev->event_id == MPV_EVENT_END_FILE) {
                auto *e = static_cast<mpv_event_end_file *>(ev->data);
                print(QStringLiteral("END_FILE reason=%1 error=%2").arg(e->reason).arg(QString::fromUtf8(mpv_error_string(e->error))));
                ended = true;
            }
            double pos = 0;
            if (loaded && mpv_get_property(mpv, "time-pos", MPV_FORMAT_DOUBLE, &pos) >= 0) {
                lastPos = pos;
                if (pos >= seconds)
                    break;
            }
            int eof = 0;
            if (loaded && mpv_get_property(mpv, "eof-reached", MPV_FORMAT_FLAG, &eof) >= 0 && eof)
                break;
        }
        double pos = -1;
        int64_t drops = 0, frames = 0;
        if (mpv_get_property(mpv, "time-pos", MPV_FORMAT_DOUBLE, &pos) < 0)
            pos = lastPos; // Ende erreicht
        mpv_get_property(mpv, "decoder-frame-drop-count", MPV_FORMAT_INT64, &drops);
        mpv_get_property(mpv, "estimated-frame-number", MPV_FORMAT_INT64, &frames);
        char *vfmt = mpv_get_property_string(mpv, "video-params/pixelformat");
        char *afmt = mpv_get_property_string(mpv, "audio-params/channel-count");
        char *dur = mpv_get_property_string(mpv, "duration");
        print(QStringLiteral("loaded=%1 pos=%2 dur=%3 frames=%4 drops=%5 pixfmt=%6 audio-ch=%7 keyErrors=%8 missingKeys=%9")
                  .arg(loaded).arg(pos, 0, 'f', 2).arg(QString::fromUtf8(dur ? dur : "?")).arg(frames).arg(drops)
                  .arg(QString::fromUtf8(vfmt ? vfmt : "-"), QString::fromUtf8(afmt ? afmt : "-"))
                  .arg(Dcp::g_keyErrors.load()).arg(Dcp::g_missingKeys.load()));
        mpv_free(vfmt);
        mpv_free(afmt);
        mpv_free(dur);
        mpv_terminate_destroy(mpv);
        return loaded && pos > 0 && Dcp::g_keyErrors == 0 ? 0 : 1;
    }
    return usage();
}
