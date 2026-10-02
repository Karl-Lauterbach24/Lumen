#include "CastRenderer.h"

#include "CastEncoder.h"

#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QSurfaceFormat>

#include <mpv/client.h>
#include <mpv/render_gl.h>

#include <cstdio>
#include <cstdlib>

CastRenderer::CastRenderer(mpv_handle *mpv, CastEncoder *encoder, QObject *parent)
    : QObject(parent)
    , m_mpv(mpv)
    , m_encoder(encoder)
    , m_size(encoder->settings().width, encoder->settings().height)
{
    QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
#ifdef Q_OS_MACOS
    // macOS liefert sonst nur einen Legacy-2.1-Kontext; mpv braucht >= 3.2 Core
    fmt.setVersion(3, 2);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
#endif
    m_gl.setFormat(fmt);
    if (!m_gl.create()) {
        qWarning("Lumen: kein OpenGL-Kontext für die Übertragung");
        return;
    }
    m_encoder->setMpvTime(mpv_get_time_us(m_mpv));
    m_surface.setFormat(m_gl.format());
    m_surface.create();
    if (!m_gl.makeCurrent(&m_surface)) {
        qWarning("Lumen: OpenGL-Kontext für die Übertragung lässt sich nicht aktivieren");
        return;
    }
    m_fbo = new QOpenGLFramebufferObject(m_size);

    mpv_opengl_init_params gl{getProcAddress, nullptr};
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
    if (mpv_render_context_create(&m_ctx, m_mpv, params) < 0) {
        qWarning("Lumen: mpv-Render-Kontext für die Übertragung konnte nicht erstellt werden");
        m_ctx = nullptr;
    } else {
        mpv_render_context_set_update_callback(m_ctx, updateCallback, this);
    }
    m_gl.doneCurrent();
}

CastRenderer::~CastRenderer()
{
    releaseRenderContext();
    if (m_fbo && m_gl.makeCurrent(&m_surface)) {
        delete m_fbo;
        m_gl.doneCurrent();
    }
}

void CastRenderer::releaseRenderContext()
{
    if (!m_ctx)
        return;
    m_gl.makeCurrent(&m_surface);
    mpv_render_context_set_update_callback(m_ctx, nullptr, nullptr);
    mpv_render_context_free(m_ctx);
    m_ctx = nullptr;
    m_gl.doneCurrent();
}

void *CastRenderer::getProcAddress(void *, const char *name)
{
    QOpenGLContext *ctx = QOpenGLContext::currentContext();
    return ctx ? reinterpret_cast<void *>(ctx->getProcAddress(name)) : nullptr;
}

void CastRenderer::updateCallback(void *ctx)
{
    // Aufruf aus einem mpv-Thread -> in den GUI-Thread wechseln
    QMetaObject::invokeMethod(static_cast<CastRenderer *>(ctx), "onMpvUpdate", Qt::QueuedConnection);
}

void CastRenderer::onMpvUpdate()
{
    if (!m_ctx || !(mpv_render_context_update(m_ctx) & MPV_RENDER_UPDATE_FRAME))
        return;
    if (!m_gl.makeCurrent(&m_surface))
        return;
    // Zeitstempel: der Augenblick, zu dem mpv das Bild zeigen will
    qint64 when = mpv_get_time_us(m_mpv);
    mpv_render_frame_info info{};
    mpv_render_param infoParam{MPV_RENDER_PARAM_NEXT_FRAME_INFO, &info};
    if (mpv_render_context_get_info(m_ctx, infoParam) >= 0 && (info.flags & MPV_RENDER_FRAME_INFO_PRESENT)
        && info.target_time > 0 && std::llabs(info.target_time - when) < 500000)
        when = info.target_time;
    const double pts = m_encoder->streamTime(when);
    mpv_opengl_fbo fbo{int(m_fbo->handle()), m_size.width(), m_size.height(), 0};
    int flipY = 0; // mpv legt die oberste Bildzeile an y = 0 des Framebuffers: glReadPixels liefert sie zuerst
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
        {MPV_RENDER_PARAM_FLIP_Y, &flipY},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
    mpv_render_context_render(m_ctx, params);

    QByteArray rgba(m_size.width() * m_size.height() * 4, Qt::Uninitialized);
    QOpenGLFunctions *f = m_gl.functions();
    f->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo->handle());
    f->glPixelStorei(GL_PACK_ALIGNMENT, 4);
    f->glReadPixels(0, 0, m_size.width(), m_size.height(), GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    if (const GLenum glError = f->glGetError(); glError != GL_NO_ERROR && !m_warned) {
        m_warned = true;
        qWarning("Lumen: OpenGL-Fehler 0x%x beim Auslesen des Bildes für die Übertragung", glError);
    }
    mpv_render_context_report_swap(m_ctx);
    m_gl.doneCurrent();
    // Entwickler-Hilfe (LUMEN_CAST_DEBUG): Farbe in Bildmitte und oben links, einmal je Sekunde
    if (m_debug && ++m_count % 30 == 1) {
        auto px = [&](int x, int y) {
            const uchar *p = reinterpret_cast<const uchar *>(rgba.constData()) + (y * m_size.width() + x) * 4;
            return QStringLiteral("%1,%2,%3,%4").arg(p[0]).arg(p[1]).arg(p[2]).arg(p[3]);
        };
        std::fprintf(stderr, "Lumen: Bild %d Mitte rgba=%s oben links rgba=%s\n", m_count,
                 qPrintable(px(m_size.width() / 2, m_size.height() / 2)), qPrintable(px(m_size.width() / 32, m_size.height() / 32)));
    }
    m_encoder->pushFrame(std::move(rgba), pts);
}
