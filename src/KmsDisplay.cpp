#include "KmsDisplay.h"

#include <QDir>
#include <QFile>

#include <cmath>

#ifdef LUMEN_HAVE_LIBDRM
#include <fcntl.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#endif

namespace Kms {

#ifdef LUMEN_HAVE_LIBDRM

namespace {

// Namen der Anschlussarten, wie mpv sie schreibt (video/out/drm_common.c)
const char *connectorName(uint32_t type)
{
    switch (type) {
    case DRM_MODE_CONNECTOR_VGA: return "VGA";
    case DRM_MODE_CONNECTOR_DVII: return "DVI-I";
    case DRM_MODE_CONNECTOR_DVID: return "DVI-D";
    case DRM_MODE_CONNECTOR_DVIA: return "DVI-A";
    case DRM_MODE_CONNECTOR_Composite: return "Composite";
    case DRM_MODE_CONNECTOR_SVIDEO: return "SVIDEO";
    case DRM_MODE_CONNECTOR_LVDS: return "LVDS";
    case DRM_MODE_CONNECTOR_Component: return "Component";
    case DRM_MODE_CONNECTOR_9PinDIN: return "DIN";
    case DRM_MODE_CONNECTOR_DisplayPort: return "DP";
    case DRM_MODE_CONNECTOR_HDMIA: return "HDMI-A";
    case DRM_MODE_CONNECTOR_HDMIB: return "HDMI-B";
    case DRM_MODE_CONNECTOR_TV: return "TV";
    case DRM_MODE_CONNECTOR_eDP: return "eDP";
    case DRM_MODE_CONNECTOR_VIRTUAL: return "Virtual";
    case DRM_MODE_CONNECTOR_DSI: return "DSI";
    case DRM_MODE_CONNECTOR_DPI: return "DPI";
#ifdef DRM_MODE_CONNECTOR_WRITEBACK
    case DRM_MODE_CONNECTOR_WRITEBACK: return "Writeback";
#endif
#ifdef DRM_MODE_CONNECTOR_SPI
    case DRM_MODE_CONNECTOR_SPI: return "SPI";
#endif
#ifdef DRM_MODE_CONNECTOR_USB
    case DRM_MODE_CONNECTOR_USB: return "USB";
#endif
    default: return "Unknown";
    }
}

double modeHz(const drmModeModeInfo &mode)
{
    if (mode.htotal == 0 || mode.vtotal == 0)
        return 0;
    return mode.clock * 1000.0 / mode.htotal / mode.vtotal;
}

void readProperties(int fd, const drmModeConnector *connector, KmsOutput &out)
{
    for (int i = 0; i < connector->count_props; ++i) {
        drmModePropertyRes *property = drmModeGetProperty(fd, connector->props[i]);
        if (!property)
            continue;
        const QByteArray name(property->name);
        if (name == "HDR_OUTPUT_METADATA") {
            out.hdrMetadata = true;
        } else if (name == "Colorspace") {
            out.colorspace = true;
        } else if (name == "max bpc") {
            // Bereich der Eigenschaft: was der Anschluss höchstens kann
            if ((property->flags & DRM_MODE_PROP_RANGE) && property->count_values >= 2)
                out.maxBpc = int(property->values[1]);
        } else if (name == "EDID" && connector->prop_values[i] != 0) {
            if (drmModePropertyBlobRes *blob = drmModeGetPropertyBlob(fd, uint32_t(connector->prop_values[i]))) {
                out.edid = QByteArray(static_cast<const char *>(blob->data), int(blob->length));
                drmModeFreePropertyBlob(blob);
            }
        }
        drmModeFreeProperty(property);
    }
}

} // namespace

bool available()
{
    return true;
}

KmsOutput probe()
{
    KmsOutput out;
    const QStringList cards = QDir(QStringLiteral("/dev/dri")).entryList({QStringLiteral("card*")}, QDir::System, QDir::Name);
    for (const QString &card : cards) {
        const QString path = QStringLiteral("/dev/dri/") + card;
        const int fd = ::open(QFile::encodeName(path).constData(), O_RDWR | O_CLOEXEC);
        if (fd < 0)
            continue;
        if (drmModeRes *resources = drmModeGetResources(fd)) {
            for (int i = 0; i < resources->count_connectors && !out.valid; ++i) {
                drmModeConnector *connector = drmModeGetConnector(fd, resources->connectors[i]);
                if (!connector)
                    continue;
                if (connector->connection == DRM_MODE_CONNECTED && connector->count_modes > 0) {
                    out.valid = true;
                    out.device = path;
                    out.connector = QStringLiteral("%1-%2").arg(QLatin1String(connectorName(connector->connector_type))).arg(connector->connector_type_id);
                    for (int m = 0; m < connector->count_modes; ++m) {
                        const drmModeModeInfo &mode = connector->modes[m];
                        if (mode.flags & DRM_MODE_FLAG_INTERLACE)
                            continue;
                        out.modes.append({mode.hdisplay, mode.vdisplay, modeHz(mode), (mode.type & DRM_MODE_TYPE_PREFERRED) != 0});
                    }
                    readProperties(fd, connector, out);
                }
                drmModeFreeConnector(connector);
            }
            drmModeFreeResources(resources);
        }
        ::close(fd);
        if (out.valid)
            break;
    }
    return out;
}

#else

bool available()
{
    return false;
}

KmsOutput probe()
{
    return {};
}

#endif

QString modeFor(const KmsOutput &output, double fps)
{
    if (!output.valid || output.modes.isEmpty() || !(fps > 1.0))
        return {};
    // die Auflösung bleibt die, die der Bildschirm bevorzugt (sonst die erste genannte)
    KmsMode base = output.modes.first();
    for (const KmsMode &mode : output.modes) {
        if (mode.preferred) {
            base = mode;
            break;
        }
    }
    // Bildwechsel als ganzes Vielfaches der Bildrate: genau (23,976 auf 23,976), sonst so nah, dass
    // mpv den Rest unhörbar ausgleicht (23,976 auf 24,000: ein Tausendstel).
    // Kinofilm (24 Bilder) bekommt einen Bildwechsel je Bild. Alles andere mindestens rund 50 je
    // Sekunde: Material in Halbbildern (DVD, Fernsehen) nennt hier seine Vollbilder, zeigt nach dem
    // Entflechten aber doppelt so viele – und 25 Bilder sehen bei 50 Bildwechseln aus wie bei 25.
    const int wanted = (fps > 23.5 && fps < 24.5) ? 1 : qMax(1, int(std::ceil(48.5 / fps)));
    const KmsMode *best = nullptr;
    double bestCost = 1e9;
    for (const KmsMode &mode : output.modes) {
        if (mode.width != base.width || mode.height != base.height || mode.hz < 1.0)
            continue;
        const int multiple = qMax(1, int(std::lround(mode.hz / fps)));
        const double off = std::fabs(mode.hz - multiple * fps) / (multiple * fps);
        if (off > 0.0011)
            continue;
        // genau vor ungefähr; dann das gewünschte Vielfache, lieber darüber als darunter
        const double cost = (off < 0.00002 ? 0.0 : 100.0 + off * 1000.0)
                            + (multiple < wanted ? 10.0 * (wanted - multiple) : multiple - wanted);
        if (cost < bestCost) {
            bestCost = cost;
            best = &mode;
        }
    }
    if (!best)
        return {};
    return QStringLiteral("%1x%2@%3").arg(best->width).arg(best->height).arg(best->hz, 0, 'f', 2);
}

} // namespace Kms
