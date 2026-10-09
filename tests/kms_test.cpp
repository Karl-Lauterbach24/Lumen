// Was der Bildschirm über sich sagt (EDID) und welche Betriebsart ein Film bekommt (KMS).
#include "Edid.h"
#include "KmsDisplay.h"

#include <QCoreApplication>
#include <cstdio>

static int failures = 0;
static void check(bool ok, const char *what)
{
    std::printf("%s %s\n", ok ? "OK  " : "FAIL", what);
    if (!ok)
        ++failures;
}

// Ein EDID mit einer CTA-Erweiterung aus den gegebenen Datenblöcken
static QByteArray edidWith(const QList<QByteArray> &dataBlocks, bool basicAudio = false)
{
    QByteArray base(128, '\0');
    static const unsigned char header[8] = {0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00};
    memcpy(base.data(), header, 8);
    // Deskriptor mit dem Namen
    const char name[] = "\x00\x00\x00\xfc\x00Wohnzimmer TV\n";
    memcpy(base.data() + 54, name, 18);
    base[126] = 1;
    QByteArray cta(128, '\0');
    cta[0] = 0x02;
    cta[1] = 0x03;
    int at = 4;
    for (const QByteArray &block : dataBlocks) {
        memcpy(cta.data() + at, block.constData(), size_t(block.size()));
        at += int(block.size());
    }
    cta[2] = char(at);
    cta[3] = basicAudio ? 0x40 : 0x00;
    return base + cta;
}

