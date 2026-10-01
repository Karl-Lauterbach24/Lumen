#include "AudioTap.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QRandomGenerator>

#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#endif

namespace {
// Fassungsvermögen der Pipe; der Leser leert sie alle paar Millisekunden
constexpr int kPipeBytes = 256 * 1024;
}

AudioTap::AudioTap() = default;

AudioTap::~AudioTap()
{
    close();
}

#ifdef _WIN32

bool AudioTap::open()
{
    close();
    m_path = QStringLiteral("\\\\.\\pipe\\lumen-cast-%1-%2")
                 .arg(QCoreApplication::applicationPid())
                 .arg(QRandomGenerator::global()->generate(), 8, 16, QLatin1Char('0'));
    for (int i = 0; i < 2; ++i) {
        // PIPE_NOWAIT: Verbinden und Lesen kehren sofort zurück; nur lokale Verbindungen
        HANDLE h = CreateNamedPipeW(reinterpret_cast<LPCWSTR>(m_path.utf16()), PIPE_ACCESS_INBOUND,
                                    PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                    2, 0, kPipeBytes, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            close();
            return false;
        }
        m_pipe[i] = h;
        m_connected[i] = false;
    }
    poll();
    return true;
}

void AudioTap::close()
{
    for (int i = 0; i < 2; ++i) {
        if (m_pipe[i]) {
            DisconnectNamedPipe(m_pipe[i]);
            CloseHandle(m_pipe[i]);
            m_pipe[i] = nullptr;
        }
        m_connected[i] = false;
    }
    m_path.clear();
}

void AudioTap::poll()
{
    for (int i = 0; i < 2; ++i) {
        if (!m_pipe[i] || m_connected[i])
            continue;
        if (ConnectNamedPipe(m_pipe[i], nullptr)) {
            m_connected[i] = true;
            continue;
        }
        const DWORD err = GetLastError();
        if (err == ERROR_PIPE_CONNECTED)
            m_connected[i] = true;
        else if (err == ERROR_NO_DATA)
            DisconnectNamedPipe(m_pipe[i]); // der vorige Schreiber hat geschlossen: wieder frei machen
    }
}

int AudioTap::read(char *data, int maxBytes)
{
    poll();
    if (maxBytes <= 0)
        return 0;
    for (int i = 0; i < 2; ++i) {
        if (!m_connected[i])
            continue;
        DWORD avail = 0;
        if (!PeekNamedPipe(m_pipe[i], nullptr, 0, nullptr, &avail, nullptr)) {
            // mpv hat die Ausgabe geschlossen (Dateiwechsel, Stopp)
            DisconnectNamedPipe(m_pipe[i]);
            m_connected[i] = false;
            continue;
        }
        if (avail == 0)
            continue;
        DWORD got = 0;
        if (ReadFile(m_pipe[i], data, std::min<DWORD>(avail, DWORD(maxBytes)), &got, nullptr) && got > 0)
            return int(got);
    }
    return 0;
}

int AudioTap::pending() const
{
    int total = 0;
    for (int i = 0; i < 2; ++i) {
        DWORD avail = 0;
        if (m_pipe[i] && m_connected[i] && PeekNamedPipe(m_pipe[i], nullptr, 0, nullptr, &avail, nullptr))
            total += int(avail);
    }
    return total;
}

#else

bool AudioTap::open()
{
    close();
    m_path = QDir::tempPath() + QStringLiteral("/lumen-cast-%1-%2.pcm")
                 .arg(QCoreApplication::applicationPid())
                 .arg(QRandomGenerator::global()->generate(), 8, 16, QLatin1Char('0'));
    const QByteArray p = QFile::encodeName(m_path);
    ::unlink(p.constData());
    if (::mkfifo(p.constData(), 0600) != 0) {
        m_path.clear();
        return false;
    }
    m_fd = ::open(p.constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (m_fd >= 0)
        m_keep = ::open(p.constData(), O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (m_fd < 0 || m_keep < 0) {
        close();
        return false;
    }
#ifdef F_SETPIPE_SZ
    ::fcntl(m_fd, F_SETPIPE_SZ, kPipeBytes); // Linux; sonst gilt die Systemgröße
#endif
    return true;
}

void AudioTap::close()
{
    if (m_fd >= 0)
        ::close(m_fd);
    if (m_keep >= 0)
        ::close(m_keep);
    m_fd = m_keep = -1;
    if (!m_path.isEmpty())
        ::unlink(QFile::encodeName(m_path).constData());
    m_path.clear();
}

int AudioTap::read(char *data, int maxBytes)
{
    if (m_fd < 0 || maxBytes <= 0)
        return 0;
    const ssize_t n = ::read(m_fd, data, size_t(maxBytes));
    return n > 0 ? int(n) : 0;
}

int AudioTap::pending() const
{
    if (m_fd < 0)
        return 0;
    int avail = 0;
    return ::ioctl(m_fd, FIONREAD, &avail) == 0 ? avail : 0;
}

#endif
