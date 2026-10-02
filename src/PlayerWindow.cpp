#include "PlayerWindow.h"

#include <QGuiApplication>
#include <QHash>
#include <QKeyEvent>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QDeadlineTimer>
#include <QThread>
#include <QScreen>
#include <QSurfaceFormat>

#include <mpv/client.h>
#include <mpv/render_gl.h>

#include <utility>
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
    setSurfaceType(QSurface::OpenGLSurface);
    setTitle(QStringLiteral("Lumen"));
    resize(1280, 720);
    QSurfaceFormat fmt = requestedFormat();
#ifdef Q_OS_MACOS
    // macOS liefert sonst nur einen Legacy-2.1-Kontext; mpv braucht >= 3.2 Core
    fmt.setVersion(3, 2);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
#endif
    fmt.setSwapInterval(1); // der Puffertausch wartet auf den Bildwechsel: er ist der Takt des Render-Threads
    setFormat(fmt);
    m_cursorTimer.setSingleShot(true);
    m_cursorTimer.setInterval(900);
    connect(&m_cursorTimer, &QTimer::timeout, this, [this] { setCursor(Qt::BlankCursor); });
    connect(this, &QWindow::screenChanged, this, [this] {
        updatePixelSize();
        syncContextWithWindow();
        wake(true);
    });
}

PlayerWindow::~PlayerWindow()
{
    releaseRenderContext();
}

void PlayerWindow::releaseRenderContext()
{
    if (!m_thread)
        return;
    {
        QMutexLocker lock(&m_lock);
        m_quit = true;
        m_cond.wakeAll();
        m_handoverCond.wakeAll();
    }
    m_thread->wait();
    delete m_thread;
    m_thread = nullptr;
    delete m_gl; // der Render-Thread hat ihn an diesen Thread zurückgegeben
    m_gl = nullptr;
    m_ready = false;
}

void *PlayerWindow::getProcAddress(void *, const char *name)
{
    QOpenGLContext *ctx = QOpenGLContext::currentContext();
    return ctx ? reinterpret_cast<void *>(ctx->getProcAddress(name)) : nullptr;
}

void PlayerWindow::updateCallback(void *ctx)
{
    // Aufruf aus einem mpv-Thread: nur den Render-Thread wecken
    static_cast<PlayerWindow *>(ctx)->wake(false);
}

void PlayerWindow::wake(bool redraw)
{
    QMutexLocker lock(&m_lock);
    m_wake = true;
    m_redraw |= redraw;
    m_cond.wakeOne();
}

void PlayerWindow::updatePixelSize()
{
    const qreal dpr = devicePixelRatio();
    m_pixelWidth = int(width() * dpr);
    m_pixelHeight = int(height() * dpr);
}

// macOS: Ändert sich das Fenster (Größe, Vollbild, Bildschirm), muss der OpenGL-Kontext im
// Haupt-Thread nachgeführt werden – AppKit bricht das Programm sonst ab. Der Render-Thread gibt
// den Kontext dafür ab, dieser Thread aktiviert ihn einmal (das führt ihn nach) und gibt ihn
// zurück. Der Render-Thread aktiviert ihn nur direkt nach einer solchen Übergabe.
void PlayerWindow::syncContextWithWindow()
{
#ifdef Q_OS_MACOS
    if (!m_thread || !m_gl || !m_ready.load())
        return;
    QMutexLocker lock(&m_lock);
    if (m_quit)
        return;
    m_handover = true;
    m_wake = true;
    m_cond.wakeOne();
    QDeadlineTimer deadline(1500);
    while (!m_released && !m_quit && !deadline.hasExpired())
        m_handoverCond.wait(&m_lock, deadline);
    if (!m_released) {
        m_handover = false; // der Render-Thread hat nicht geantwortet: beim nächsten Mal
        return;
    }
    lock.unlock();
    if (isExposed() && m_gl->makeCurrent(this))
        m_gl->doneCurrent();
    m_gl->moveToThread(m_thread);
    lock.relock();
    m_handover = false;
    m_released = false;
    m_handoverCond.wakeAll();
    // Warten, bis der Render-Thread den Kontext wieder hat: solange dieser Thread hier steht,
    // kann keine weitere Fensteränderung dazwischenkommen
    m_reclaimed = false;
    deadline = QDeadlineTimer(1500);
    while (!m_reclaimed && !m_quit && !deadline.hasExpired())
        m_handoverCond.wait(&m_lock, deadline);
#endif
}

void PlayerWindow::exposeEvent(QExposeEvent *)
{
    m_exposed = isExposed();
    updatePixelSize();
    syncContextWithWindow();
    if (isExposed() && !m_thread && m_mpv && !m_quit) {
        // Kontext hier anlegen, dann dem Render-Thread übergeben
        m_gl = new QOpenGLContext;
        m_gl->setFormat(requestedFormat());
        if (!m_gl->create()) {
            qWarning("Lumen: kein OpenGL-Kontext für das Player-Fenster");
            delete m_gl;
            m_gl = nullptr;
            return;
        }
        // macOS: Die Verbindung von Kontext und Fenster muss im Haupt-Thread entstehen; danach
        // darf jeder Thread den Kontext aktivieren
        m_gl->makeCurrent(this);
        m_gl->doneCurrent();
        m_thread = QThread::create([this] { renderLoop(); });
        m_thread->setObjectName(QStringLiteral("lumen-render"));
        m_gl->moveToThread(m_thread);
        m_thread->start(QThread::TimeCriticalPriority);
    }
    wake(true);
}

