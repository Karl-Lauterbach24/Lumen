#pragma once

#include <QStringList>

// Disc-Menüs in Java (BD-J): was libbluray dafür braucht, findet es über die Umgebung.
namespace BdjSetup {
// Vor dem ersten Öffnen einer Disc aufrufen. Setzt LIBBLURAY_CP auf den Ordner mit libblurays
// Java-Archiv in der Version der Bibliothek (zuerst extraDirs, dann "bdj" im Datenordner, neben dem
// Programm und im Paket) und JAVA_HOME auf eine gefundene Java-Laufzeit. Schon gesetzte Variablen
// bleiben, wie sie sind.
void prepare(const QStringList &extraDirs = {});
}
