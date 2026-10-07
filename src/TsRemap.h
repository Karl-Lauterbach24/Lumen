#pragma once

#include <QHash>
#include <QList>
#include <QSet>

#include <cstddef>
#include <cstdint>

// Gibt einem Strom, dessen Format sich von Playlist zu Playlist ändert, eine eigene PID.
//
// Die Playlists einer Blu-ray benutzen dieselben PIDs für verschiedene Formate: der Ton des Menüs
// liegt als AC-3 auf 0x1100, der des Films als DTS-HD auf derselben PID. Ein MPEG-TS-Demuxer legt
// das Format einer PID mit der ersten Programmtabelle (PMT) fest und bleibt dabei – der Decoder
// bekäme DTS als AC-3 und lieferte Fehler statt Ton. Darum bekommt jede Paarung aus PID und Format,
// die von der zuerst gesehenen abweicht, eine eigene PID: in den Paketen und in der PMT (deren
// Prüfsumme wird neu gerechnet). Der Demuxer sieht einen neuen Strom, der Spieler eine neue Spur.
//
// Auch eine Tonspur, die eine Playlist lang gefehlt hat, bekommt eine neue PID: im Demuxer liegt
// von ihr womöglich noch ein abgeschnittenes Paket (die Playlist wurde mit einer Taste verlassen),
// das beim Wiedersehen als erstes käme – mit der Zeit von damals. mpv setzt dann die Wiedergabe
// zurück. Reichen die eigenen PIDs nicht mehr (127 je Bereich), bleibt es bei der bekannten.
//
// Die eigene PID behält das obere Byte der alten (0x1100 -> 0x1180 …; die Disc nutzt je Bereich nur
// 0x00–0x1f): so ändert sich in der PMT nur das untere Byte, und sie lässt sich im Durchlauf
// umschreiben, auch wenn ein Abschnitt über Paket- oder Blockgrenzen reicht.
class TsRemap
{
public:
    struct Stream
    {
        uint16_t pid = 0;
        uint8_t type = 0; // stream_type der PMT (= coding_type der Playlist)
    };

    // Ströme des Clips, der jetzt beginnt
    void setStreams(const QList<Stream> &streams);
    // PID eines Stroms in der Ausgabe und zurück
    uint16_t map(uint16_t pid) const { return m_map.value(pid, pid); }
    uint16_t unmap(uint16_t pid) const { return m_back.value(pid, pid); }
    // 192-Byte-M2TS-Pakete an Ort und Stelle umschreiben
    void process(uint8_t *data, size_t size);

private:
    void pmtByte(uint8_t &b);

    QHash<uint16_t, uint8_t> m_announced; // Ausgabe-PID -> Format, wie es der Demuxer kennt
    QHash<uint32_t, uint16_t> m_alias;    // PID << 8 | Format -> eigene PID
    QHash<uint8_t, int> m_used;           // oberes Byte -> vergebene eigene PIDs
    QHash<uint16_t, uint16_t> m_map;      // laufender Clip: nur die Abweichungen
    QHash<uint16_t, uint16_t> m_back;
    QSet<uint16_t> m_live;                // Ausgabe-PIDs des laufenden Clips

    // PMT-Abschnitt im Durchlauf
    int m_pos = -1;         // Stelle im Abschnitt, -1 = auf den nächsten Beginn warten
    int m_length = 0;       // Länge des Abschnitts samt Prüfsumme
    int m_es = 0;           // Beginn des laufenden Eintrags
    int m_esInfo = 0;
    uint8_t m_pidHigh = 0;
    uint32_t m_crc = 0;
    bool m_dirty = false;
};
