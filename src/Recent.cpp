#include "Recent.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>

Recent::Recent(QObject *parent)
    : QObject(parent)
{
    const QByteArray json = QSettings().value(QStringLiteral("recent/items")).toString().toUtf8();
    m_items = QJsonDocument::fromJson(json).array().toVariantList();
}

QVariantList Recent::items() const
{
    QVariantList out;
    for (const QVariant &v : m_items) {
        // Wechselmedien und gelöschte Dateien nicht anbieten
        if (QFileInfo::exists(v.toMap().value("path").toString()))
            out << v;
    }
    return out;
}

int Recent::indexOf(const QString &path) const
{
    const QString key = QDir::cleanPath(path);
    for (int i = 0; i < m_items.size(); ++i)
        if (QDir::cleanPath(m_items[i].toMap().value("path").toString()).compare(key, Qt::CaseInsensitive) == 0)
            return i;
    return -1;
}

void Recent::note(const QString &path, const QString &kind, const QString &title)
{
    if (path.isEmpty() || path.contains(QLatin1String("://")))
        return;
    const int i = indexOf(path);
    QVariantMap item = i >= 0 ? m_items.takeAt(i).toMap()
                              : QVariantMap{{"path", QDir::cleanPath(path)}, {"position", 0.0}, {"duration", 0.0}};
    item["kind"] = kind;
    if (!title.isEmpty())
        item["title"] = title;
    else if (!item.contains("title"))
        item["title"] = QFileInfo(path).completeBaseName().isEmpty() ? QDir::toNativeSeparators(path) : QFileInfo(path).completeBaseName();
    m_items.prepend(item);
    while (m_items.size() > kMax)
        m_items.removeLast();
    save();
}

void Recent::notePosition(const QString &path, double position, double duration)
{
    const int i = indexOf(path);
    if (i < 0 || duration <= 0)
        return;
    QVariantMap item = m_items[i].toMap();
    const bool finished = position > duration - 15 || position > duration * 0.97;
    item["position"] = finished ? 0.0 : position;
    item["duration"] = duration;
    m_items[i] = item;
    save();
}

void Recent::remove(const QString &path)
{
    const int i = indexOf(path);
    if (i < 0)
        return;
    m_items.removeAt(i);
    save();
}

void Recent::clear()
{
    m_items.clear();
    save();
}

void Recent::save()
{
    QSettings().setValue(QStringLiteral("recent/items"),
                         QString::fromUtf8(QJsonDocument(QJsonArray::fromVariantList(m_items)).toJson(QJsonDocument::Compact)));
    emit changed();
}
