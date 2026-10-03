#include "StereoDetect.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QStringList>
#include <QThread>

#include <algorithm>
#include <cstring>
#include <cmath>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace StereoDetect {

// --------------------------------------------------------------------------
// Dateiname
// --------------------------------------------------------------------------

namespace {

// "h" + "sbs", "half" + "ou" … ; liefert 1 (halb), 0 (voll) oder -1
int sizeWord(const QString &t)
{
    if (t == QLatin1String("h") || t == QLatin1String("half"))
        return 1;
    if (t == QLatin1String("f") || t == QLatin1String("full"))
        return 0;
    return -1;
}

// Kennung einer Anordnung, auch mit Vorsilbe: sbs, hsbs, halfsbs, fsbs, fullsbs, tab, htab, ou, hou …
// weak: nur zusammen mit "3D" glaubwürdig ("ou" und "tab" sind auch gewöhnliche Wörter)
bool layoutWord(QString t, Layout *layout, int *half, bool *weak)
{
    *half = -1;
    *weak = false;
    for (const char *prefix : {"half", "full"}) {
        if (t.startsWith(QLatin1String(prefix)) && t.size() > 4) {
            *half = prefix[0] == 'h';
            t = t.mid(4);
        }
    }
    if (*half < 0 && t.size() > 2 && (t.at(0) == QLatin1Char('h') || t.at(0) == QLatin1Char('f'))
        && (t.mid(1) == QLatin1String("sbs") || t.mid(1) == QLatin1String("tab") || t.mid(1) == QLatin1String("ou"))) {
        *half = t.at(0) == QLatin1Char('h');
        // "hou"/"fou" allein sind zu kurz, um sicher zu sein
        *weak = t.mid(1) == QLatin1String("ou");
        t = t.mid(1);
    }
    if (t == QLatin1String("sbs") || t == QLatin1String("sidebyside")) {
        *layout = SideBySide;
        return true;
    }
    if (t == QLatin1String("tab") || t == QLatin1String("ou") || t == QLatin1String("overunder") || t == QLatin1String("topbottom")
        || t == QLatin1String("topandbottom") || t == QLatin1String("abovebelow")) {
        *layout = TopBottom;
        if (*half < 0 && (t == QLatin1String("tab") || t == QLatin1String("ou")))
            *weak = true;
        return true;
    }
    return false;
}

} // namespace

Hint fromName(const QString &fileName)
{
    Hint hint;
    const QString lower = fileName.toLower();
    const int dot = lower.lastIndexOf(QLatin1Char('.'));
    const QString ext = dot > 0 ? lower.mid(dot + 1) : QString();
    const QString base = dot > 0 ? lower.left(dot) : lower;
    if (ext == QLatin1String("mk3d"))
        hint.is3d = hint.tagged3d = true;
    if (ext == QLatin1String("ssif")) {
        hint.is3d = hint.tagged3d = true;
        hint.layout = Mvc;
    }

    QStringList tokens;
    QString cur;
    for (const QChar c : base) {
        if (c.isLetterOrNumber()) {
            cur += c;
        } else if (!cur.isEmpty()) {
            tokens << cur;
            cur.clear();
        }
    }
    if (!cur.isEmpty())
        tokens << cur;

    Layout weakLayout = NoLayout;
    int weakHalf = -1;
    bool order = false, rightFirst = false;
    for (int i = 0; i < tokens.size(); ++i) {
        QString t = tokens.at(i);
        // "3dsbs", "3dhou": die Kennung hängt am "3d"
        if (t.startsWith(QLatin1String("3d"))) {
            hint.is3d = hint.tagged3d = true;
            t = t.mid(2);
            if (t.isEmpty())
                continue;
        }
        if (t == QLatin1String("mvc") && hint.layout == NoLayout) {
            hint.layout = Mvc;
            continue;
        }
        if (t == QLatin1String("rl") || t == QLatin1String("lr")) {
            order = true;
            rightFirst = t == QLatin1String("rl");
            continue;
        }
        // zusammengesetzt über Trennzeichen: "side-by-side", "over-under", "top-and-bottom"
        const QString two = t + tokens.value(i + 1);
        const QString three = two + tokens.value(i + 2);
        Layout layout = NoLayout;
        int half = -1;
        bool weak = false;
        int used = 0;
        if (layoutWord(three, &layout, &half, &weak) && !tokens.value(i + 2).isEmpty())
            used = 3;
        else if (layoutWord(two, &layout, &half, &weak) && !tokens.value(i + 1).isEmpty())
            used = 2;
        else if (layoutWord(t, &layout, &half, &weak))
            used = 1;
        if (!used)
            continue;
        // Größenangabe davor oder danach: "half sbs", "sbs full", "h-ou"
        if (half < 0 && i > 0) {
            half = sizeWord(tokens.at(i - 1));
            if (half >= 0)
                weak = false;
        }
        if (half < 0)
            half = sizeWord(tokens.value(i + used));
        if (weak) {
            if (weakLayout == NoLayout) {
                weakLayout = layout;
                weakHalf = half;
            }
        } else if (hint.layout == NoLayout || hint.layout == Mvc) {
            hint.layout = layout;
            hint.half = half;
        }
        i += used - 1;
    }
    if (hint.layout == NoLayout && weakLayout != NoLayout && hint.is3d) {
        hint.layout = weakLayout;
        hint.half = weakHalf;
    }
    if (hint.layout != NoLayout)
        hint.is3d = true;
    if (order && hint.is3d)
        hint.rightFirst = rightFirst;
    return hint;
}

