#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

// Einträge eines ZIP-Archivs lesen – genug für die KDM-Pakete, die Verleihe per E-Mail schicken
// (ein KDM je Saal bzw. Zertifikat). Gespeicherte und mit Deflate gepackte Einträge; ohne
// Verschlüsselung und ZIP64.
namespace Zip {

struct Entry
{
    QString name;
    QByteArray data;
};

// Alle Einträge, deren Name auf suffix endet (leer: alle). maxBytes begrenzt die entpackte Gesamtgröße.
QList<Entry> read(const QString &file, const QString &suffix, QString *error, qint64 maxBytes = 64 << 20);

} // namespace Zip
