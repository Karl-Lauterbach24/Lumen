#pragma once

#include <QByteArray>
#include <QImage>
#include <QMutex>
#include <QTimer>
#include <QWaitCondition>
#include <QWindow>

#include <atomic>
#include <initializer_list>

struct mpv_handle;
struct mpv_render_context;
class QOpenGLContext;
class QThread;

// Eingebettetes Player-Fenster über die libmpv-Render-API (OpenGL).
//
// Wird genutzt, wo libmpv kein eigenes Fenster öffnen kann (macOS) oder wenn
// das Profil "Eingebettet" wählt. Die Fensterverwaltung (Bildschirm, Vollbild,
// Tastatur, Maus) übernimmt Qt; Eingaben werden an mpv weitergereicht, damit
// sich das Fenster wie das native Player-Fenster verhält.
// Einschränkung: Ausgabe in SDR (die OpenGL-Render-API kennt kein HDR-Signal).
//
// Gezeichnet wird in einem eigenen Thread: mpv weckt ihn für jedes Bild, er rendert und
// tauscht die Puffer im Takt des Bildschirms. Der GUI-Thread ist daran nicht beteiligt –
// was dort gerade läuft (Oberfläche, Abfragen an mpv), kann kein Bild aufhalten, und bei
// 120 Bildern je Sekunde (3D-Bildfolge) kommt jedes zu seinem Bildwechsel.
class PlayerWindow : public QWindow
{
    Q_OBJECT

public:
    explicit PlayerWindow(mpv_handle *mpv);
    ~PlayerWindow() override;

    // Muss vor mpv_terminate_destroy() aufgerufen werden: beendet den Render-Thread
    void releaseRenderContext();
    void place(QScreen *screen, bool fullscreen);
    void setFullscreen(bool on);
    bool ready() const { return m_ready.load(); }
    // Inhalt des Fensters, wie er gerade gezeichnet wird (wartet auf das nächste Bild)
    QImage grabFramebuffer();
    // Zahl der gezeichneten Bilder seit dem Start (für das Leistungsprotokoll)
    qint64 paintCount() const { return m_paints.load(); }

signals:
    void closeRequested();
    void renderReady();

protected:
    void exposeEvent(QExposeEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void keyReleaseEvent(QKeyEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    bool event(QEvent *e) override;

private:
    static void *getProcAddress(void *ctx, const char *name);
    static void updateCallback(void *ctx);
    void command(std::initializer_list<QByteArray> args);
    void showCursorTemporarily();
    void renderLoop();
    void wake(bool redraw);
    void updatePixelSize();
    void syncContextWithWindow();

    mpv_handle *m_mpv = nullptr;
    mpv_render_context *m_ctx = nullptr; // gehört dem Render-Thread
    QOpenGLContext *m_gl = nullptr;
    QThread *m_thread = nullptr;
    QTimer m_cursorTimer;
    QScreen *m_screen = nullptr;

    // zwischen GUI-, mpv- und Render-Thread
    QMutex m_lock;
    QWaitCondition m_cond;
    QWaitCondition m_grabDone;
    bool m_wake = false;
    bool m_redraw = false;
    bool m_quit = false;
    bool m_grab = false;
    // macOS: Übergabe des Kontexts an den Haupt-Thread nach einer Größenänderung
    QWaitCondition m_handoverCond;
    bool m_handover = false;
    bool m_released = false;
    bool m_reclaimed = false;
    QImage m_grabbed;
    std::atomic<bool> m_ready{false};
    std::atomic<bool> m_exposed{false};
    std::atomic<int> m_pixelWidth{0};
    std::atomic<int> m_pixelHeight{0};
    std::atomic<qint64> m_paints{0};
};
