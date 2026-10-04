#include "BdOverlay.h"

#include <libbluray/overlay.h>

#include <algorithm>
#include <cmath>
#include <cstring>

bool BdOverlay::apply(QImage &plane, const bd_overlay_s &ov)
{
    switch (ov.cmd) {
    case BD_OVERLAY_INIT:
        plane = QImage(ov.w, ov.h, QImage::Format_ARGB32);
        plane.fill(Qt::transparent);
        return false;
    case BD_OVERLAY_CLOSE:
        plane = QImage();
        return true;
    case BD_OVERLAY_CLEAR:
    case BD_OVERLAY_HIDE:
        if (!plane.isNull())
            plane.fill(Qt::transparent);
        return false;
    case BD_OVERLAY_WIPE: {
        const QRect r = QRect(ov.x, ov.y, ov.w, ov.h).intersected(plane.rect());
        for (int y = 0; y < r.height(); ++y)
            std::memset(plane.scanLine(r.y() + y) + r.x() * 4, 0, size_t(r.width()) * 4);
        return false;
    }
    case BD_OVERLAY_DRAW: {
        if (plane.isNull() || !ov.img || !ov.palette)
            return false;
        uint32_t argb[256];
        for (int i = 0; i < 256; ++i) {
            const BD_PG_PALETTE_ENTRY &e = ov.palette[i];
            const double y = 1.164 * (e.Y - 16), cb = e.Cb - 128, cr = e.Cr - 128;
            const auto c = [](double v) { return uint32_t(std::clamp(int(std::lround(v)), 0, 255)); };
            argb[i] = uint32_t(e.T) << 24 | c(y + 1.793 * cr) << 16 | c(y - 0.213 * cb - 0.533 * cr) << 8 | c(y + 2.112 * cb);
        }
        // Jede Zeile besteht aus Läufen, die zusammen die Breite ergeben; libbluray schließt sie mit
        // einem Lauf der Länge 0 ab
        const BD_PG_RLE_ELEM *run = ov.img;
        for (int y = 0; y < ov.h; ++y) {
            const int py = ov.y + y;
            uint32_t *dst = py < plane.height() ? reinterpret_cast<uint32_t *>(plane.scanLine(py)) : nullptr;
            for (int x = 0, empty = 0; x < ov.w; ++run) {
                if (!run->len) {
                    if (++empty > 2) // beschädigte Daten: nicht endlos laufen
                        return false;
                    continue;
                }
                empty = 0;
                if (dst) {
                    const int from = std::min(ov.x + x, plane.width()), to = std::min(ov.x + x + int(run->len), plane.width());
                    std::fill(dst + from, dst + to, argb[run->color & 0xff]);
                }
                x += run->len;
            }
        }
        return false;
    }
    case BD_OVERLAY_FLUSH:
        return true;
    default:
        return false;
    }
}
