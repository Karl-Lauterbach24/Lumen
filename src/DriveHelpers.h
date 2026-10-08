#pragma once

#include <QtGlobal>

#include <vector>

// Hilfsprozesse von Disc-Bibliotheken.
//
// MakeMKV entschlüsselt über einen eigenen Prozess ("makemkvcon guiserver"), den seine Bibliothek
// für jede geöffnete Disc startet. Er hält das Laufwerk, solange er läuft – und er endet nicht von
// selbst, wenn das Programm ohne bd_close() verschwindet (Absturz, erzwungenes Ende): Er erbt beide
// Enden seiner eigenen Verbindung und sieht deren Ende darum nie. Zurück bleibt ein Prozess ohne
// Programm, der weiter mit dem Laufwerk spricht; das nächste Öffnen der Disc dauert dann, und das
// Lesen setzt sekundenlang aus.
namespace DriveHelpers {

// Hilfsprozesse des Nutzers beenden, deren Programm nicht mehr läuft. Rückgabe: wie viele.
int endOrphans();
// Die Hilfsprozesse dieses Prozesses (ihre Prozessnummern)
std::vector<qint64> own();
// Die Hilfsprozesse dieses Prozesses beenden: vor einem Ende, das den Abbau überspringt – oder
// wenn das Öffnen einer Disc nicht zurückkehrt (ihr Hilfsprozess steht dann im Laufwerk und hält
// es für alle anderen besetzt). keep: diese nicht (was vor dem Öffnen schon lief).
int endOwn(const std::vector<qint64> &keep = {});

} // namespace DriveHelpers
