#include "DisplayManager.h"
#include "Tr.h"

#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QScreen>
#include <QStandardPaths>
#include <cmath>
#include <vector>

#ifdef Q_OS_WIN
#include <windows.h>
#endif
#ifdef Q_OS_MACOS
#include <ApplicationServices/ApplicationServices.h>
#endif

namespace {

// Bildraten gelten als passend, wenn sie ein ganzzahliges Vielfaches sind
// (±0,05 % – trennt 23,976 sauber von 24,000).
constexpr double kRateTolerance = 0.0005;

#ifdef Q_OS_LINUX
QString run(const QString &program, const QStringList &args, int timeoutMs = 3000)
{
    QProcess p;
    p.start(program, args);
    if (!p.waitForFinished(timeoutMs))
        return {};
    return QString::fromLocal8Bit(p.readAllStandardOutput());
}
#endif

#ifdef Q_OS_WIN
struct PathInfo
{
    QString friendlyName;
    LUID adapterId{};
    UINT32 targetId = 0;
    bool hdrSupported = false;
    bool hdrEnabled = false;
};

// GDI-Gerätename ("\\.\DISPLAY1") -> Ziel-Infos aus der DisplayConfig-API
QHash<QString, PathInfo> queryPaths()
{
    QHash<QString, PathInfo> result;
    UINT32 numPaths = 0, numModes = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &numPaths, &numModes) != ERROR_SUCCESS)
        return result;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(numPaths);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(numModes);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &numPaths, paths.data(), &numModes, modes.data(), nullptr) != ERROR_SUCCESS)
        return result;

    for (UINT32 i = 0; i < numPaths; ++i) {
        const auto &p = paths[i];
        DISPLAYCONFIG_SOURCE_DEVICE_NAME src{};
        src.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        src.header.size = sizeof(src);
        src.header.adapterId = p.sourceInfo.adapterId;
        src.header.id = p.sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&src.header) != ERROR_SUCCESS)
            continue;

        PathInfo info;
        info.adapterId = p.targetInfo.adapterId;
        info.targetId = p.targetInfo.id;

        DISPLAYCONFIG_TARGET_DEVICE_NAME tgt{};
        tgt.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        tgt.header.size = sizeof(tgt);
        tgt.header.adapterId = p.targetInfo.adapterId;
        tgt.header.id = p.targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&tgt.header) == ERROR_SUCCESS)
            info.friendlyName = QString::fromWCharArray(tgt.monitorFriendlyDeviceName);

        DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO color{};
        color.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
        color.header.size = sizeof(color);
        color.header.adapterId = p.targetInfo.adapterId;
        color.header.id = p.targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&color.header) == ERROR_SUCCESS) {
            info.hdrSupported = color.advancedColorSupported;
            info.hdrEnabled = color.advancedColorEnabled;
        }
        result.insert(QString::fromWCharArray(src.viewGdiDeviceName), info);
    }
    return result;
}

struct MonitorEnum
{
    QVariantList list;
    QHash<QString, PathInfo> paths;
};

BOOL CALLBACK monitorProc(HMONITOR mon, HDC, LPRECT, LPARAM param)
{
    auto *e = reinterpret_cast<MonitorEnum *>(param);
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(mon, &mi))
        return TRUE;

    const QString device = QString::fromWCharArray(mi.szDevice);
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm);

    const PathInfo pi = e->paths.value(device);
    QVariantMap o;
    o[QStringLiteral("index")] = e->list.size(); // == mpv --screen Index
    o[QStringLiteral("id")] = device;
    o[QStringLiteral("name")] = pi.friendlyName.isEmpty() ? device : pi.friendlyName;
    o[QStringLiteral("x")] = int(mi.rcMonitor.left);
    o[QStringLiteral("y")] = int(mi.rcMonitor.top);
    o[QStringLiteral("width")] = int(dm.dmPelsWidth);
    o[QStringLiteral("height")] = int(dm.dmPelsHeight);
    o[QStringLiteral("refresh")] = int(dm.dmDisplayFrequency);
    o[QStringLiteral("primary")] = bool(mi.dwFlags & MONITORINFOF_PRIMARY);
    o[QStringLiteral("hdrSupported")] = pi.hdrSupported;
    o[QStringLiteral("hdrEnabled")] = pi.hdrEnabled;
    e->list.append(o);
    return TRUE;
}

