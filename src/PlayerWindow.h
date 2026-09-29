#pragma once

#include <QByteArray>
#include <QOpenGLWindow>
#include <QTimer>
#include <initializer_list>

struct mpv_handle;
struct mpv_render_context;

// Eingebettetes Player-Fenster über die libmpv-Render-API (OpenGL).
//
// Wird genutzt, wo libmpv kein eigenes Fenster öffnen kann (macOS) oder wenn
// das Profil "Eingebettet" wählt. Die Fensterverwaltung (Bildschirm, Vollbild,
// Tastatur, Maus) übernimmt Qt; Eingaben werden an mpv weitergereicht, damit
// sich das Fenster wie das native Player-Fenster verhält.
// Einschränkung: Ausgabe in SDR (die OpenGL-Render-API kennt kein HDR-Signal).
class PlayerWindow : public QOpenGLWindow
{
    Q_OBJECT

public:
    explicit PlayerWindow(mpv_handle *mpv);
    ~PlayerWindow() override;

    // Muss vor mpv_terminate_destroy() aufgerufen werden
    void releaseRenderContext();
    void place(QScreen *screen, bool fullscreen);
    void setFullscreen(bool on);
    bool ready() const { return m_ctx != nullptr; }

signals:
    void closeRequested();
    void renderReady();

protected:
    void initializeGL() override;
    void paintGL() override;
    void keyPressEvent(QKeyEvent *e) override;
    void keyReleaseEvent(QKeyEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    bool event(QEvent *e) override;

private slots:
    void onMpvUpdate();

private:
    static void *getProcAddress(void *ctx, const char *name);
    static void updateCallback(void *ctx);
    void command(std::initializer_list<QByteArray> args);
    void showCursorTemporarily();

    mpv_handle *m_mpv = nullptr;
    mpv_render_context *m_ctx = nullptr;
    QTimer m_cursorTimer;
    QScreen *m_screen = nullptr;
};