// --------------------------------------------------------------------------
// Container-Angaben, Bildgröße
// --------------------------------------------------------------------------

Hint fromMetadata(const QString &stereoIn)
{
    Hint hint;
    const QString s = stereoIn.toLower();
    if (s.isEmpty() || s == QLatin1String("mono") || s == QLatin1String("no"))
        return hint;
    if (s.startsWith(QLatin1String("sbs")))
        hint.layout = SideBySide;
    else if (s.startsWith(QLatin1String("ab")))
        hint.layout = TopBottom;
    else if (s.startsWith(QLatin1String("ir")))
        hint.layout = Interleaved;
    else if (s == QLatin1String("al") || s == QLatin1String("ar"))
        hint.layout = Alternating;
    else
        return hint; // Schachbrett, Spalten, Anaglyph: kann der Filter nicht auflösen
    hint.is3d = true;
    hint.rightFirst = s.endsWith(QLatin1Char('r'));
    // mpv kennt nur "sbs2"/"ab2"; ob halb oder voll, zeigt erst die Bildgröße
    return hint;
}

QString format(const Hint &hint, int width, int height)
{
    const QLatin1Char eye(hint.rightFirst ? 'r' : 'l');
    switch (hint.layout) {
    case SideBySide: {
        bool half = hint.half == 1;
        // Eine Hälfte schmaler als 1,3:1 ist kein Kinobild: gestaucht, also halbe Auflösung
        if (hint.half < 0)
            half = width <= 0 || height <= 0 || double(width) / 2 / height < 1.3;
        return (half ? QStringLiteral("sbs2") : QStringLiteral("sbs")) + eye;
    }
    case TopBottom: {
        bool half = hint.half == 1;
        // Eine Hälfte breiter als 3:1: gestaucht
        if (hint.half < 0)
            half = width <= 0 || height <= 0 || double(width) / (double(height) / 2) > 3.0;
        return (half ? QStringLiteral("ab2") : QStringLiteral("ab")) + eye;
    }
    case Interleaved:
        return QStringLiteral("ir") + eye;
    case Alternating:
        return QStringLiteral("a") + eye;
    default:
        return QStringLiteral("none");
    }
}

Hint fromSize(int width, int height)
{
    Hint hint;
    if (width <= 0 || height <= 0)
        return hint;
    const double ratio = double(width) / height;
    if (width >= 2560 && ratio >= 3.4 && ratio <= 3.7) { // 3840x1080, 2560x720
        hint.is3d = true;
        hint.layout = SideBySide;
        hint.half = 0;
    } else if (height >= 1440 && ratio >= 0.85 && ratio <= 0.95) { // 1920x2160, 1280x1440
        hint.is3d = true;
        hint.layout = TopBottom;
        hint.half = 0;
    }
    return hint;
}

// --------------------------------------------------------------------------
// Bildvergleich
// --------------------------------------------------------------------------

