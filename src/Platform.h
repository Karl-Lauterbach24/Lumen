#pragma once

#include <QString>
#include <QStringList>
#include <QSysInfo>

// Plattformkennungen für Plugins (plugin.json, Store-Index):
//   "windows", "macos", "linux"            Betriebssystem
//   "windows-arm64", "linux-arm64", …      Betriebssystem + Architektur (x64 | arm64)
// Ohne Architektur gilt für Binärdateien: Windows/Linux = x64 (so wurden sie bisher
// gebaut), macOS = beide Architekturen (Universal-Bibliothek).
namespace Platform {

inline QString os()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#else
    return QStringLiteral("linux");
#endif
}

inline QString arch()
{
    return QSysInfo::buildCpuArchitecture() == QLatin1String("arm64") ? QStringLiteral("arm64") : QStringLiteral("x64");
}

inline QString osArch()
{
    return os() + QLatin1Char('-') + arch();
}

// Kennungen, deren Binärdateien dieser Build laden kann (genaueste zuerst)
inline QStringList binaryTags()
{
    QStringList tags{osArch()};
#if defined(Q_OS_MACOS)
    tags << os();
#else
    if (arch() == QLatin1String("x64"))
        tags << os();
#endif
    return tags;
}

} // namespace Platform
