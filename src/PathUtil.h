#pragma once

#include <QDir>
#include <QMutex>
#include <QString>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

// Pfad für libbluray aufbereiten. libbluray öffnet Dateien unter Windows ohne
// Langpfad-Unterstützung (und normalisiert "\\?\" weg) – Discs/ISOs in tief
// verschachtelten Ordnern (> 260 Zeichen inkl. "\BDMV\PLAYLIST\xxxxx.mpls")
// würden sonst nicht gefunden. Abhilfe: 8.3-Kurzpfad des Wurzelordners.
inline QString blurayPath(const QString &path)
{
#ifdef Q_OS_WIN
    const QString native = QDir::toNativeSeparators(path);
    if (native.size() <= 180)
        return native;
    const std::wstring in = native.toStdWString();
    const DWORD len = GetShortPathNameW(in.c_str(), nullptr, 0);
    if (len == 0)
        return native;
    std::wstring out(len, L'\0');
    if (GetShortPathNameW(in.c_str(), out.data(), len) == 0)
        return native;
    out.resize(len - 1);
    return QString::fromStdWString(out);
#else
    return path;
#endif
}

// bd_open() liest die Disc-Metadaten (bdmt_*.xml) und räumt danach libxml2 global auf
// (xmlCleanupParser). Zwei gleichzeitige Aufrufe – Disc-Übersicht und Wiedergabe öffnen dieselbe
// Disc in verschiedenen Threads – geben dabei denselben Speicher zweimal frei. Deshalb nacheinander.
inline QMutex &blurayOpenMutex()
{
    static QMutex mutex;
    return mutex;
}