namespace {

// Zwei gleich große Felder (cols x rows, zeilenweise) gegeneinander, das zweite waagerecht
// um -maxShift … +maxShift verschoben (die Augen sehen dasselbe leicht versetzt): größte
// normierte Kreuzkorrelation.
double bestCorrelation(const std::vector<float> &a, const std::vector<float> &b, int cols, int rows, int maxShift)
{
    double best = -1;
    for (int shift = -maxShift; shift <= maxShift; ++shift) {
        const int x0 = std::max(0, -shift), x1 = std::min(cols, cols - shift);
        double ab = 0, aa = 0, bb = 0;
        for (int y = 0; y < rows; ++y) {
            const float *pa = a.data() + size_t(y) * cols;
            const float *pb = b.data() + size_t(y) * cols;
            for (int x = x0; x < x1; ++x) {
                const double va = pa[x], vb = pb[x + shift];
                ab += va * vb;
                aa += va * va;
                bb += vb * vb;
            }
        }
        if (aa > 0 && bb > 0)
            best = std::max(best, ab / std::sqrt(aa * bb));
    }
    return best;
}

// Mittlere Abweichung vom Nullpunkt: wie viel Zeichnung ist übrig?
double detail(const std::vector<float> &v)
{
    if (v.empty())
        return 0;
    double sum = 0;
    for (const float f : v)
        sum += std::fabs(f);
    return sum / double(v.size());
}

// Zeilen- und Spaltenmittel abziehen. Übrig bleibt nur, was sich in beiden Richtungen ändert:
// Balken, Streifen, Horizonte und Verläufe gleichen sich in jedem Bild über die Hälften hinweg
// (ein Testbild mit Farbbalken sähe sonst wie 3D aus), echte Zeichnung nur bei zwei Ansichten.
void center(std::vector<float> &v, int cols, int rows)
{
    if (cols <= 0 || rows <= 0)
        return;
    std::vector<double> col(cols, 0);
    for (int y = 0; y < rows; ++y) {
        float *p = v.data() + size_t(y) * cols;
        double row = 0;
        for (int x = 0; x < cols; ++x)
            row += p[x];
        row /= cols;
        for (int x = 0; x < cols; ++x) {
            p[x] -= float(row);
            col[x] += p[x];
        }
    }
    for (int y = 0; y < rows; ++y) {
        float *p = v.data() + size_t(y) * cols;
        for (int x = 0; x < cols; ++x)
            p[x] -= float(col[x] / rows);
    }
}

const int kFlat = 10;       // Zeile/Spalte mit weniger Hub gilt als Balken oder leere Fläche
const double kDetail = 2.5; // Graustufen: darunter ist die Hälfte zu gleichförmig

} // namespace

Scores compare(const unsigned char *gray, int width, int height, int stride)
{
    Scores scores;
    if (!gray || width < 64 || height < 32)
        return scores;

    // Nebeneinander: Hälften links/rechts, ohne die Zeilen der schwarzen Balken
    {
        const int cols = width / 2;
        std::vector<float> a, b;
        int rows = 0;
        for (int y = 0; y < height; ++y) {
            const unsigned char *row = gray + size_t(y) * stride;
            int lo = 255, hi = 0;
            for (int x = 0; x < 2 * cols; ++x) {
                lo = std::min(lo, int(row[x]));
                hi = std::max(hi, int(row[x]));
            }
            if (hi - lo < kFlat)
                continue;
            for (int x = 0; x < cols; ++x)
                a.push_back(row[x]);
            for (int x = 0; x < cols; ++x)
                b.push_back(row[cols + x]);
            ++rows;
        }
        center(a, cols, rows);
        center(b, cols, rows);
        if (rows >= height / 4 && detail(a) >= kDetail && detail(b) >= kDetail)
            scores.sideBySide = bestCorrelation(a, b, cols, rows, std::max(2, cols / 16));
    }

    // Übereinander: Hälften oben/unten, ohne die Spalten seitlicher Balken
    {
        const int rows = height / 2;
        std::vector<int> columns;
        for (int x = 0; x < width; ++x) {
            int lo = 255, hi = 0;
            for (int y = 0; y < 2 * rows; ++y) {
                const int v = gray[size_t(y) * stride + x];
                lo = std::min(lo, v);
                hi = std::max(hi, v);
            }
            if (hi - lo >= kFlat)
                columns.push_back(x);
        }
        const int cols = int(columns.size());
        if (cols >= width / 4) {
            std::vector<float> a(size_t(cols) * rows), b(size_t(cols) * rows);
            for (int y = 0; y < rows; ++y) {
                for (int i = 0; i < cols; ++i) {
                    const int x = columns[i];
                    a[size_t(y) * cols + i] = gray[size_t(y) * stride + x];
                    b[size_t(y) * cols + i] = gray[size_t(rows + y) * stride + x];
                }
            }
            center(a, cols, rows);
            center(b, cols, rows);
            if (detail(a) >= kDetail && detail(b) >= kDetail)
                scores.topBottom = bestCorrelation(a, b, cols, rows, std::max(2, width / 16));
        }
    }
    return scores;
}

