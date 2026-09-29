#include "PlayerWindow.h"

#include <QGuiApplication>
#include <QHash>
#include <QKeyEvent>
#include <QOpenGLContext>
#include <QScreen>
#include <QSurfaceFormat>

#include <mpv/client.h>
#include <mpv/render_gl.h>

#include <vector>

namespace {

QByteArray mpvKeyName(const QKeyEvent *e)
{
    static const QHash<int, const char *> special = {
        {Qt::Key_Space, "SPACE"}, {Qt::Key_Return, "ENTER"}, {Qt::Key_Enter, "KP_ENTER"},
        {Qt::Key_Escape, "ESC"}, {Qt::Key_Left, "LEFT"}, {Qt::Key_Right, "RIGHT"},
        {Qt::Key_Up, "UP"}, {Qt::Key_Down, "DOWN"}, {Qt::Key_PageUp, "PGUP"},
        {Qt::Key_PageDown, "PGDWN"}, {Qt::Key_Home, "HOME"}, {Qt::Key_End, "END"},
        {Qt::Key_Backspace, "BS"}, {Qt::Key_Tab, "TAB"}, {Qt::Key_Delete, "DEL"},
        {Qt::Key_Insert, "INS"}, {Qt::Key_MediaPlay, "PLAY"}, {Qt::Key_MediaPause, "PAUSE"},
        {Qt::Key_MediaTogglePlayPause, "PLAYPAUSE"}, {Qt::Key_MediaStop, "STOP"},
        {Qt::Key_MediaNext, "NEXT"}, {Qt::Key_MediaPrevious, "PREV"},
        {Qt::Key_VolumeUp, "VOLUME_UP"}, {Qt::Key_VolumeDown, "VOLUME_DOWN"},
        {Qt::Key_VolumeMute, "MUTE"}, {Qt::Key_Menu, "MENU"},
    };
    QByteArray mods;
    if (e->modifiers() & Qt::ControlModifier)
        mods += "Ctrl+";
    if (e->modifiers() & Qt::AltModifier)
        mods += "Alt+";
    if (e->modifiers() & Qt::MetaModifier)
        mods += "Meta+";

    if (const char *name = special.value(e->key(), nullptr)) {
        if (e->modifiers() & Qt::ShiftModifier)
            mods += "Shift+";
        return mods + name;
    }
    if (e->key() >= Qt::Key_F1 && e->key() <= Qt::Key_F24)
        return mods + "F" + QByteArray::number(e->key() - Qt::Key_F1 + 1);

    const QString text = e->text();
    if (text.isEmpty() || !text.at(0).isPrint())
        return {};
    // Zeichen tragen die Umschalttaste bereits in sich ("A" statt "Shift+a")
    return mods + text.toUtf8();
}

const char *mouseButton(Qt::MouseButton b)
{
    switch (b) {
    case Qt::LeftButton: return "MBTN_LEFT";
    case Qt::RightButton: return "MBTN_RIGHT";
    case Qt::MiddleButton: return "MBTN_MID";
    case Qt::BackButton: return "MBTN_BACK";
    case Qt::ForwardButton: return "MBTN_FORWARD";
    default: return nullptr;
    }
}

} // namespace

PlayerWindow::PlayerWindow(mpv_handle *mpv)
    : m_mpv(mpv)
{
    setTitle(QStringLiteral("Lumen"));
    resize(1280, 720);
#ifdef Q_OS_MACOS
    // macOS liefert sonst nur einen Legacy-2.1-Kontext; mpv braucht >= 3.2 Core
    QSurfaceFormat fmt = format();
    fmt.setVersion(3, 2);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    setFormat(fmt);
#endif
    m_cursorTimer.setSingleShot(true);
    m_cursorTimer.setInterval(900);
    connect(&m_cursorTimer, &QTimer::timeout, this, [this] { setCursor(Qt::BlankCursor); });
}

PlayerWindow::~PlayerWindow()
{
    releaseRenderContext();
}

void PlayerWindow::releaseRenderContext()
{
    if (!m_ctx)
        return;
    makeCurrent();
    mpv_render_context_free(m_ctx);
    m_ctx = nullptr;
    doneCurrent();
}

void *PlayerWindow::getProcAddress(void *, const char *name)
{
    QOpenGLContext *ctx = QOpenGLContext::currentContext();
    return ctx ? reinterpret_cast<void *>(ctx->getProcAddress(name)) : nullptr;
}

