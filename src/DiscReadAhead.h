#pragma once

#include <QString>

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

// Liest einer Disc voraus, damit die Wiedergabe nicht am Laufwerk hängt.
//
// Ein optisches Laufwerk liefert nicht gleichmäßig: Das Betriebssystem holt die Daten in Schüben
// (macOS: rund 3 MB am Stück, der Aufruf wartet so lange), das Laufwerk wechselt die Drehzahl und
// setzt dabei für eine halbe bis mehrere Sekunden aus. Mit dem Menü der Disc spielt Lumen einen
// linearen Strom mit kleinem Puffer (0,4 s, damit Eingaben im Menü sofort wirken) – jede solche
// Pause stand dann als Hänger im Bild.
//
// Darum liest ein eigener Thread dieselbe Datei über einen zweiten Zugriff voraus und hält so den
// Dateipuffer des Betriebssystems gefüllt: rund 25 Sekunden vor der Stelle, an der die Bibliothek
// (libbluray) gerade liest. Deren Lesen trifft dann auf Daten im Arbeitsspeicher; setzt das
// Laufwerk aus, wartet nur der Vorausleser. Die Bibliothek selbst liest unverändert – ihre
// Position, die Menüs und alles, was daran hängt, bleiben, wie sie sind.
//
// Erkannt werden fortlaufende Leseströme: je Datei bis zu vier (ein Abbild oder Laufwerk ist eine
// einzige "Datei", in der Bild und zweite Ansicht einer Blu-ray 3D nebeneinander fortlaufen).
// Einzelne kurze Zugriffe (Verzeichnisse, Playlists) lösen nichts aus.
class DiscReadAhead
{
public:
    // Zweiter Zugriff auf eine Datei; nur der Vorauslese-Thread benutzt ihn.
    class Reader
    {
    public:
        virtual ~Reader() = default;
        // Liest ab offset; Rückgabe: gelesene Bytes, 0 am Ende, < 0 bei einem Fehler
        virtual qint64 readAt(qint64 offset, char *buffer, qint64 size) = 0;
    };
    using ReaderFactory = std::function<std::unique_ptr<Reader>()>;

    static DiscReadAhead &instance();
    // LUMEN_NO_READAHEAD=1 schaltet das Vorauslesen ab (Vergleichsmessungen, Fehlersuche)
    static bool enabled();

    // Die Bibliothek hat eine Datei geöffnet. key: woran dieselbe Datei wiedererkannt wird (Pfad).
    // Rückgabe: Kennung für noteRead()/close().
    int open(const QString &key, qint64 size, ReaderFactory factory);
    void close(int id);
    // Die Bibliothek hat an dieser Stelle gelesen
    void noteRead(int id, qint64 offset, qint64 size);
    // Diese Datei liest die Bibliothek als Nächste von vorn (folgender Clip der Playlist): Ihr Anfang
    // wird vorausgelesen, sobald für die laufende nichts mehr zu tun ist. Leerer key: keine.
    void hintNext(const QString &key, qint64 size, ReaderFactory factory);

    // Ein Leseaufruf der Quelle (bd_read, dvdnav_get_next_block) hat so lange gedauert. Was darin
    // über 100 ms liegt, zählt als Aussetzer der Quelle: Bilder, die deshalb fehlen, hat nicht die
    // Maschine verloren (siehe Tuning::Governor).
    static void noteSourceRead(double seconds);

    struct Stats
    {
        int streams = 0;            // fortlaufende Leseströme, denen vorausgelesen wird
        qint64 leadBytes = 0;       // kleinster Vorsprung über diese Ströme
        qint64 windowBytes = 0;     // angestrebter Vorsprung
        double consumeRate = 0;     // Byte/s, die die Bibliothek liest
        double driveRate = 0;       // Byte/s, die der Vorausleser zuletzt bekam
        qint64 prefetched = 0;      // vorausgelesene Bytes seit der letzten Abfrage
        double prefetchWorst = 0;   // längster einzelner Lesevorgang des Vorauslesers, Sekunden
        double sourceBlocked = 0;   // Summe der Leseaufrufe der Quelle über 100 ms, Sekunden
        double sourceWorst = 0;     // längster davon
    };
    // Stand seit der letzten Abfrage (die Zähler beginnen danach neu)
    Stats takeStats();

    // Nur für Tests: wartet, bis der Vorausleser nichts mehr zu tun hat (oder die Zeit um ist)
    bool waitIdle(int timeoutMs);

private:
    DiscReadAhead();
    struct File;
    struct Cursor;
    void run();
    bool pick(std::shared_ptr<File> &file, int &cursor, qint64 &offset, qint64 &size);
    qint64 windowFor(const Cursor &c) const;

    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    std::condition_variable m_idleCond;
    std::vector<std::shared_ptr<File>> m_files;
    std::shared_ptr<File> m_next; // Hinweis auf die folgende Datei
    int m_nextId = 1;
    bool m_started = false;
    bool m_busy = false;
    Stats m_stats;
    double m_driveRate = 0;
};

// libbluray: jede Datei, die die Bibliothek öffnet (Clips einer eingehängten Disc, ein Abbild,
// unter Linux das Laufwerk selbst), geht durch DiscReadAhead. Gilt für alles, was im Prozess
// libbluray benutzt – die Wiedergabe mit Menü, die Disc-Übersicht und mpvs bd://.
namespace BlurayReadAhead {
// Einmal beim Start, vor dem ersten bd_open()
void install();
// Der Clip, den die Playlist als Nächsten spielt (Pfad seiner Datei auf der Disc); leer = keiner
void hintNext(const QString &path);
} // namespace BlurayReadAhead