Hint fromScores(const QList<Scores> &frames, bool hint3d, double *confidence)
{
    Hint hint;
    if (confidence)
        *confidence = 0;
    // Mindest-Ähnlichkeit und Abstand zur anderen Anordnung; Anteil der Bilder, die zustimmen
    // (gemessen: echte Paare 0,75 … 0,95, gewöhnliche Bilder um 0)
    const double minScore = hint3d ? 0.5 : 0.65;
    const double minShare = hint3d ? 0.5 : 0.7;
    int valid = 0, sbs = 0, tab = 0;
    QList<double> sbsScores, tabScores;
    for (const Scores &s : frames) {
        if (s.sideBySide < -1 && s.topBottom < -1)
            continue;
        ++valid;
        if (s.sideBySide >= minScore && s.sideBySide - std::max(s.topBottom, 0.0) >= 0.2) {
            ++sbs;
            sbsScores << s.sideBySide;
        } else if (s.topBottom >= minScore && s.topBottom - std::max(s.sideBySide, 0.0) >= 0.2) {
            ++tab;
            tabScores << s.topBottom;
        }
    }
    if (valid < (hint3d ? 2 : 3))
        return hint;
    auto median = [](QList<double> v) {
        std::sort(v.begin(), v.end());
        return v.isEmpty() ? 0.0 : v.at(v.size() / 2);
    };
    if (sbs >= tab && sbs >= minShare * valid) {
        hint.layout = SideBySide;
        if (confidence)
            *confidence = median(sbsScores) * sbs / valid;
    } else if (tab > sbs && tab >= minShare * valid) {
        hint.layout = TopBottom;
        if (confidence)
            *confidence = median(tabScores) * tab / valid;
    }
    hint.is3d = hint.layout != NoLayout;
    return hint;
}

// --------------------------------------------------------------------------
// Datei auswerten
// --------------------------------------------------------------------------