void PlayerWindow::resizeEvent(QResizeEvent *)
{
    updatePixelSize();
    syncContextWithWindow();
    wake(true);
}

// Der Render-Thread: wartet, bis mpv ein Bild hat (oder das Fenster neu zu zeichnen ist),
// rendert es und tauscht die Puffer. Der Tausch wartet auf den Bildwechsel des Bildschirms.
void PlayerWindow::renderLoop()
{
    QThread *const guiThread = thread();
    auto giveBack = [&] {
        m_gl->doneCurrent();
        m_gl->moveToThread(guiThread);
    };
    if (!m_gl->makeCurrent(this)) {
        qWarning("Lumen: OpenGL-Kontext des Player-Fensters lässt sich nicht aktivieren");
        giveBack();
        return;
    }
    mpv_opengl_init_params gl{getProcAddress, nullptr};
    mpv_render_param initParams[] = {
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
    if (mpv_render_context_create(&m_ctx, m_mpv, initParams) < 0) {
        qWarning("Lumen: mpv-Render-Kontext konnte nicht erstellt werden");
        m_ctx = nullptr;
        giveBack();
        return;
    }
    mpv_render_context_set_update_callback(m_ctx, updateCallback, this);
    m_ready = true;
    QMetaObject::invokeMethod(this, &PlayerWindow::renderReady, Qt::QueuedConnection);

    bool current = true; // der Kontext ist in diesem Thread aktiv
    for (;;) {
        bool redraw = false, grab = false;
        {
            QMutexLocker lock(&m_lock);
            while (!m_quit && !m_wake)
                m_cond.wait(&m_lock);
            if (m_quit)
                break;
            m_wake = false;
            if (m_handover) {
                // an den Haupt-Thread abgeben und warten, bis er zurück ist (syncContextWithWindow)
                m_gl->doneCurrent();
                m_gl->moveToThread(guiThread);
                m_released = true;
                m_handoverCond.wakeAll();
                // Der Haupt-Thread gibt ihn in jedem Fall zurück (nur er setzt auch m_quit)
                while (m_released)
                    m_handoverCond.wait(&m_lock);
                lock.unlock();
                current = m_exposed.load() && m_gl->makeCurrent(this);
                lock.relock();
                m_reclaimed = true;
                m_handoverCond.wakeAll();
                m_redraw = true;
            }
            redraw = std::exchange(m_redraw, false);
            grab = m_grab;
        }
        const bool frame = mpv_render_context_update(m_ctx) & MPV_RENDER_UPDATE_FRAME;
        if (!frame && !redraw && !grab)
            continue;
        // Verdecktes oder minimiertes Fenster: nicht zeichnen (mpv verwirft die Bilder, der Ton läuft)
        const int w = m_pixelWidth.load(), h = m_pixelHeight.load();
#ifndef Q_OS_MACOS
        current = false; // überall sonst: vor jedem Bild aktivieren (folgt dem Fenster von selbst)
#endif
        if (!m_exposed.load() || w <= 0 || h <= 0 || (!current && !(current = m_gl->makeCurrent(this)))) {
            if (grab) {
                QMutexLocker lock(&m_lock);
                m_grab = false;
                m_grabbed = QImage();
                m_grabDone.wakeAll();
            }
            continue;
        }
        mpv_opengl_fbo fbo{0, w, h, 0};
        int flipY = 1;
        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
            {MPV_RENDER_PARAM_FLIP_Y, &flipY},
            {MPV_RENDER_PARAM_INVALID, nullptr},
        };
        mpv_render_context_render(m_ctx, params);
        if (grab) {
            QImage img(w, h, QImage::Format_RGBA8888);
            QOpenGLFunctions *f = m_gl->functions();
            f->glPixelStorei(GL_PACK_ALIGNMENT, 4);
            f->glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, img.bits());
            QMutexLocker lock(&m_lock);
            m_grab = false;
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
            m_grabbed = img.flipped(Qt::Vertical);
#else
            m_grabbed = img.mirrored();
#endif
            m_grabDone.wakeAll();
        }
        m_gl->swapBuffers(this);
        mpv_render_context_report_swap(m_ctx);
        ++m_paints;
    }

    if (!current)
        m_gl->makeCurrent(this);
    mpv_render_context_set_update_callback(m_ctx, nullptr, nullptr);
    mpv_render_context_free(m_ctx);
    m_ctx = nullptr;
    giveBack();
}

QImage PlayerWindow::grabFramebuffer()
{
    QMutexLocker lock(&m_lock);
    if (!m_thread || !m_ready.load())
        return {};
    m_grab = true;
    m_wake = true;
    m_redraw = true;
    m_cond.wakeOne();
    m_grabDone.wait(&m_lock, 2000);
    m_grab = false;
    return std::exchange(m_grabbed, QImage());
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
        return QWindow::keyPressEvent(e);
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
    return QWindow::event(e);
}
