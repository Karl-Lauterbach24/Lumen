#include "Tuning.h"

#include <QRegularExpression>

#include <algorithm>

namespace Tuning {

GpuClass classifyGpu(const QString &renderer, const QString &vendor)
{
    const QString r = renderer.toLower();
    const QString v = vendor.toLower();
    if (r.isEmpty() && v.isEmpty())
        return GpuUnknown;
    auto has = [&](std::initializer_list<const char *> words) {
        for (const char *w : words) {
            if (r.contains(QLatin1String(w)))
                return true;
        }
        return false;
    };
    // Die CPU rechnet das Bild
    if (has({"llvmpipe", "softpipe", "lavapipe", "swrast", "swiftshader", "software", "microsoft basic render", "gdi generic"}))
        return GpuSoftware;
    if (has({"nvidia", "geforce", "quadro", "rtx", "gtx"}) || v.contains(QLatin1String("nvidia")))
        return GpuDiscrete;
    // Apple Silicon: integriert, aber so schnell wie eine eigenständige Karte
    if (r.contains(QRegularExpression(QStringLiteral("apple m\\d"))))
        return GpuDiscrete;
    if (r.contains(QLatin1String("radeon")) || v.contains(QLatin1String("amd")) || v.contains(QLatin1String("ati "))) {
        // "Radeon RX 6800", "Radeon Pro W6600", "Radeon HD 7970", "FirePro" sind Karten;
        // "Radeon Graphics", "Radeon Vega 8", "Radeon 780M" sitzen im Prozessor
        if (r.contains(QRegularExpression(QStringLiteral("radeon\\s*(\\(tm\\)\\s*)?(rx|pro|hd|r[579])\\b"))) || r.contains(QLatin1String("firepro")))
            return GpuDiscrete;
        return GpuIntegrated;
    }
    if (r.contains(QLatin1String("intel")) || v.contains(QLatin1String("intel"))) {
        // Arc A/B-Serie sind Karten ("Arc(TM) A770"), "Arc Graphics" ohne Nummer sitzt im Prozessor
        if (r.contains(QRegularExpression(QStringLiteral("arc(\\(tm\\))?\\s*(pro\\s*)?[ab]\\d{3}"))))
            return GpuDiscrete;
        return GpuIntegrated;
    }
    if (has({"apple", "adreno", "mali", "powervr", "videocore", "virgl", "svga3d", "parallels", "vmware"}))
        return GpuIntegrated;
    return GpuUnknown;
}

QString gpuClassName(GpuClass c)
{
    switch (c) {
    case GpuSoftware: return QStringLiteral("software");
    case GpuIntegrated: return QStringLiteral("integrated");
    case GpuDiscrete: return QStringLiteral("discrete");
    default: return QStringLiteral("unknown");
    }
}

QStringList qualityTiers()
{
    return {QStringLiteral("reference"), QStringLiteral("high"), QStringLiteral("balanced"), QStringLiteral("fast")};
}

QString qualityFor(const Hardware &hw)
{
    switch (hw.gpu) {
    case GpuSoftware: return QStringLiteral("fast");
    case GpuDiscrete: return QStringLiteral("high");
    default: return QStringLiteral("balanced");
    }
}

QString platformHwdec(bool copy)
{
#if defined(Q_OS_MACOS)
    // Nur VideoToolbox kommt in Frage; mpvs Automatik probiert sonst auch Vulkan und meldet dessen Fehlen
    return copy ? QStringLiteral("videotoolbox-copy") : QStringLiteral("videotoolbox");
#else
    return copy ? QStringLiteral("auto-copy-safe") : QStringLiteral("auto-safe");
#endif
}

static QString baseQuality(const QVariantMap &profile, const Hardware &hw)
{
    const QString q = profile.value("quality", "auto").toString();
    if (q == QLatin1String("auto") || !qualityTiers().contains(q))
        return qualityFor(hw);
    return q;
}

int maxRenderLevel(const QVariantMap &profile, const Hardware &hw)
{
    const QStringList tiers = qualityTiers();
    // jede Stufe unterhalb der Ausgangsstufe, dazu "minimal"
    return int(tiers.size()) - 1 - int(tiers.indexOf(baseQuality(profile, hw))) + 1;
}

QVariantMap resolve(const QVariantMap &profile, const Hardware &hw, const Context &context)
{
    QVariantMap p = profile;
    const QStringList tiers = qualityTiers();
    const int index = int(tiers.indexOf(baseQuality(profile, hw))) + std::max(0, context.renderLevel);
    p["quality"] = tiers.at(std::min(index, int(tiers.size()) - 1));
    // unterhalb von "fast": auch Debanding, Dithering und Zwischenbilder weglassen
    p["tuningMinimal"] = index > int(tiers.size()) - 1;

    if (p.value("hwdec", "smart").toString() == QLatin1String("smart")) {
        if (context.mvc)
            p["hwdec"] = QStringLiteral("no");
        else // Bilder im Arbeitsspeicher, wenn dort gerechnet wird: kein Rückweg von der Grafikkarte
            p["hwdec"] = platformHwdec(context.cpuFilter || hw.gpu == GpuSoftware);
    }
    return p;
}

QVariantMap reliefOptions(const QVariantMap &profile, const Context &context)
{
    QVariantMap o;
    // Decoder (nur Software): erst Abkürzungen ohne sichtbare Folgen, dann der Deblocking-Filter,
    // zuletzt Bilder auslassen, damit der Ton nicht davonläuft
    o["vd-lavc-fast"] = context.decodeLevel >= 1 ? "yes" : "no";
    o["vd-lavc-skiploopfilter"] = context.decodeLevel >= 2 ? "all" : context.decodeLevel >= 1 ? "nonref" : "default";
    o["framedrop"] = context.decodeLevel >= 3 ? "decoder+vo" : "vo";

    // Renderer unterhalb von "fast" (resolve() hat das im Profil vermerkt)
    const bool minimal = profile.value("tuningMinimal").toBool();
    if (minimal) {
        o["deband"] = "no";
        o["dither"] = "no";
        o["interpolation"] = "no";
        o["hdr-compute-peak"] = "no";
    }
    if (context.decodeLevel >= 3 || minimal)
        o["video-sync"] = "audio"; // Abgleich mit dem Bildschirm setzt voraus, dass jedes Bild rechtzeitig fertig ist
    return o;
}

// --------------------------------------------------------------------------

void Governor::reset()
{
    m_window.clear();
    m_holdUntil = 0;
    m_render = m_decode = 0;
}

void Governor::hold(double seconds, double now)
{
    m_holdUntil = std::max(m_holdUntil, now + seconds);
    m_window.clear();
}

void Governor::setLimits(int maxRender, int maxDecode)
{
    m_maxRender = std::max(0, maxRender);
    m_maxDecode = std::max(0, maxDecode);
}

void Governor::setLevels(int render, int decode)
{
    m_render = std::clamp(render, 0, m_maxRender);
    m_decode = std::clamp(decode, 0, m_maxDecode);
}

Governor::Action Governor::feed(const Sample &s)
{
    if (!s.steady || s.time < m_holdUntil) {
        m_window.clear();
        return None;
    }
    if (!m_window.isEmpty()) {
        const Sample &last = m_window.last();
        // Zähler beginnen neu (andere Datei, neuer Decoder)
        if (s.voDrops < last.voDrops || s.decoderDrops < last.decoderDrops || s.delayed < last.delayed)
            m_window.clear();
    }
    m_window.append(s);
    while (m_window.size() > 7)
        m_window.removeFirst();
    const Sample &first = m_window.first();
    const double span = s.time - first.time;
    const double fps = s.fps > 1 ? s.fps : 24.0;
    const double frames = fps * span;
    const qint64 lost = (s.voDrops - first.voDrops) + (s.decoderDrops - first.decoderDrops);
    const qint64 delayed = s.delayed - first.delayed;
    // Geht mehr als ein Viertel verloren, genügen zwei Sekunden; sonst ab 6 % der Bilder
    // (mindestens 8) über dreieinhalb Sekunden: einzelne Aussetzer zählen nicht
    const bool severe = span >= 2 && double(lost + delayed) >= std::max(12.0, 0.25 * frames);
    if (!severe && (span < 3.5 || double(lost + delayed) < std::max(8.0, 0.06 * frames)))
        return None;

    const bool canRender = m_render < m_maxRender;
    const bool canDecode = s.software && m_decode < m_maxDecode;
    Action action = None;
    if (s.renderMs >= 0) {
        // Der Renderer misst sich selbst: braucht er mehr als 60 % der Bilddauer, liegt es an ihm
        const bool renderBound = s.renderMs > 0.6 * 1000.0 / fps || delayed > lost;
        if (renderBound)
            action = canRender ? LowerRender : None;
        else
            action = canDecode ? RelieveDecoder : None;
    } else {
        // unbekannt: abwechselnd. Der Renderer zuerst (kostet am wenigsten Bildqualität) – außer
        // der Software-Decoder hat 4K und mehr zu leisten: dann liegt es fast immer an ihm
        const bool heavyDecode = canDecode && s.pixelRate >= 3840.0 * 2160 * 24;
        const int renderTurn = m_render + (heavyDecode ? 1 : 0);
        if (canRender && (renderTurn <= m_decode || !canDecode))
            action = LowerRender;
        else if (canDecode)
            action = RelieveDecoder;
    }
    if (action == LowerRender)
        ++m_render;
    else if (action == RelieveDecoder)
        ++m_decode;
    if (action != None)
        hold(severe ? 3 : 6, s.time);
    else
        m_window.clear(); // nichts mehr zu tun: neu zählen
    return action;
}

QString loadClass(int width, int height, double fps)
{
    const qint64 px = qint64(width) * height;
    const char *size = px <= 1280 * 720 * 11 / 10 ? "hd" : px <= 1920 * 1088 * 11 / 10 ? "fhd" : px <= 2560 * 1440 * 11 / 10 ? "qhd"
                       : px <= qint64(4096) * 2304 ? "uhd" : "8k";
    return QStringLiteral("%1-%2").arg(QLatin1String(size), fps > 50 ? QStringLiteral("hfr") : QStringLiteral("std"));
}

} // namespace Tuning