// Die ersten Pakete eines H.264-Stroms im Annex-B-Format (MPEG-TS) nach NAL-Einheiten einer
// zweiten Ansicht durchsuchen: Subset-SPS (15) oder Slice-Erweiterung (20). Liest höchstens
// 120 Pakete und spult danach an den Anfang zurück.
static bool hasMvcNalUnits(AVFormatContext *fmt, int index)
{
    if (!fmt->iformat || strcmp(fmt->iformat->name, "mpegts") != 0)
        return false; // Matroska und MP4 nennen das Profil der beiden Ansichten selbst
    AVPacket *pkt = av_packet_alloc();
    bool found = false;
    for (int n = 0, seen = 0; pkt && !found && n < 2000 && seen < 120; ++n) {
        if (av_read_frame(fmt, pkt) < 0)
            break;
        if (pkt->stream_index == index) {
            ++seen;
            for (int i = 0; i + 3 < pkt->size && !found; ++i) {
                if (pkt->data[i] == 0 && pkt->data[i + 1] == 0 && pkt->data[i + 2] == 1) {
                    const int type = pkt->data[i + 3] & 0x1f;
                    found = type == 15 || type == 20;
                }
            }
        }
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    av_seek_frame(fmt, -1, fmt->start_time != AV_NOPTS_VALUE ? fmt->start_time : 0, AVSEEK_FLAG_BACKWARD);
    return found;
}

Hint analyzeFile(const QString &path, bool hint3d, const std::atomic_bool *cancel, double *confidence, int *width, int *height,
                 QList<Scores> *scoresOut, bool *mvc)
{
    const int kW = 320, kH = 180; // Vergleichsgröße
    const int kFrames = 7;        // Stellen über die Laufzeit
    const qint64 kBudgetMs = 5000;

    Hint none;
    if (confidence)
        *confidence = 0;
    auto cancelled = [&] { return cancel && cancel->load(); };

    QElapsedTimer timer;
    timer.start();
    // Auch mitten im Lesen abbrechen können (langsames Laufwerk, Netzlaufwerk)
    struct Guard
    {
        const std::atomic_bool *cancel;
        const QElapsedTimer *timer;
        qint64 limit;
    } guard{cancel, &timer, kBudgetMs + 1500};
    AVFormatContext *fmt = avformat_alloc_context();
    if (!fmt)
        return none;
    fmt->interrupt_callback.opaque = &guard;
    fmt->interrupt_callback.callback = [](void *opaque) -> int {
        const auto *g = static_cast<const Guard *>(opaque);
        return (g->cancel && g->cancel->load()) || g->timer->elapsed() > g->limit;
    };
    if (avformat_open_input(&fmt, path.toUtf8().constData(), nullptr, nullptr) < 0)
        return none; // gibt fmt selbst frei
    AVCodecContext *dec = nullptr;
    AVFrame *frame = av_frame_alloc();
    AVPacket *pkt = av_packet_alloc();
    SwsContext *sws = nullptr;
    QList<Scores> scores;
    std::vector<unsigned char> gray(size_t(kW) * kH);

    do {
        if (avformat_find_stream_info(fmt, nullptr) < 0)
            break;
        const int index = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (index < 0)
            break;
        AVStream *stream = fmt->streams[index];
        // Stereo High (128) / Multiview High (118): zwei Ansichten in einem Strom. Das Bild der
        // Basisansicht ist gewöhnliches 2D – hier gibt es nichts zu vergleichen. In MPEG-TS
        // (3D-Camcorder, zusammengeführte Blu-ray-Ströme) nennt FFmpeg nur das Profil der
        // Basisansicht; dort verraten die NAL-Einheiten der zweiten Ansicht den Strom.
        const bool mvcStream = stream->codecpar->codec_id == AV_CODEC_ID_H264
                               && (stream->codecpar->profile == 128 || stream->codecpar->profile == 118
                                   || hasMvcNalUnits(fmt, index));
        if (mvcStream) {
            if (mvc)
                *mvc = true;
            if (width)
                *width = stream->codecpar->width;
            if (height)
                *height = stream->codecpar->height;
            break;
        }
        const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (!codec)
            break;
        dec = avcodec_alloc_context3(codec);
        if (!dec || avcodec_parameters_to_context(dec, stream->codecpar) < 0)
            break;
        dec->thread_count = std::clamp(QThread::idealThreadCount(), 1, 4);
        if (avcodec_open2(dec, codec, nullptr) < 0)
            break;
        // Größe, wie sie gezeigt wird (nicht quadratische Bildpunkte eingerechnet): danach
        // richtet sich, ob eine Hälfte gestaucht ist
        const AVRational sar = av_guess_sample_aspect_ratio(fmt, stream, nullptr);
        if (width)
            *width = sar.num > 0 && sar.den > 0 ? int(av_rescale(stream->codecpar->width, sar.num, sar.den)) : stream->codecpar->width;
        if (height)
            *height = stream->codecpar->height;

        const bool seekable = fmt->duration > 0 && fmt->pb && (fmt->pb->seekable & AVIO_SEEKABLE_NORMAL);
        for (int n = 0; n < kFrames && !cancelled() && timer.elapsed() < kBudgetMs; ++n) {
            if (seekable) {
                // 8 % … 86 % der Laufzeit: Vor- und Abspann meiden
                const int64_t ts = (fmt->start_time != AV_NOPTS_VALUE ? fmt->start_time : 0)
                                   + int64_t(double(fmt->duration) * (0.08 + 0.13 * n));
                if (av_seek_frame(fmt, -1, ts, AVSEEK_FLAG_BACKWARD) < 0)
                    break;
                avcodec_flush_buffers(dec);
            }
            // bis zum ersten Bild dekodieren; ohne Sprungmöglichkeit einige Bilder weiter
            int packets = 0, skip = seekable ? 0 : 20;
            bool got = false, end = false;
            while (!got && !end && packets < 600 && !cancelled() && timer.elapsed() < kBudgetMs) {
                const int r = av_read_frame(fmt, pkt);
                if (r < 0) {
                    avcodec_send_packet(dec, nullptr);
                    end = true;
                } else if (pkt->stream_index == index) {
                    ++packets;
                    avcodec_send_packet(dec, pkt);
                }
                if (r >= 0)
                    av_packet_unref(pkt);
                while (avcodec_receive_frame(dec, frame) >= 0) {
                    if (!got && skip-- <= 0 && frame->width > 0 && frame->height > 0) {
                        sws = sws_getCachedContext(sws, frame->width, frame->height, AVPixelFormat(frame->format), kW, kH,
                                                   AV_PIX_FMT_GRAY8, SWS_AREA, nullptr, nullptr, nullptr);
                        if (sws) {
                            uint8_t *dst[4] = {gray.data(), nullptr, nullptr, nullptr};
                            int dstStride[4] = {kW, 0, 0, 0};
                            sws_scale(sws, frame->data, frame->linesize, 0, frame->height, dst, dstStride);
                            scores << compare(gray.data(), kW, kH, kW);
                            got = true;
                            // Früh aufhören, wenn die Sache klar ist: drei auswertbare Bilder ohne jede
                            // Ähnlichkeit (2D – der häufigste Fall) oder vier mit derselben Anordnung
                            int valid = 0, flat = 0, sbs = 0, tab = 0;
                            for (const Scores &s : std::as_const(scores)) {
                                if (s.sideBySide < -1 && s.topBottom < -1)
                                    continue;
                                ++valid;
                                flat += s.sideBySide < 0.3 && s.topBottom < 0.3;
                                sbs += s.sideBySide >= 0.75 && s.topBottom < 0.4;
                                tab += s.topBottom >= 0.75 && s.sideBySide < 0.4;
                            }
                            if ((valid >= 3 && flat == valid) || (valid >= 4 && (sbs == valid || tab == valid)))
                                n = kFrames;
                        }
                    }
                    av_frame_unref(frame);
                }
            }
            if (end) {
                if (!seekable)
                    break;
                avcodec_flush_buffers(dec);
            }
        }
    } while (false);

    sws_freeContext(sws);
    av_packet_free(&pkt);
    av_frame_free(&frame);
    avcodec_free_context(&dec);
    avformat_close_input(&fmt);
    if (scoresOut)
        *scoresOut = scores;
    if (cancelled())
        return none;
    return fromScores(scores, hint3d, confidence);
}

Result detectFile(const QString &path, const std::atomic_bool *cancel)
{
    Result result;
    const Hint name = fromName(QFileInfo(path).fileName());
    int w = 0, h = 0;
    double confidence = 0;
    QList<Scores> scores;
    const Hint picture = analyzeFile(path, name.is3d, cancel, &confidence, &w, &h, &scores, &result.mvc);
    if (result.mvc) {
        result.source = QStringLiteral("mvc");
        return result;
    }
    int valid = 0;
    for (const Scores &s : scores)
        valid += s.sideBySide >= -1 || s.topBottom >= -1;
    // "SBS" allein kann auch ein Sendername sein: ohne "3D" oder Größenangabe ("H-SBS") zählt
    // die Kennung nur, wenn das Bild nicht dagegen spricht
    const bool nameLayout = (name.layout == SideBySide || name.layout == TopBottom)
                            && (name.tagged3d || name.half >= 0 || valid < 3);

    if (picture.decided() && (!nameLayout || picture.layout == name.layout || confidence >= 0.8)) {
        Hint hint = picture;
        if (nameLayout && picture.layout == name.layout)
            hint.half = name.half;
        hint.rightFirst = name.rightFirst;
        result.format = format(hint, w, h);
        result.source = QStringLiteral("picture");
        result.confidence = confidence;
    } else if (nameLayout) {
        result.format = format(name, w, h);
        result.source = QStringLiteral("name");
        result.confidence = 0.7;
    } else if (name.is3d && scores.isEmpty()) {
        // "3D" im Namen, das Bild ließ sich nicht prüfen: nach der Bildgröße
        const Hint size = fromSize(w, h);
        if (size.decided()) {
            result.format = format(size, w, h);
            result.source = QStringLiteral("size");
            result.confidence = 0.4;
        }
    }
    return result;
}

} // namespace StereoDetect