static QByteArray block(int tag, const QByteArray &payload)
{
    return QByteArray(1, char((tag << 5) | payload.size())) + payload;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    check(!parseEdid(QByteArray()).valid, "leer: nicht gültig");
    check(!parseEdid(QByteArray(128, 'x')).valid, "ohne Kopf: nicht gültig");

    {
        // Ein Fernseher: PCM 2 Kanäle, Dolby Digital, DD+ mit Atmos, DTS; PQ und HLG, BT.2020
        const QByteArray audio = block(1, QByteArray::fromHex("097f07" "150750" "570403" "3d1ec0"));
        const QByteArray hdr = block(7, QByteArray::fromHex("06" "0c" "01" "73"));
        const QByteArray colorimetry = block(7, QByteArray::fromHex("05" "e0" "00"));
        const EdidInfo tv = parseEdid(edidWith({audio, hdr, colorimetry}));
        check(tv.valid && tv.name == QLatin1String("Wohnzimmer TV"), "Name aus dem Deskriptor");
        check(tv.pcmChannels == 2, "PCM: zwei Kanäle");
        check(tv.bitstream == QStringList({"ac3", "eac3", "dts"}), "Datenströme: Dolby Digital, DD+, DTS");
        check(tv.atmos, "Atmos in Dolby Digital Plus");
        check(tv.hdr10 && tv.hlg && tv.bt2020, "HDR10, HLG, BT.2020");
        check(!tv.dolbyVision && !tv.hdr10plus, "kein Dolby Vision, kein HDR10+");
        check(tv.maxLuminance > 590 && tv.maxLuminance < 620, "Spitzenhelligkeit aus dem Kennwert (rund 600 cd/m²)");
    }
    {
        // Ein Verstärker: PCM 8 Kanäle, TrueHD, DTS-HD; dazu Dolby Vision und HDR10+ des Fernsehers dahinter
        const QByteArray audio = block(1, QByteArray::fromHex("0f7f07" "670000" "5f7e01"));
        const QByteArray dolby = block(7, QByteArray::fromHex("01" "46d000" "0000"));
        const QByteArray plus = block(7, QByteArray::fromHex("01" "8b8490" "01"));
        const EdidInfo avr = parseEdid(edidWith({audio, dolby, plus}));
        check(avr.pcmChannels == 8, "PCM: acht Kanäle");
        check(avr.bitstream == QStringList({"truehd", "dts-hd"}), "Datenströme: TrueHD, DTS-HD");
        check(avr.dolbyVision && avr.hdr10plus, "Dolby Vision und HDR10+");
        check(!avr.hdr10 && !avr.hlg, "ohne HDR-Block: kein HDR10, kein HLG");
    }
    {
        // BT.2020 nur als YCC genannt: die Grafik gibt RGB aus, HDR ginge mit falschen Farben durch
        const QByteArray hdr = block(7, QByteArray::fromHex("06" "04" "01"));
        const QByteArray ycc = block(7, QByteArray::fromHex("05" "60" "00"));
        check(!parseEdid(edidWith({hdr, ycc})).bt2020, "BT.2020 nur als YCC: zählt nicht");
        // HDR erst in einer zweiten CTA-Erweiterung: mpv liest sie dort nicht, also zählt es auch hier nicht
        const QByteArray first = edidWith({block(1, QByteArray::fromHex("150750"))});
        const QByteArray second = edidWith({hdr, block(7, QByteArray::fromHex("05" "e0" "00")), block(1, QByteArray::fromHex("3d1ec0"))}).mid(128);
        QByteArray two = first + second;
        two[126] = 2;
        const EdidInfo late = parseEdid(two);
        check(!late.hdr10 && !late.bt2020, "HDR in der zweiten Erweiterung: zählt nicht");
        check(late.bitstream == QStringList({"ac3", "dts"}), "Tonformate aus allen Erweiterungen");
    }
    {
        const EdidInfo plain = parseEdid(edidWith({}, true));
        check(plain.valid && plain.pcmChannels == 2 && plain.bitstream.isEmpty(), "nur Grundton: Stereo, keine Datenströme");
        // ein Datenblock, der über das Ende hinausreicht, wird nicht gelesen
        QByteArray broken = edidWith({block(1, QByteArray::fromHex("150750"))});
        broken[128 + 2] = 5;
        check(parseEdid(broken).bitstream.isEmpty(), "abgeschnittener Block: nichts gelesen");
    }

    // --- Betriebsart für einen Film
    KmsOutput out;
    out.valid = true;
    out.modes = {{3840, 2160, 59.94, true}, {3840, 2160, 60.0, false}, {3840, 2160, 50.0, false}, {3840, 2160, 29.97, false},
                 {3840, 2160, 25.0, false}, {3840, 2160, 24.0, false}, {3840, 2160, 23.976, false}, {1920, 1080, 60.0, false},
                 {1920, 1080, 23.976, false}};
    check(Kms::modeFor(out, 23.976) == QLatin1String("3840x2160@23.98"), "23,976 Bilder: 23,976 Bildwechsel");
    check(Kms::modeFor(out, 24.0) == QLatin1String("3840x2160@24.00"), "24 Bilder: 24 Bildwechsel");
    check(Kms::modeFor(out, 25.0) == QLatin1String("3840x2160@50.00"), "25 Bilder: 50 Bildwechsel (Halbbilder)");
    check(Kms::modeFor(out, 29.97) == QLatin1String("3840x2160@59.94"), "29,97 Bilder: 59,94 Bildwechsel");
    check(Kms::modeFor(out, 59.94) == QLatin1String("3840x2160@59.94"), "59,94 Bilder: 59,94 Bildwechsel");
    check(Kms::modeFor(out, 60.0) == QLatin1String("3840x2160@60.00"), "60 Bilder: 60 Bildwechsel");
    check(Kms::modeFor(out, 15.0) == QLatin1String("3840x2160@60.00"), "15 Bilder: 60 Bildwechsel");
    check(Kms::modeFor(out, 0).isEmpty(), "Bildrate unbekannt: keine Wahl");
    // ein Bildschirm ohne 23,976: 24,000 ist nah genug
    out.modes = {{1920, 1080, 60.0, true}, {1920, 1080, 24.0, false}, {1920, 1080, 50.0, false}};
    check(Kms::modeFor(out, 23.976) == QLatin1String("1920x1080@24.00"), "23,976 Bilder ohne passende Betriebsart: 24");
    check(Kms::modeFor(out, 29.97) == QLatin1String("1920x1080@60.00"), "29,97 Bilder: 60 (ein Tausendstel daneben)");
    // nur 60 Hz: für 25 Bilder passt nichts – die bevorzugte Betriebsart bleibt
    out.modes = {{1920, 1080, 60.0, true}};
    check(Kms::modeFor(out, 25.0).isEmpty(), "nichts Passendes: keine Wahl");

    std::printf(failures ? "FAILED (%d)\n" : "PASSED\n", failures);
    return failures ? 1 : 0;
}