// Windows meldet 23.976 Hz als "23", 59.94 als "59" usw.
double realRate(int hz)
{
    switch (hz) {
    case 23: case 29: case 47: case 59: case 71: case 119: case 143: case 239:
        return (hz + 1) * 1000.0 / 1001.0;
    default:
        return hz;
    }
}
#endif

#ifdef Q_OS_LINUX
// --- xrandr (X11) ---------------------------------------------------------
struct XrandrOutput
{
    QString currentMode;   // "1920x1080"
    QString currentRate;   // "60.00"
    QString mode;          // gewünschte (oder aktuelle) Auflösung
    QStringList rates;     // Raten dieser Auflösung
};

// wantedMode leer = aktuelle Auflösung
XrandrOutput xrandrQuery(const QString &name, const QString &wantedMode = QString())
{
    XrandrOutput out;
    const QStringList lines = run(QStringLiteral("xrandr"), {QStringLiteral("--query")}).split(QLatin1Char('\n'));
    bool inOutput = false;
    QHash<QString, QStringList> modeRates;
    for (const QString &line : lines) {
        if (!line.startsWith(QLatin1Char(' '))) {
            inOutput = line.startsWith(name + QLatin1Char(' '));
            continue;
        }
        if (!inOutput)
            continue;
        // "   1920x1080     60.00*+  50.00    23.98"
        const QStringList tok = line.simplified().split(QLatin1Char(' '));
        QStringList rates;
        for (int i = 1; i < tok.size(); ++i) {
            QString r = tok[i];
            const bool current = r.contains(QLatin1Char('*'));
            r.remove(QLatin1Char('*')).remove(QLatin1Char('+'));
            if (r.isEmpty())
                continue;
            rates << r;
            if (current) {
                out.currentMode = tok.value(0);
                out.currentRate = r;
            }
        }
        modeRates[tok.value(0)] += rates;
    }
    out.mode = wantedMode.isEmpty() ? out.currentMode : wantedMode;
    out.rates = modeRates.value(out.mode);
    return out;
}

// --- kscreen-doctor (KDE Plasma, X11 + Wayland) ---------------------------
QJsonObject kscreenOutput(const QString &name)
{
    const QJsonObject root = QJsonDocument::fromJson(run(QStringLiteral("kscreen-doctor"), {QStringLiteral("-j")}).toUtf8()).object();
    for (const auto &v : root.value(QStringLiteral("outputs")).toArray()) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("name")).toString() == name)
            return o;
    }
    return {};
}
#endif

#ifdef Q_OS_MACOS
CGDirectDisplayID displayForOrigin(const QPoint &origin)
{
    CGDirectDisplayID ids[16];
    uint32_t n = 0;
    if (CGGetActiveDisplayList(16, ids, &n) != kCGErrorSuccess)
        return kCGNullDirectDisplay;
    for (uint32_t i = 0; i < n; ++i) {
        const CGRect b = CGDisplayBounds(ids[i]);
        if (QPoint(int(b.origin.x), int(b.origin.y)) == origin)
            return ids[i];
    }
    return kCGNullDirectDisplay;
}
#endif

} // namespace

int DisplayManager::pickRate(const QList<double> &rates, double fps)
{
    int best = -1, bestMultiple = 1000;
    for (int i = 0; i < rates.size(); ++i) {
        const double ratio = rates[i] / fps;
        const int multiple = int(std::lround(ratio));
        if (multiple < 1 || std::abs(ratio - multiple) > kRateTolerance * multiple)
            continue;
        if (multiple < bestMultiple) {
            bestMultiple = multiple;
            best = i;
        }
    }
    return best;
}

