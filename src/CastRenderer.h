#pragma once

#include <QObject>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QSize>

struct mpv_handle;
struct mpv_render_context;
class CastEncoder;
class QOpenGLFramebufferObject;

// Rendert das Bild von mpv ohne Fenster (Render-API in einen Framebuffer) und reicht jedes
// fertige Bild an den Encoder weiter. Der Framebuffer hat immer die Größe des Sendestroms;
// mpv setzt das Bild selbst hinein (Seitenverhältnis, Disc-Menüs, Untertitel, Tonemapping).
class CastRenderer : public QObject
{
    Q_OBJECT

public:
    CastRenderer(mpv_handle *mpv, CastEncoder *encoder, QObject *parent = nullptr);
    ~CastRenderer() override;

    bool ready() const { return m_ctx != nullptr; }
    // Muss vor mpv_terminate_destroy() aufgerufen werden
    void releaseRenderContext();

    // Entwickler-Hilfe (LUMEN_CAST_DEBUG): Zähler über die gerenderten Bilder
    struct DebugStats
    {
        int frames = 0;      // gerenderte Bilder
        int lit = 0;         // Bilder mit mindestens einem hellen Bildpunkt
        int errors = 0;      // Fehler von mpv_render_context_render()
        double firstMs = 0;  // Dauer des ersten Aufrufs
        double maxMs = 0;    // längster Aufruf
        int fboCheck = -1;   // eigener Probe-Anstrich des Framebuffers: 1 = kam an
        QString first;       // Zustand beim ersten Bild
        QString boxes;       // helle Bereiche der ersten Bilder
    };
    const DebugStats &debugStats() const { return m_stats; }

private slots:
    void onMpvUpdate();

private:
    static void *getProcAddress(void *ctx, const char *name);
    static void updateCallback(void *ctx);

    mpv_handle *m_mpv;
    CastEncoder *m_encoder;
    QSize m_size;
    QOffscreenSurface m_surface;
    QOpenGLContext m_gl;
    QOpenGLFramebufferObject *m_fbo = nullptr;
    mpv_render_context *m_ctx = nullptr;
    bool m_warned = false;
    bool m_debug = qEnvironmentVariableIsSet("LUMEN_CAST_DEBUG");
    int m_count = 0;
    DebugStats m_stats;
    QByteArray m_exp = qgetenv("LUMEN_CAST_EXP"); // vorübergehend: Varianten für die Fehlersuche
};
