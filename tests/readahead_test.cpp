// Vorauslesen einer Disc (DiscReadAhead): Erkennt es fortlaufende Leseströme, hält es den
// Vorsprung, lässt es einzelne Zugriffe in Ruhe, und hält ein stehendes Laufwerk nur den
// Vorausleser auf – nie den Aufruf der Bibliothek?
#include "DiscReadAhead.h"

#include <QElapsedTimer>
#include <QString>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

static int failures = 0;

static void check(bool ok, const QString &what)
{
    std::printf("%s %s\n", ok ? "OK  " : "FAIL", qPrintable(what));
    if (!ok)
        ++failures;
}

namespace {

constexpr qint64 MiB = qint64(1) << 20;

// Was ein zweiter Zugriff gelesen hat
struct Trace
{
    std::mutex mutex;
    qint64 bytes = 0;
    qint64 reach = 0;  // höchste gelesene Stelle
    qint64 lowest = -1;
    int calls = 0;
    int opened = 0;
    int closed = 0;
    std::atomic<int> delayMs{0};   // jeder Lesevorgang dauert so lange (stehendes Laufwerk)
    std::atomic<qint64> badFrom{-1}; // ab hier Lesefehler
    qint64 size = 0;
};

class FakeReader final : public DiscReadAhead::Reader
{
public:
    explicit FakeReader(Trace *t) : m_t(t)
    {
        std::lock_guard<std::mutex> lock(m_t->mutex);
        ++m_t->opened;
    }
    ~FakeReader() override
    {
        std::lock_guard<std::mutex> lock(m_t->mutex);
        ++m_t->closed;
    }
    qint64 readAt(qint64 offset, char *buffer, qint64 size) override
    {
        if (const int ms = m_t->delayMs.load())
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        std::lock_guard<std::mutex> lock(m_t->mutex);
        ++m_t->calls;
        const qint64 bad = m_t->badFrom.load();
        if (bad >= 0 && offset + size > bad)
            return -1;
        const qint64 n = std::min(size, m_t->size - offset);
        if (n <= 0)
            return 0;
        std::memset(buffer, 0, size_t(n));
        m_t->bytes += n;
        m_t->reach = std::max(m_t->reach, offset + n);
        m_t->lowest = m_t->lowest < 0 ? offset : std::min(m_t->lowest, offset);
        return n;
    }

private:
    Trace *m_t;
};

DiscReadAhead::ReaderFactory factory(Trace *t)
{
    return [t] { return std::make_unique<FakeReader>(t); };
}

// die Bibliothek liest fortlaufend: n Bytes ab pos in Stücken von 6144 * 32
qint64 readOn(DiscReadAhead &ra, int id, qint64 pos, qint64 n)
{
    const qint64 step = 6144 * 32;
    for (qint64 done = 0; done < n; done += step, pos += step)
        ra.noteRead(id, pos, step);
    return pos;
}

qint64 reach(Trace &t)
{
    std::lock_guard<std::mutex> lock(t.mutex);
    return t.reach;
}

qint64 bytes(Trace &t)
{
    std::lock_guard<std::mutex> lock(t.mutex);
    return t.bytes;
}

// Der Vorauslese-Thread gibt einen zweiten Zugriff frei, sobald er die geschlossene Datei austrägt
bool allClosed(Trace &t, int timeoutMs = 3000)
{
    QElapsedTimer timer;
    timer.start();
    for (;;) {
        {
            std::lock_guard<std::mutex> lock(t.mutex);
            if (t.opened == t.closed)
                return true;
        }
        if (timer.elapsed() > timeoutMs)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

} // namespace

int main()
{
    DiscReadAhead &ra = DiscReadAhead::instance();

    // 1. Ein Clip, von vorn gelesen: nach dem ersten Megabyte beginnt das Vorauslesen und hält 48 MiB
    Trace clip;
    clip.size = 2048 * MiB;
    const int id = ra.open(QStringLiteral("/disc/BDMV/STREAM/00001.m2ts"), clip.size, factory(&clip));
    ra.noteRead(id, 0, 6144);
    check(ra.waitIdle(2000) && reach(clip) == 0, QStringLiteral("ein einzelner Zugriff löst nichts aus"));
    qint64 pos = readOn(ra, id, 6144, 2 * MiB);
    check(ra.waitIdle(5000), QStringLiteral("Vorausleser kommt zur Ruhe"));
    check(reach(clip) - pos >= 47 * MiB && reach(clip) - pos <= 49 * MiB,
          QStringLiteral("Vorsprung nach dem Beginn: %1 MiB").arg((reach(clip) - pos) / MiB));
    check(clip.lowest >= 6144 && clip.lowest <= pos, QStringLiteral("gelesen wird ab der Stelle der Bibliothek, nichts doppelt davor"));
    DiscReadAhead::Stats s = ra.takeStats();
    check(s.streams == 1 && s.leadBytes >= 47 * MiB && s.windowBytes == 48 * MiB,
          QStringLiteral("Stand: %1 Strom, Vorsprung %2 von %3 MiB").arg(s.streams).arg(s.leadBytes / MiB).arg(s.windowBytes / MiB));

    // 2. Die Bibliothek liest weiter: der Vorsprung bleibt
    pos = readOn(ra, id, pos, 30 * MiB);
    ra.waitIdle(5000);
    check(reach(clip) - pos >= 47 * MiB && reach(clip) - pos <= 49 * MiB,
          QStringLiteral("Vorsprung nach 30 MiB: %1 MiB").arg((reach(clip) - pos) / MiB));

    // 3. Sprung: dort beginnt ein neuer Strom, sobald wieder fortlaufend gelesen wird
    pos = readOn(ra, id, 1000 * MiB, 2 * MiB);
    ra.waitIdle(5000);
    check(reach(clip) - pos >= 47 * MiB && reach(clip) - pos <= 49 * MiB,
          QStringLiteral("nach einem Sprung: %1 MiB vor der neuen Stelle").arg((reach(clip) - pos) / MiB));

    // 4. Hoher Datenstrom (UHD): der Vorsprung wächst mit der Rate, bis zur Obergrenze
    {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 2300) { // rund 60 MiB je Sekunde
            pos = readOn(ra, id, pos, 3 * MiB);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        ra.waitIdle(5000);
        s = ra.takeStats();
        check(s.consumeRate > 20e6 && reach(clip) - pos >= 380 * MiB && reach(clip) - pos <= 385 * MiB,
              QStringLiteral("bei %1 MB/s: Vorsprung %2 MiB").arg(s.consumeRate / 1e6, 0, 'f', 0).arg((reach(clip) - pos) / MiB));
    }

    // 5. Ende der Datei: nicht darüber hinaus
    pos = readOn(ra, id, clip.size - 20 * MiB, 2 * MiB);
    ra.waitIdle(5000);
    check(reach(clip) == clip.size, QStringLiteral("am Ende der Datei ist Schluss"));
    ra.close(id);
    check(allClosed(clip) && clip.opened == 1, QStringLiteral("zweiter Zugriff einmal geöffnet und wieder geschlossen"));

    // 6. Viele kurze Zugriffe quer durch die Datei (Verzeichnisse eines Abbilds): kein Vorauslesen
    Trace image;
    image.size = 40000 * MiB;
    const int img = ra.open(QStringLiteral("/discs/film.iso"), image.size, factory(&image));
    for (int i = 0; i < 200; ++i)
        ra.noteRead(img, qint64(i * 7919 % 3001) * MiB, 2048 * 8);
    check(ra.waitIdle(2000) && image.opened == 0, QStringLiteral("verstreute Zugriffe öffnen keinen zweiten Zugriff"));

    // 7. Zwei Ströme in einer Datei (Blu-ray 3D im Abbild: Bild und zweite Ansicht), abwechselnd gelesen
    qint64 a = 5000 * MiB, b = 9000 * MiB;
    for (int i = 0; i < 12; ++i) {
        a = readOn(ra, img, a, MiB / 2);
        b = readOn(ra, img, b, MiB / 4);
    }
    ra.waitIdle(5000);
    s = ra.takeStats();
    {
        std::lock_guard<std::mutex> lock(image.mutex);
        check(s.streams == 2 && image.reach >= b + 47 * MiB && image.bytes >= 2 * 47 * MiB && image.bytes <= 2 * 49 * MiB + 4 * MiB,
              QStringLiteral("zwei Ströme: je 48 MiB voraus (%1 MiB gelesen)").arg(image.bytes / MiB));
    }

    // 8. Lesefehler vor der Bibliothek: dort hält der Vorausleser an, statt es endlos zu versuchen,
    //    und liest weiter, sobald die Bibliothek selbst über die Stelle hinaus ist
    {
        std::lock_guard<std::mutex> lock(image.mutex);
        image.calls = 0;
    }
    image.badFrom = a + 60 * MiB;
    a = readOn(ra, img, a, 20 * MiB);
    ra.waitIdle(5000);
    std::this_thread::sleep_for(std::chrono::milliseconds(700));
    int calls;
    {
        std::lock_guard<std::mutex> lock(image.mutex);
        calls = image.calls;
    }
    check(calls < 40, QStringLiteral("nach einem Lesefehler kein Dauerversuch (%1 Lesevorgänge)").arg(calls));
    image.badFrom = -1;
    const qint64 before = bytes(image);
    a = readOn(ra, img, a + 70 * MiB, 2 * MiB);
    ra.waitIdle(5000);
    check(bytes(image) - before >= 47 * MiB && bytes(image) - before <= 49 * MiB,
          QStringLiteral("hinter der Stelle geht es weiter, mit dem vollen Vorsprung (%1 MiB)").arg((bytes(image) - before) / MiB));

    // 9. Das Laufwerk steht: Der Vorausleser hängt in seinem Lesen, die Aufrufe der Bibliothek kehren
    //    sofort zurück – auch das Schließen
    image.delayMs = 400;
    QElapsedTimer t;
    t.start();
    a = readOn(ra, img, a, 8 * MiB);
    const qint64 noteMs = t.restart();
    ra.close(img);
    const qint64 closeMs = t.elapsed();
    check(noteMs < 100 && closeMs < 100, QStringLiteral("stehendes Laufwerk hält die Bibliothek nicht auf (%1 ms, Schließen %2 ms)").arg(noteMs).arg(closeMs));
    image.delayMs = 0;
    check(allClosed(image), QStringLiteral("der zweite Zugriff wird danach freigegeben"));

    // 10. Der folgende Clip ist angekündigt: sein Anfang liegt bereit, bevor die Bibliothek ihn öffnet
    Trace first, next;
    first.size = 300 * MiB;
    next.size = 500 * MiB;
    const int f = ra.open(QStringLiteral("/disc/BDMV/STREAM/00010.m2ts"), first.size, factory(&first));
    ra.hintNext(QStringLiteral("/disc/BDMV/STREAM/00011.m2ts"), next.size, factory(&next));
    pos = readOn(ra, f, 0, 4 * MiB);
    ra.waitIdle(5000);
    check(reach(first) - pos >= 47 * MiB && reach(next) == 32 * MiB,
          QStringLiteral("laufender Clip zuerst, dann 32 MiB vom folgenden (%1 MiB)").arg(reach(next) / MiB));
    ra.close(f);
    const int n = ra.open(QStringLiteral("/disc/BDMV/STREAM/00011.m2ts"), next.size, factory(&next));
    pos = readOn(ra, n, 0, MiB / 2);
    s = ra.takeStats();
    check(s.streams == 1 && s.leadBytes >= 31 * MiB, QStringLiteral("beim Öffnen schon %1 MiB Vorsprung").arg(s.leadBytes / MiB));
    ra.waitIdle(5000);
    check(next.opened == 1 && reach(next) - pos >= 47 * MiB, QStringLiteral("derselbe Zugriff liest weiter"));
    ra.hintNext(QStringLiteral("/disc/BDMV/STREAM/00012.m2ts"), next.size, factory(&first));
    ra.hintNext(QString(), 0, nullptr);
    ra.close(n);
    check(allClosed(next) && allClosed(first), QStringLiteral("alles wieder geschlossen"));

    // 11. Aussetzer der Quelle: Aufrufe unter 100 ms zählen nicht
    ra.takeStats();
    DiscReadAhead::noteSourceRead(0.05);
    DiscReadAhead::noteSourceRead(0.3);
    DiscReadAhead::noteSourceRead(0.5);
    s = ra.takeStats();
    check(qAbs(s.sourceBlocked - 0.8) < 0.001 && qAbs(s.sourceWorst - 0.5) < 0.001,
          QStringLiteral("Aussetzer: %1 s gewartet, längster %2 s").arg(s.sourceBlocked).arg(s.sourceWorst));
    s = ra.takeStats();
    check(s.sourceBlocked == 0 && s.sourceWorst == 0, QStringLiteral("die Zähler beginnen nach der Abfrage neu"));

    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