DisplayManager::DisplayManager(QObject *parent)
    : QObject(parent)
{
#ifdef Q_OS_WIN
    m_backend = QStringLiteral("windows");
    m_canRefresh = m_canHdr = true;
#elif defined(Q_OS_MACOS)
    m_backend = QStringLiteral("coregraphics");
    m_canRefresh = true;
#elif defined(Q_OS_LINUX)
    const bool kscreen = !QStandardPaths::findExecutable(QStringLiteral("kscreen-doctor")).isEmpty();
    const bool x11 = QGuiApplication::platformName() == QLatin1String("xcb");
    if (kscreen) {
        m_backend = QStringLiteral("kscreen");
        m_canRefresh = m_canHdr = true;
    } else if (x11 && !QStandardPaths::findExecutable(QStringLiteral("xrandr")).isEmpty()) {
        m_backend = QStringLiteral("xrandr");
        m_canRefresh = true;
    } else {
        m_backend = QStringLiteral("none");
    }
#endif
    refresh();
    auto *app = qobject_cast<QGuiApplication *>(QGuiApplication::instance());
    if (app) {
        connect(app, &QGuiApplication::screenAdded, this, &DisplayManager::refresh);
        connect(app, &QGuiApplication::screenRemoved, this, &DisplayManager::refresh);
    }
}

DisplayManager::~DisplayManager()
{
    restoreAll();
}

void DisplayManager::refresh()
{
    QVariantList list;
#ifdef Q_OS_WIN
    MonitorEnum e;
    e.paths = queryPaths();
    EnumDisplayMonitors(nullptr, nullptr, monitorProc, reinterpret_cast<LPARAM>(&e));
    list = e.list;
#else
    const auto screens = QGuiApplication::screens();
    for (int i = 0; i < screens.size(); ++i) {
        QScreen *s = screens[i];
        const QSize px = s->size() * s->devicePixelRatio();
        QVariantMap o;
        o[QStringLiteral("index")] = i;
        o[QStringLiteral("id")] = s->name();
        o[QStringLiteral("name")] = s->model().isEmpty() ? s->name() : (s->manufacturer() + QLatin1Char(' ') + s->model()).trimmed();
        o[QStringLiteral("x")] = s->geometry().x();
        o[QStringLiteral("y")] = s->geometry().y();
        o[QStringLiteral("width")] = px.width();
        o[QStringLiteral("height")] = px.height();
        o[QStringLiteral("refresh")] = qRound(s->refreshRate());
        o[QStringLiteral("primary")] = (s == QGuiApplication::primaryScreen());
        o[QStringLiteral("hdrSupported")] = false;
        o[QStringLiteral("hdrEnabled")] = false;
#ifdef Q_OS_LINUX
        if (m_backend == QLatin1String("kscreen")) {
            const QJsonObject k = kscreenOutput(s->name());
            if (k.contains(QStringLiteral("hdr"))) {
                o[QStringLiteral("hdrSupported")] = true;
                o[QStringLiteral("hdrEnabled")] = k.value(QStringLiteral("hdr")).toBool();
            }
        }
#endif
        list.append(o);
    }
#endif
    for (auto &v : list) {
        QVariantMap o = v.toMap();
        o[QStringLiteral("label")] = QStringLiteral("%1 · %2×%3 @ %4 Hz%5")
                                         .arg(o.value("name").toString())
                                         .arg(o.value("width").toInt())
                                         .arg(o.value("height").toInt())
                                         .arg(o.value("refresh").toInt())
                                         .arg(o.value("hdrEnabled").toBool() ? QStringLiteral(" · HDR") : QString());
        v = o;
    }
    if (list != m_outputs) {
        m_outputs = list;
        emit outputsChanged();
    }
}

