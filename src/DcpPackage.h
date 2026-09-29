#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>

// Digital Cinema Package (SMPTE ST 429 / Interop): ASSETMAP, PKL und CPL lesen.
//
//   ASSETMAP ─> Asset-UUID -> Datei       (auch aus benachbarten Paketen: OV/VF)
//   PKL      ─> Größe, SHA-1 (Prüfung), Typ
//   CPL      ─> Rollen (Reels) mit Bild/Ton/Untertitel, Einstiegspunkt, Dauer,
//               Schlüssel-ID (verschlüsselt), Marker (FFOC, LFOC, FFEC …)
namespace Dcp {

struct Rational
{
    qint64 num = 24;
    qint64 den = 1;
    double value() const { return den ? double(num) / double(den) : 0.0; }
    bool valid() const { return num > 0 && den > 0; }
};

struct MxfInfo
{
    bool valid = false;
    bool encrypted = false;
    bool stereoscopic = false;
    int width = 0;
    int height = 0;
    int channels = 0;
    int sampleRate = 0;
    int bitsPerSample = 0;
    QString codec; // "J2K", "PCM", "IAB", "TimedText" …
};

struct Asset
{
    QString id;
    QString path;        // absolut, leer = nicht gefunden
    qint64 size = -1;    // aus PKL
    QString hash;        // Base64-SHA-1 aus PKL
    QString type;        // MIME aus PKL
};

enum class Kind { Picture, StereoPicture, Sound, Subtitle, ClosedCaption, Atmos, Other };

struct ReelAsset
{
    Kind kind = Kind::Other;
    QString id;
    QString file;
    QString keyId;       // leer = unverschlüsselt
    qint64 entryPoint = 0;
    qint64 duration = 0;       // in Edit Units
    qint64 intrinsic = 0;
    Rational editRate;
    Rational frameRate;
    QString language;
    MxfInfo mxf;
    bool encrypted() const { return !keyId.isEmpty(); }
    double startSeconds() const { return editRate.valid() ? entryPoint / editRate.value() : 0; }
    double lengthSeconds() const { return editRate.valid() ? duration / editRate.value() : 0; }
};

struct Marker
{
    QString label;
    qint64 offset = 0; // Edit Units ab Rollenbeginn
};

struct Reel
{
    QString id;
    QList<ReelAsset> assets;
    QList<Marker> markers;
    Rational editRate;
    qint64 duration = 0;
    const ReelAsset *find(Kind k) const;
    double seconds() const { return editRate.valid() ? duration / editRate.value() : 0; }
};

struct Cpl
{
    QString id;
    QString file;
    QString root;         // Paketordner
    QString title;
    QString annotation;
    QString issuer;
    QString creator;
    QString issueDate;
    QString contentKind;
    QString contentVersion;
    QString aspect;
    QStringList ratings;
    bool smpte = true;
    Rational editRate;
    QList<Reel> reels;
    QStringList missing;  // Asset-IDs ohne Datei (VF ohne OV?)

    bool encrypted() const;
    bool stereoscopic() const;
    QStringList keyIds() const;
    double seconds() const;
    QVariantMap toVariant() const;
};

struct Package
{
    QString root;
    QList<Cpl> cpls;
    QHash<QString, Asset> assets; // inkl. benachbarter Pakete
    QString error;
};

// Ordner (oder ASSETMAP-Datei, CPL-Datei) einlesen. extraRoots: weitere Pakete (OV).
Package scan(const QString &path, const QStringList &extraRoots = {});
// Liegt unter root ein DCP (ASSETMAP / ASSETMAP.xml)?
bool isDcp(const QString &root);
// Kopf eines MXF auslesen (Auflösung, Kanäle, verschlüsselt, stereoskopisch)
MxfInfo probeMxf(const QString &file);
QString normalizeUuid(QString s);
QString kindName(Kind k);

} // namespace Dcp
