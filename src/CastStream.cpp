#include "CastStream.h"

#include <QMutexLocker>

#include <algorithm>
#include <cmath>

CastStream::CastStream(QObject *parent)
    : QObject(parent)
{
}

void CastStream::reset()
{
    QMutexLocker lock(&m_mutex);
    m_segments.clear();
    m_open.clear();
    m_next = 0;
}

void CastStream::append(const QByteArray &bytes)
{
    {
        QMutexLocker lock(&m_mutex);
        m_open.append(bytes);
    }
    emit dataAvailable();
}

void CastStream::closeSegment(double duration)
{
    qint64 count;
    {
        QMutexLocker lock(&m_mutex);
        if (m_open.isEmpty())
            return;
        m_segments.append(Segment{m_next++, duration, m_open});
        m_open = QByteArray();
        while (m_segments.size() > kKeep)
            m_segments.removeFirst();
        count = m_next;
    }
    emit segmentClosed(count);
}

qint64 CastStream::segmentCount() const
{
    QMutexLocker lock(&m_mutex);
    return m_next;
}

QByteArray CastStream::playlist(const QString &segmentPrefix) const
{
    QMutexLocker lock(&m_mutex);
    const int n = std::min<int>(kPlaylist, m_segments.size());
    const auto first = m_segments.cend() - n;
    double longest = 1;
    for (auto it = first; it != m_segments.cend(); ++it)
        longest = std::max(longest, it->duration);

    QByteArray out = "#EXTM3U\n#EXT-X-VERSION:3\n";
    out += "#EXT-X-TARGETDURATION:" + QByteArray::number(int(std::ceil(longest))) + "\n";
    out += "#EXT-X-MEDIA-SEQUENCE:" + QByteArray::number(n ? first->index : m_next) + "\n";
    for (auto it = first; it != m_segments.cend(); ++it) {
        out += "#EXTINF:" + QByteArray::number(it->duration, 'f', 3) + ",\n";
        out += segmentPrefix.toUtf8() + QByteArray::number(it->index) + ".ts\n";
    }
    return out;
}

QByteArray CastStream::segment(qint64 index) const
{
    QMutexLocker lock(&m_mutex);
    for (const Segment &s : m_segments) {
        if (s.index == index)
            return s.data;
    }
    return {};
}

bool CastStream::read(Cursor &cursor, QByteArray &out) const
{
    QMutexLocker lock(&m_mutex);
    if (cursor.segment < 0) {
        // Einstieg: jüngstes abgeschlossenes Segment (beginnt mit PAT/PMT + Schlüsselbild)
        cursor.segment = m_segments.isEmpty() ? m_next : m_segments.constLast().index;
        cursor.offset = 0;
    }
    const qint64 oldest = m_segments.isEmpty() ? m_next : m_segments.constFirst().index;
    if (cursor.segment < oldest)
        return false;
    for (const Segment &s : m_segments) {
        if (s.index < cursor.segment)
            continue;
        out.append(s.data.constData() + cursor.offset, s.data.size() - cursor.offset);
        cursor.segment = s.index + 1;
        cursor.offset = 0;
    }
    if (cursor.segment == m_next && m_open.size() > cursor.offset) {
        out.append(m_open.constData() + cursor.offset, m_open.size() - cursor.offset);
        cursor.offset = m_open.size();
    }
    return true;
}
