#pragma once

#include <cstddef>
#include <cstdint>

// Verschiebt die Zeitstempel eines M2TS-Stroms (192-Byte-Pakete: 4 Byte Ankunftszeit + 188 Byte TS)
// um offset Ticks zu 90 kHz: PTS und DTS am Beginn jedes PES-Pakets (Bild, Ton, Grafik) und die PCR.
// Damit liegen die Clips einer Blu-ray-Playlist, die ihre Zeit jeder für sich zählen, auf der
// Zeitachse der Playlist – der Spieler sieht an einer Clipgrenze keinen Zeitsprung.
void retimeM2ts(uint8_t *data, size_t size, int64_t offset);

// Zeitstempel (PTS, 90 kHz) der PES-Pakete einer PID in einem M2TS-Block: die erste im Strom und die
// höchste, je -1, wenn in dem Block kein PES-Paket dieser PID beginnt. Damit schließt eine neue
// Playlist an das zuletzt ausgegebene Bild an (jede Playlist zählt ihre Zeit von vorn).
struct TsStamps
{
    int64_t first = -1;
    int64_t max = -1;
};
TsStamps scanM2ts(const uint8_t *data, size_t size, int pid);
