#include "ProfileManager.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>

namespace {

QString userProfilesFile()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/profiles.json");
}

QVariantList readArray(const QString &file)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).array().toVariantList();
}

QString yesNo(bool b) { return b ? QStringLiteral("yes") : QStringLiteral("no"); }

} // namespace

ProfileManager::ProfileManager(QObject *parent)
    : QObject(parent)
{
    load();
}

QVariantMap ProfileManager::defaults() const
{
    return {
        {"id", QString()},
        {"name", QStringLiteral("Neues Profil")},
        {"description", QString()},
        {"builtin", false},
        // Ausgabegerät
        {"output", QString()},        // leer = Hauptbildschirm
        {"playerWindow", QStringLiteral("auto")}, // auto | native | embedded
        {"fullscreen", false},
        {"ontop", false},
        {"border", true},
        {"matchRefreshRate", false},
        {"osHdrSwitch", false},
        // Video
        {"vo", QStringLiteral("gpu-next")},
        {"gpuApi", QStringLiteral("auto")},
        {"hwdec", QStringLiteral("auto-safe")},
        {"quality", QStringLiteral("balanced")}, // fast | balanced | high
        {"hdr", QStringLiteral("auto")},         // auto | passthrough | tonemap
        {"targetPeak", 0},                       // 0 = auto (nits)
        {"targetPrim", QStringLiteral("auto")},
        {"targetTrc", QStringLiteral("auto")},
        {"toneMapping", QStringLiteral("auto")},
        {"videoSync", QStringLiteral("display-resample")},
        {"interpolation", false},
        {"deband", true},
        // Kalibrierung / Kinoqualität
        {"iccAuto", false},          // ICC-Profil des Monitors vom System
        {"iccProfile", QString()},   // eigenes ICC-Profil (Datei)
        {"targetLut", QString()},    // 3D-LUT (.cube) der Anzeigekalibrierung
        {"targetContrast", 0},       // 0 = automatisch, sonst z. B. 2000 (Projektor)
        {"dither", QStringLiteral("auto")}, // auto | error-diffusion | ordered | no
        {"ditherDepth", QStringLiteral("auto")},
        {"shaders", QString()},      // GLSL-Shader, je Zeile ein Pfad
        // 3D
        {"stereoOut", QStringLiteral("none")},
        {"subtitleDepth", 0},
        // Audio
        {"audioDevice", QStringLiteral("auto")},
        {"audioPassthrough", QVariantList{}},
        {"audioExclusive", false},
        {"audioChannels", QStringLiteral("auto-safe")},
        // Freie mpv-Optionen (Experten)
        {"extra", QVariantMap{}},
    };
}

void ProfileManager::load()
{
    m_builtin.clear();
    for (const auto &v : readArray(QStringLiteral(":/qt/qml/Lumen/profiles/presets.json"))) {
        QVariantMap p = defaults();
        const QVariantMap src = v.toMap();
        for (auto it = src.cbegin(); it != src.cend(); ++it)
            p.insert(it.key(), it.value());
        p["builtin"] = true;
        m_builtin.append(p);
    }

    // Nutzerprofile: überschreiben gleichnamige IDs der Vorlagen
    QVariantList user = readArray(userProfilesFile());
    m_profiles = m_builtin;
    for (const auto &v : user) {
        QVariantMap p = defaults();
        const QVariantMap src = v.toMap();
        for (auto it = src.cbegin(); it != src.cend(); ++it)
            p.insert(it.key(), it.value());
        bool replaced = false;
        for (auto &b : m_profiles) {
            if (b.toMap().value("id") == p.value("id")) {
                p["builtin"] = true;
                p["modified"] = true;
                b = p;
                replaced = true;
                break;
            }
        }
        if (!replaced)
            m_profiles.append(p);
    }

    QSettings s;
    m_currentId = s.value(QStringLiteral("profile/current"), QStringLiteral("desktop")).toString();
    if (profileById(m_currentId).isEmpty() && !m_profiles.isEmpty())
        m_currentId = m_profiles.first().toMap().value("id").toString();
}

