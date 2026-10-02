#include "Tuning.h"

#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QThread>

namespace Tuning {

static Hardware probe()
{
    Hardware hw;
    hw.cores = qMax(1, QThread::idealThreadCount());
    const QByteArray forced = qgetenv("LUMEN_GPU");
    if (!forced.isEmpty()) {
        hw.renderer = QString::fromUtf8(forced);
    } else if (qGuiApp && QGuiApplication::platformName() != QLatin1String("offscreen")) {
        // Ein Kontext ohne Fenster genügt, um den Treiber nach seinem Namen zu fragen
        QOpenGLContext gl;
        QOffscreenSurface surface;
        if (gl.create()) {
            surface.setFormat(gl.format());
            surface.create();
            if (surface.isValid() && gl.makeCurrent(&surface)) {
                auto text = [&](GLenum name) {
                    const GLubyte *s = gl.functions()->glGetString(name);
                    return s ? QString::fromLatin1(reinterpret_cast<const char *>(s)) : QString();
                };
                hw.renderer = text(GL_RENDERER);
                hw.vendor = text(GL_VENDOR);
                gl.doneCurrent();
            }
        }
    }
    hw.gpu = classifyGpu(hw.renderer, hw.vendor);
    return hw;
}

const Hardware &hardware()
{
    static const Hardware hw = probe();
    return hw;
}

} // namespace Tuning