QVariantMap DisplayManager::output(const QString &outputId) const
{
    for (const auto &v : m_outputs) {
        const QVariantMap o = v.toMap();
        if (outputId.isEmpty() ? o.value("primary").toBool() : o.value("id").toString() == outputId)
            return o;
    }
    // Fallback: nach Anzeigenamen suchen (z. B. Profil von anderem Rechner)
    for (const auto &v : m_outputs) {
        const QVariantMap o = v.toMap();
        if (o.value("name").toString() == outputId)
            return o;
    }
    return {};
}

QVariantMap DisplayManager::mpvScreenOptions(const QString &outputId) const
{
    QVariantMap opts;
    if (outputId.isEmpty())
        return opts;
    const QVariantMap o = output(outputId);
    if (o.isEmpty())
        return opts;
#ifdef Q_OS_WIN
    opts[QStringLiteral("screen")] = o.value("index").toInt();
    opts[QStringLiteral("fs-screen")] = o.value("index").toInt();
#else
    opts[QStringLiteral("screen-name")] = o.value("id").toString();
    opts[QStringLiteral("fs-screen-name")] = o.value("id").toString();
#endif
    return opts;
}

bool DisplayManager::matchRefreshRate(const QString &outputId, double fps, QString *info, const QSize &size)
{
    if (!m_canRefresh || fps < 10 || fps > 200)
        return false;
    const QVariantMap o = output(outputId);
    if (o.isEmpty())
        return false;
    const QString id = o.value("id").toString();
    auto noMode = [&] {
        if (info) {
            *info = size.isValid()
                ? LTR("Kein %1×%2-Modus mit %3 fps – im Grafiktreiber als benutzerdefinierte Auflösung anlegen")
                      .arg(size.width()).arg(size.height()).arg(fps, 0, 'f', 3)
                : LTR("Kein passender Anzeigemodus für %1 fps").arg(fps, 0, 'f', 3);
        }
        return false;
    };

#ifdef Q_OS_WIN
    const std::wstring dev = id.toStdWString();
    DEVMODEW cur{};
    cur.dmSize = sizeof(cur);
    if (!EnumDisplaySettingsW(dev.c_str(), ENUM_CURRENT_SETTINGS, &cur))
        return false;

    const DWORD wantW = size.isValid() ? DWORD(size.width()) : cur.dmPelsWidth;
    const DWORD wantH = size.isValid() ? DWORD(size.height()) : cur.dmPelsHeight;
    std::vector<DEVMODEW> modes;
    QList<double> rates;
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    for (DWORD i = 0; EnumDisplaySettingsW(dev.c_str(), i, &dm); ++i) {
        if (dm.dmPelsWidth != wantW || dm.dmPelsHeight != wantH
            || dm.dmBitsPerPel != cur.dmBitsPerPel || (dm.dmDisplayFlags & DM_INTERLACED))
            continue;
        modes.push_back(dm);
        rates << realRate(int(dm.dmDisplayFrequency));
    }
    const int pick = pickRate(rates, fps);
    if (pick < 0)
        return noMode();
    DEVMODEW best = modes[size_t(pick)];
    if (info)
        *info = QStringLiteral("%1×%2 @ %3 Hz").arg(wantW).arg(wantH).arg(rates[pick], 0, 'f', 3);
    if (best.dmDisplayFrequency == cur.dmDisplayFrequency && wantW == cur.dmPelsWidth && wantH == cur.dmPelsHeight)
        return true;
    best.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | DM_DISPLAYFREQUENCY;
    if (ChangeDisplaySettingsExW(dev.c_str(), &best, nullptr, CDS_FULLSCREEN, nullptr) != DISP_CHANGE_SUCCESSFUL)
        return false;
    m_originalMode.insert(id, QStringLiteral("registry"));

#elif defined(Q_OS_LINUX)
    if (m_backend == QLatin1String("xrandr")) {
        const XrandrOutput x = xrandrQuery(id, size.isValid() ? QStringLiteral("%1x%2").arg(size.width()).arg(size.height()) : QString());
        QList<double> rates;
        for (const QString &r : x.rates)
            rates << r.toDouble();
        const int pick = pickRate(rates, fps);
        if (pick < 0)
            return noMode();
        if (info)
            *info = x.mode + QStringLiteral(" @ ") + x.rates[pick] + QStringLiteral(" Hz");
        if (x.rates[pick] == x.currentRate && x.mode == x.currentMode)
            return true;
        if (!m_originalMode.contains(id))
            m_originalMode.insert(id, x.currentMode + QLatin1Char('@') + x.currentRate);
        QProcess::execute(QStringLiteral("xrandr"), {QStringLiteral("--output"), id, QStringLiteral("--mode"), x.mode,
                                                     QStringLiteral("--rate"), x.rates[pick]});
    } else if (m_backend == QLatin1String("kscreen")) {
        const QJsonObject k = kscreenOutput(id);
        const QString currentId = k.value(QStringLiteral("currentModeId")).toVariant().toString();
        QJsonObject current;
        const QJsonArray modes = k.value(QStringLiteral("modes")).toArray();
        for (const auto &m : modes)
            if (m.toObject().value(QStringLiteral("id")).toVariant().toString() == currentId)
                current = m.toObject();
        const QJsonObject curSize = size.isValid()
            ? QJsonObject{{QStringLiteral("width"), size.width()}, {QStringLiteral("height"), size.height()}}
            : current.value(QStringLiteral("size")).toObject();
        QList<double> rates;
        QStringList ids;
        for (const auto &mv : modes) {
            const QJsonObject m = mv.toObject();
            if (m.value(QStringLiteral("size")).toObject() != curSize)
                continue;
            rates << m.value(QStringLiteral("refreshRate")).toDouble();
            ids << m.value(QStringLiteral("id")).toVariant().toString();
        }
        const int pick = pickRate(rates, fps);
        if (pick < 0)
            return noMode();
        if (info)
            *info = QStringLiteral("%1 Hz").arg(rates[pick], 0, 'f', 3);
        if (ids[pick] == currentId)
            return true;
        if (!m_originalMode.contains(id))
            m_originalMode.insert(id, currentId);
        QProcess::execute(QStringLiteral("kscreen-doctor"), {QStringLiteral("output.%1.mode.%2").arg(id, ids[pick])});
    } else {
        return false;
    }

#elif defined(Q_OS_MACOS)
    const CGDirectDisplayID display = displayForOrigin(QPoint(o.value("x").toInt(), o.value("y").toInt()));
    if (display == kCGNullDirectDisplay)
        return false;
    CGDisplayModeRef cur = CGDisplayCopyDisplayMode(display);
    const void *keys[] = {kCGDisplayShowDuplicateLowResolutionModes};
    const void *vals[] = {kCFBooleanTrue};
    CFDictionaryRef opts = CFDictionaryCreate(nullptr, keys, vals, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFArrayRef all = CGDisplayCopyAllDisplayModes(display, opts);
    CFRelease(opts);

    std::vector<CGDisplayModeRef> candidates;
    QList<double> rates;
    for (CFIndex i = 0; all && i < CFArrayGetCount(all); ++i) {
        auto m = static_cast<CGDisplayModeRef>(const_cast<void *>(CFArrayGetValueAtIndex(all, i)));
        const bool sizeOk = size.isValid()
            ? (CGDisplayModeGetPixelWidth(m) == size_t(size.width()) && CGDisplayModeGetPixelHeight(m) == size_t(size.height()))
            : (CGDisplayModeGetPixelWidth(m) == CGDisplayModeGetPixelWidth(cur)
               && CGDisplayModeGetPixelHeight(m) == CGDisplayModeGetPixelHeight(cur)
               && CGDisplayModeGetWidth(m) == CGDisplayModeGetWidth(cur));
        if (!sizeOk || CGDisplayModeGetRefreshRate(m) <= 0)
            continue;
        candidates.push_back(m);
        rates << CGDisplayModeGetRefreshRate(m);
    }
    const int pick = pickRate(rates, fps);
    bool ok = false;
    if (pick >= 0) {
        if (info)
            *info = QStringLiteral("%1 Hz").arg(rates[pick], 0, 'f', 3);
        ok = std::abs(rates[pick] - CGDisplayModeGetRefreshRate(cur)) < 0.001
             || CGDisplaySetDisplayMode(display, candidates[size_t(pick)], nullptr) == kCGErrorSuccess;
        if (ok)
            m_originalMode.insert(id, QStringLiteral("permanent"));
    }
    CGDisplayModeRelease(cur);
    if (all)
        CFRelease(all);
    if (pick < 0)
        return noMode();
    if (!ok)
        return false;
#endif

    refresh();
    return true;
}

bool DisplayManager::setHdr(const QString &outputId, bool enabled)
{
    if (!m_canHdr)
        return false;
    const QVariantMap o = output(outputId);
    if (o.isEmpty() || !o.value("hdrSupported").toBool())
        return false;
    const QString id = o.value("id").toString();
    if (o.value("hdrEnabled").toBool() == enabled)
        return true;

#ifdef Q_OS_WIN
    const PathInfo pi = queryPaths().value(id);
    DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE s{};
    s.header.type = DISPLAYCONFIG_DEVICE_INFO_SET_ADVANCED_COLOR_STATE;
    s.header.size = sizeof(s);
    s.header.adapterId = pi.adapterId;
    s.header.id = pi.targetId;
    s.enableAdvancedColor = enabled ? 1 : 0;
    if (DisplayConfigSetDeviceInfo(&s.header) != ERROR_SUCCESS)
        return false;
#elif defined(Q_OS_LINUX)
    // Plasma 6: HDR und erweiterter Farbraum gemeinsam schalten
    const QString state = enabled ? QStringLiteral("enable") : QStringLiteral("disable");
    if (QProcess::execute(QStringLiteral("kscreen-doctor"), {QStringLiteral("output.%1.hdr.%2").arg(id, state),
                                                             QStringLiteral("output.%1.wcg.%2").arg(id, state)}) != 0)
        return false;
#else
    return false;
#endif
    if (!m_originalHdr.contains(id))
        m_originalHdr.insert(id, !enabled);
    refresh();
    return true;
}

void DisplayManager::restoreAll()
{
    const auto modes = m_originalMode;
    m_originalMode.clear();
    for (auto it = modes.cbegin(); it != modes.cend(); ++it) {
#ifdef Q_OS_WIN
        ChangeDisplaySettingsExW(it.key().toStdWString().c_str(), nullptr, nullptr, 0, nullptr);
#elif defined(Q_OS_LINUX)
        if (m_backend == QLatin1String("xrandr")) {
            const QStringList mr = it.value().split(QLatin1Char('@'));
            QProcess::execute(QStringLiteral("xrandr"), {QStringLiteral("--output"), it.key(), QStringLiteral("--mode"), mr.value(0),
                                                         QStringLiteral("--rate"), mr.value(1)});
        } else if (m_backend == QLatin1String("kscreen")) {
            QProcess::execute(QStringLiteral("kscreen-doctor"), {QStringLiteral("output.%1.mode.%2").arg(it.key(), it.value())});
        }
#elif defined(Q_OS_MACOS)
        CGRestorePermanentDisplayConfiguration();
        break; // setzt alle Displays auf einmal zurück
#endif
    }

    const auto hdr = m_originalHdr;
    m_originalHdr.clear();
    for (auto it = hdr.cbegin(); it != hdr.cend(); ++it)
        setHdr(it.key(), it.value());
    m_originalHdr.clear();
}