void ProfileManager::persist() const
{
    QJsonArray arr;
    for (const auto &v : m_profiles) {
        QVariantMap p = v.toMap();
        if (p.value("builtin").toBool() && !p.value("modified").toBool())
            continue;
        p.remove("builtin");
        p.remove("modified");
        arr.append(QJsonObject::fromVariantMap(p));
    }
    QFile f(userProfilesFile());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
}

QString ProfileManager::configPath() const
{
    return userProfilesFile();
}

void ProfileManager::setCurrentId(const QString &id)
{
    if (id == m_currentId || profileById(id).isEmpty())
        return;
    m_currentId = id;
    QSettings().setValue(QStringLiteral("profile/current"), id);
    emit currentProfileChanged();
}

QVariantMap ProfileManager::currentProfile() const
{
    return profileById(m_currentId);
}

QVariantMap ProfileManager::profileById(const QString &id) const
{
    for (const auto &v : m_profiles) {
        const QVariantMap p = v.toMap();
        if (p.value("id").toString() == id)
            return p;
    }
    return {};
}

QString ProfileManager::saveProfile(const QVariantMap &profile)
{
    QVariantMap p = defaults();
    for (auto it = profile.cbegin(); it != profile.cend(); ++it)
        p.insert(it.key(), it.value());
    if (p.value("id").toString().isEmpty())
        p["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);

    const QString id = p.value("id").toString();
    bool replaced = false;
    for (auto &v : m_profiles) {
        const QVariantMap old = v.toMap();
        if (old.value("id").toString() == id) {
            p["builtin"] = old.value("builtin");
            p["modified"] = old.value("builtin").toBool();
            v = p;
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        p["builtin"] = false;
        m_profiles.append(p);
    }
    persist();
    emit profilesChanged();
    if (id == m_currentId)
        emit currentProfileChanged();
    return id;
}

QString ProfileManager::duplicateProfile(const QString &id)
{
    QVariantMap p = profileById(id);
    if (p.isEmpty())
        return {};
    p["id"] = QString();
    p["builtin"] = false;
    p.remove("modified");
    p["name"] = p.value("name").toString() + QStringLiteral(" (Kopie)");
    return saveProfile(p);
}

void ProfileManager::deleteProfile(const QString &id)
{
    for (int i = 0; i < m_profiles.size(); ++i) {
        const QVariantMap p = m_profiles[i].toMap();
        if (p.value("id").toString() != id)
            continue;
        if (p.value("builtin").toBool())
            return; // Vorlagen können nur zurückgesetzt werden
        m_profiles.removeAt(i);
        persist();
        emit profilesChanged();
        if (id == m_currentId)
            setCurrentId(m_profiles.first().toMap().value("id").toString());
        return;
    }
}

void ProfileManager::resetBuiltin(const QString &id)
{
    for (const auto &b : m_builtin) {
        if (b.toMap().value("id").toString() != id)
            continue;
        for (auto &v : m_profiles) {
            if (v.toMap().value("id").toString() == id)
                v = b;
        }
        persist();
        emit profilesChanged();
        if (id == m_currentId)
            emit currentProfileChanged();
        return;
    }
}

QVariantMap ProfileManager::mpvOptions(const QVariantMap &profile) const
{
    return toMpvOptions(profile);
}

QVariantMap ProfileManager::toMpvOptions(const QVariantMap &p)
{
    QVariantMap o;

    // --- Fenster -----------------------------------------------------------
    o["fullscreen"] = yesNo(p.value("fullscreen").toBool());
    o["ontop"] = yesNo(p.value("ontop").toBool());
    o["border"] = yesNo(p.value("border", true).toBool());

    // --- Renderer ----------------------------------------------------------
    o["vo"] = p.value("vo", "gpu-next").toString();
    o["gpu-api"] = p.value("gpuApi", "auto").toString();
    o["hwdec"] = p.value("hwdec", "auto-safe").toString();

    const QString q = p.value("quality", "balanced").toString();
    if (q == QLatin1String("fast")) {
        o["scale"] = "bilinear";
        o["cscale"] = "bilinear";
        o["dscale"] = "bilinear";
        o["correct-downscaling"] = "no";
        o["linear-downscaling"] = "no";
        o["sigmoid-upscaling"] = "no";
        o["hdr-compute-peak"] = "no";
    } else if (q == QLatin1String("reference")) {
        // Referenz: EWA Lanczos 4 (schärfste Rekonstruktion), Error Diffusion,
        // dynamische HDR-Spitzenmessung mit Kontrastrückgewinnung
        o["scale"] = "ewa_lanczos4sharpest";
        o["cscale"] = "ewa_lanczossharp";
        o["dscale"] = "mitchell";
        o["correct-downscaling"] = "yes";
        o["linear-downscaling"] = "yes";
        o["sigmoid-upscaling"] = "yes";
        o["hdr-compute-peak"] = "yes";
        o["hdr-peak-percentile"] = "99.995";
        o["hdr-contrast-recovery"] = "0.30";
        o["dither"] = "error-diffusion";
        o["error-diffusion"] = "sierra-lite";
        o["deband-iterations"] = "2";
    } else if (q == QLatin1String("high")) {
        o["scale"] = "ewa_lanczossharp";
        o["cscale"] = "ewa_lanczossharp";
        o["dscale"] = "mitchell";
        o["correct-downscaling"] = "yes";
        o["linear-downscaling"] = "yes";
        o["sigmoid-upscaling"] = "yes";
        o["hdr-compute-peak"] = "yes";
    } else {
        o["scale"] = "spline36";
        o["cscale"] = "spline36";
        o["dscale"] = "mitchell";
        o["correct-downscaling"] = "yes";
        o["linear-downscaling"] = "yes";
        o["sigmoid-upscaling"] = "yes";
        o["hdr-compute-peak"] = "yes";
    }
    o["deband"] = yesNo(p.value("deband", true).toBool());
    o["dither-depth"] = p.value("ditherDepth", "auto").toString();
    const QString dither = p.value("dither", "auto").toString();
    if (dither != QLatin1String("auto"))
        o["dither"] = dither;

    // --- Kalibrierung ------------------------------------------------------
    o["icc-profile-auto"] = yesNo(p.value("iccAuto").toBool() && p.value("iccProfile").toString().isEmpty());
    o["icc-profile"] = p.value("iccProfile").toString();
    o["target-lut"] = p.value("targetLut").toString();
    const int contrast = p.value("targetContrast").toInt();
    o["target-contrast"] = contrast > 0 ? QString::number(contrast) : QStringLiteral("auto");
    QStringList shaders;
    for (const QString &line : p.value("shaders").toString().split(QLatin1Char('\n'), Qt::SkipEmptyParts))
        if (!line.trimmed().isEmpty())
            shaders << line.trimmed();
    o["glsl-shaders"] = shaders;

    // --- HDR ---------------------------------------------------------------
    const QString hdr = p.value("hdr", "auto").toString();
    const int peak = p.value("targetPeak").toInt();
    o["target-colorspace-hint"] = yesNo(hdr != QLatin1String("tonemap"));
    o["target-prim"] = p.value("targetPrim", "auto").toString();
    o["target-trc"] = p.value("targetTrc", "auto").toString();
    o["target-peak"] = peak > 0 ? QString::number(peak) : QStringLiteral("auto");
    o["tone-mapping"] = p.value("toneMapping", "auto").toString();
    o["gamut-mapping-mode"] = "auto";

    // --- Timing ------------------------------------------------------------
    o["video-sync"] = p.value("videoSync", "display-resample").toString();
    o["interpolation"] = yesNo(p.value("interpolation").toBool());
    o["tscale"] = "oversample";

    // 3D: "stereoOut" (was das Gerät erwartet) wird vom MpvController mit dem
    // zur Laufzeit gewählten Quellformat zu einem stereo3d-Filter kombiniert.

    // --- Audio -------------------------------------------------------------
    o["audio-device"] = p.value("audioDevice", "auto").toString();
    QStringList spdif;
    for (const auto &c : p.value("audioPassthrough").toList())
        spdif << c.toString();
    o["audio-spdif"] = spdif.join(QLatin1Char(','));
    o["audio-exclusive"] = yesNo(p.value("audioExclusive").toBool());
    o["audio-channels"] = p.value("audioChannels", "auto-safe").toString();

    // --- Experten ----------------------------------------------------------
    const QVariantMap extra = p.value("extra").toMap();
    for (auto it = extra.cbegin(); it != extra.cend(); ++it)
        o[it.key()] = it.value();
    return o;
}