void PlayerWindow::updateCallback(void *ctx)
{
    // Aufruf aus einem mpv-Thread -> in den GUI-Thread wechseln
    QMetaObject::invokeMethod(static_cast<PlayerWindow *>(ctx), "onMpvUpdate", Qt::QueuedConnection);
}

void PlayerWindow::initializeGL()
{
    if (m_ctx || !m_mpv)
        return;
    mpv_opengl_init_params gl{getProcAddress, nullptr};
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
    if (mpv_render_context_create(&m_ctx, m_mpv, params) < 0) {
        qWarning("Lumen: mpv-Render-Kontext konnte nicht erstellt werden");
        m_ctx = nullptr;
        return;
    }
    mpv_render_context_set_update_callback(m_ctx, updateCallback, this);
    connect(this, &QOpenGLWindow::frameSwapped, this, [this] {
        if (m_ctx)
            mpv_render_context_report_swap(m_ctx);
    });
    QMetaObject::invokeMethod(this, &PlayerWindow::renderReady, Qt::QueuedConnection);
}

void PlayerWindow::onMpvUpdate()
{
    if (m_ctx && (mpv_render_context_update(m_ctx) & MPV_RENDER_UPDATE_FRAME))
        update();
}

void PlayerWindow::paintGL()
{
    if (!m_ctx)
        return;
    const qreal dpr = devicePixelRatio();
    mpv_opengl_fbo fbo{int(defaultFramebufferObject()), int(width() * dpr), int(height() * dpr), 0};
    int flipY = 1;
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
        {MPV_RENDER_PARAM_FLIP_Y, &flipY},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
    mpv_render_context_render(m_ctx, params);
}

void PlayerWindow::place(QScreen *screen, bool fullscreen)
{
    if (screen && screen != m_screen) {
        m_screen = screen;
        setScreen(screen);
        const QRect g = screen->availableGeometry();
        setGeometry(QRect(QPoint(), size().boundedTo(g.size())).translated(g.center() - QPoint(width() / 2, height() / 2)));
    }
    setFullscreen(fullscreen);
}

void PlayerWindow::setFullscreen(bool on)
{
    if (on == (visibility() == QWindow::FullScreen) && isVisible())
        return;
    if (on) {
        if (m_screen)
            setScreen(m_screen);
        showFullScreen();
    } else {
        showNormal();
    }
}

void PlayerWindow::command(std::initializer_list<QByteArray> args)
{
    if (!m_mpv)
        return;
    std::vector<const char *> argv;
    for (const auto &a : args)
        argv.push_back(a.constData());
    argv.push_back(nullptr);
    mpv_command_async(m_mpv, 0, argv.data());
}

void PlayerWindow::showCursorTemporarily()
{
    unsetCursor();
    m_cursorTimer.start();
}

void PlayerWindow::keyPressEvent(QKeyEvent *e)
{
    const QByteArray key = mpvKeyName(e);
    if (key.isEmpty())
        return QOpenGLWindow::keyPressEvent(e);
    // keydown/keyup statt keypress: mpv's Tastenwiederholung und Doppelbelegung bleiben erhalten
    if (!e->isAutoRepeat())
        command({"keydown", key});
}

void PlayerWindow::keyReleaseEvent(QKeyEvent *e)
{
    const QByteArray key = mpvKeyName(e);
    if (!key.isEmpty() && !e->isAutoRepeat())
        command({"keyup", key});
}

void PlayerWindow::mouseMoveEvent(QMouseEvent *e)
{
    const qreal dpr = devicePixelRatio();
    command({"mouse", QByteArray::number(int(e->position().x() * dpr)), QByteArray::number(int(e->position().y() * dpr))});
    showCursorTemporarily();
}

void PlayerWindow::mousePressEvent(QMouseEvent *e)
{
    mouseMoveEvent(e);
    if (const char *b = mouseButton(e->button()))
        command({"keydown", b});
}

void PlayerWindow::mouseReleaseEvent(QMouseEvent *e)
{
    if (const char *b = mouseButton(e->button()))
        command({"keyup", b});
}

void PlayerWindow::wheelEvent(QWheelEvent *e)
{
    const QPoint d = e->angleDelta();
    if (d.y() != 0)
        command({"keypress", d.y() > 0 ? "WHEEL_UP" : "WHEEL_DOWN"});
    else if (d.x() != 0)
        command({"keypress", d.x() > 0 ? "WHEEL_LEFT" : "WHEEL_RIGHT"});
}

bool PlayerWindow::event(QEvent *e)
{
    if (e->type() == QEvent::Close) {
        e->ignore();
        emit closeRequested();
        return true;
    }
    return QOpenGLWindow::event(e);
}
