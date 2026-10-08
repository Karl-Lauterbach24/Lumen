#include "DiscReadAhead.h"

#include <QtGlobal>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>

namespace {

constexpr qint64 kChunk = qint64(1) << 20;        // ein Lesevorgang des Vorauslesers
constexpr qint64 kMinWindow = qint64(48) << 20;   // Vorsprung mindestens …
constexpr qint64 kMaxWindow = qint64(384) << 20;  // … und höchstens
constexpr double kWindowSeconds = 25.0;           // … sonst so viele Sekunden des laufenden Stroms
constexpr qint64 kStartRun = qint64(1) << 20;     // so viel fortlaufend gelesen: ein Strom
constexpr qint64 kSkipAhead = qint64(4) << 20;    // Lücke nach vorn, die noch als fortlaufend gilt
constexpr qint64 kReRead = qint64(256) << 10;     // so weit zurück liest die Bibliothek nach einem Fehler neu
constexpr qint64 kHintBytes = qint64(32) << 20;   // vom Anfang der folgenden Datei
constexpr int kMaxCursors = 4;
constexpr double kBlocked = 0.1;                  // ab hier zählt ein Leseaufruf der Quelle als Aussetzer

using Clock = std::chrono::steady_clock;

double seconds(Clock::duration d)
{
    return std::chrono::duration<double>(d).count();
}

std::atomic<qint64> g_sourceBlockedUs{0};
std::atomic<qint64> g_sourceWorstUs{0};

} // namespace

struct DiscReadAhead::Cursor
{
    qint64 pos = 0;   // hinter dem letzten Lesen der Bibliothek
    qint64 ahead = 0; // bis hierhin ist vorausgelesen
    qint64 run = 0;   // fortlaufend gelesene Bytes
    double rate = 0;  // Byte/s, geglättet
    qint64 sinceBytes = 0;
    Clock::time_point since;
    Clock::time_point used;
    bool failed = false; // an dieser Stelle ließ sich nicht vorauslesen (Lesefehler, Ende)
};

struct DiscReadAhead::File
{
    int id = 0;
    QString key;
    qint64 size = 0;
    ReaderFactory factory;
    std::unique_ptr<Reader> reader; // gehört dem Vorauslese-Thread
    bool opened = false;            // der Versuch, ihn zu öffnen, ist gemacht
    Cursor cursors[kMaxCursors];
    int used = 0;
    bool closed = false;
    bool hint = false; // die Bibliothek hat die Datei noch nicht geöffnet
};

DiscReadAhead::DiscReadAhead() = default;

DiscReadAhead &DiscReadAhead::instance()
{
    // Lebt bis zum Ende des Prozesses: der Vorauslese-Thread kann gerade im Laufwerk stehen
    static DiscReadAhead *self = new DiscReadAhead;
    return *self;
}

bool DiscReadAhead::enabled()
{
    static const bool on = !qEnvironmentVariableIsSet("LUMEN_NO_READAHEAD");
    return on;
}

void DiscReadAhead::noteSourceRead(double secs)
{
    if (secs < kBlocked)
        return;
    const qint64 us = qint64(secs * 1e6);
    g_sourceBlockedUs.fetch_add(us, std::memory_order_relaxed);
    qint64 worst = g_sourceWorstUs.load(std::memory_order_relaxed);
    while (us > worst && !g_sourceWorstUs.compare_exchange_weak(worst, us, std::memory_order_relaxed)) {
    }
}

int DiscReadAhead::open(const QString &key, qint64 size, ReaderFactory factory)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::shared_ptr<File> file;
    if (m_next && !m_next->closed && m_next->key == key) {
        // angekündigt und schon ein Stück vorausgelesen
        file = std::move(m_next);
        m_next.reset();
        file->hint = false;
    } else {
        file = std::make_shared<File>();
        file->key = key;
        file->factory = std::move(factory);
    }
    file->size = size;
    file->id = m_nextId++;
    m_files.push_back(file);
    if (!m_started) {
        m_started = true;
        std::thread([this] { run(); }).detach();
    }
    return file->id;
}

void DiscReadAhead::close(int id)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto &f : m_files) {
        if (f->id == id)
            f->closed = true; // der Vorauslese-Thread räumt ab (sein Zugriff kann gerade lesen)
    }
    m_wake.notify_one();
}

void DiscReadAhead::hintNext(const QString &key, qint64 size, ReaderFactory factory)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_next && m_next->key == key)
        return;
    if (m_next) {
        m_next->closed = true;
        m_files.push_back(std::move(m_next)); // dort gibt der Thread den Zugriff frei
        m_next.reset();
    }
    if (key.isEmpty() || size <= 0 || !factory)
        return;
    // dieselbe Datei läuft schon (eine Playlist, die einen Clip wiederholt)
    for (const auto &f : m_files) {
        if (!f->closed && f->key == key)
            return;
    }
    m_next = std::make_shared<File>();
    m_next->key = key;
    m_next->size = size;
    m_next->factory = std::move(factory);
    m_next->hint = true;
    m_next->used = 1;
    m_next->cursors[0].run = kStartRun;
    m_next->cursors[0].used = Clock::now();
    if (!m_started) {
        m_started = true;
        std::thread([this] { run(); }).detach();
    }
    m_wake.notify_one();
}

void DiscReadAhead::noteRead(int id, qint64 offset, qint64 size)
{
    if (size <= 0 || offset < 0)
        return;
    std::lock_guard<std::mutex> lock(m_mutex);
    File *file = nullptr;
    for (const auto &f : m_files) {
        if (f->id == id && !f->closed) {
            file = f.get();
            break;
        }
    }
    if (!file)
        return;
    const Clock::time_point now = Clock::now();
    const qint64 end = offset + size;

    Cursor *c = nullptr;
    for (int i = 0; i < file->used; ++i) {
        Cursor &k = file->cursors[i];
        if (offset >= k.pos - kReRead && offset - k.pos <= kSkipAhead) {
            c = &k;
            break;
        }
    }
    if (c) {
        if (end > c->pos) {
            c->run += end - std::max(c->pos, offset);
            c->sinceBytes += end - c->pos;
            c->pos = end;
        }
        const double dt = seconds(now - c->since);
        if (dt >= 1.0) {
            const double rate = double(c->sinceBytes) / dt;
            c->rate = c->rate > 0 ? 0.7 * c->rate + 0.3 * rate : rate;
            c->since = now;
            c->sinceBytes = 0;
        }
    } else {
        // ein neuer Strom: freien Platz nehmen, sonst den am längsten unbenutzten
        if (file->used < kMaxCursors) {
            c = &file->cursors[file->used++];
        } else {
            c = &file->cursors[0];
            for (int i = 1; i < kMaxCursors; ++i) {
                if (file->cursors[i].used < c->used)
                    c = &file->cursors[i];
            }
        }
        *c = Cursor();
        c->pos = end;
        c->ahead = end;
        c->run = size;
        c->since = now;
    }
    c->used = now;
    if (c->ahead < c->pos) {
        // die Bibliothek ist weiter als der Vorausleser (Beginn, oder sie hat eine Stelle gelesen,
        // an der er gescheitert war): von dort weiter
        c->ahead = c->pos;
        c->failed = false;
    }
    if (c->run >= kStartRun && c->ahead - c->pos < windowFor(*c))
        m_wake.notify_one();
}

qint64 DiscReadAhead::windowFor(const Cursor &c) const
{
    return std::clamp(qint64(c.rate * kWindowSeconds), kMinWindow, kMaxWindow);
}

// Was als Nächstes vorauszulesen ist: der Strom mit dem kleinsten Vorsprung. Aufrufer hält m_mutex.
bool DiscReadAhead::pick(std::shared_ptr<File> &file, int &cursor, qint64 &offset, qint64 &size)
{
    qint64 least = -1;
    for (const auto &f : m_files) {
        if (f->closed)
            continue;
        for (int i = 0; i < f->used; ++i) {
            const Cursor &c = f->cursors[i];
            if (c.failed || c.run < kStartRun || (f->size > 0 && c.ahead >= f->size))
                continue;
            const qint64 lead = c.ahead - c.pos;
            if (lead >= windowFor(c) || (least >= 0 && lead >= least))
                continue;
            least = lead;
            file = f;
            cursor = i;
        }
    }
    if (least < 0 && m_next && !m_next->closed) {
        const Cursor &c = m_next->cursors[0];
        if (!c.failed && c.ahead < std::min(kHintBytes, m_next->size)) {
            least = 0;
            file = m_next;
            cursor = 0;
        }
    }
    if (least < 0)
        return false;
    const Cursor &c = file->cursors[cursor];
    offset = c.ahead;
    size = kChunk;
    if (file->size > 0)
        size = std::min(size, file->size - offset);
    if (file->hint)
        size = std::min(size, kHintBytes - offset);
    return size > 0;
}

void DiscReadAhead::run()
{
    std::vector<char> buffer(static_cast<size_t>(kChunk));
    std::vector<std::shared_ptr<File>> dead;
    std::unique_lock<std::mutex> lock(m_mutex);
    for (;;) {
        // geschlossene Dateien austragen; ihr Zugriff wird außerhalb der Sperre freigegeben
        for (auto it = m_files.begin(); it != m_files.end();) {
            if ((*it)->closed) {
                dead.push_back(std::move(*it));
                it = m_files.erase(it);
            } else {
                ++it;
            }
        }
        std::shared_ptr<File> file;
        int cursor = 0;
        qint64 offset = 0, size = 0;
        const bool work = pick(file, cursor, offset, size);
        if (!work) {
            m_busy = false;
            m_idleCond.notify_all();
        } else {
            m_busy = true;
        }
        lock.unlock();
        dead.clear();
        if (!work) {
            lock.lock();
            m_wake.wait_for(lock, std::chrono::milliseconds(250));
            continue;
        }
        if (!file->opened) {
            file->opened = true;
            file->reader = file->factory ? file->factory() : nullptr;
        }
        qint64 got = -1;
        const Clock::time_point start = Clock::now();
        if (file->reader)
            got = file->reader->readAt(offset, buffer.data(), size);
        const double took = seconds(Clock::now() - start);
        lock.lock();
        Cursor &c = file->cursors[cursor];
        if (got <= 0) {
            // Lesefehler oder Ende: hier nicht weiter, bis die Bibliothek selbst darüber hinaus ist
            if (c.ahead == offset)
                c.failed = true;
            continue;
        }
        // hat die Bibliothek inzwischen woanders weitergelesen, gilt deren Stand
        if (c.ahead == offset)
            c.ahead = offset + got;
        m_stats.prefetched += got;
        m_stats.prefetchWorst = std::max(m_stats.prefetchWorst, took);
        // die Rate des Laufwerks nur aus Lesevorgängen, die es auch erreicht haben (nicht aus dem Puffer)
        if (took > 0.002) {
            const double rate = double(got) / took;
            m_driveRate = m_driveRate > 0 ? 0.8 * m_driveRate + 0.2 * rate : rate;
        }
    }
}

DiscReadAhead::Stats DiscReadAhead::takeStats()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    Stats s = m_stats;
    m_stats = Stats();
    const Clock::time_point now = Clock::now();
    qint64 least = -1;
    for (const auto &f : m_files) {
        if (f->closed)
            continue;
        for (int i = 0; i < f->used; ++i) {
            const Cursor &c = f->cursors[i];
            // ein Strom, an dem die Bibliothek gerade liest
            if (c.run < kStartRun || seconds(now - c.used) > 3.0)
                continue;
            ++s.streams;
            s.consumeRate += c.rate;
            const qint64 lead = c.ahead - c.pos;
            if (least < 0 || lead < least) {
                least = lead;
                s.windowBytes = windowFor(c);
            }
        }
    }
    s.leadBytes = std::max<qint64>(0, least);
    s.driveRate = m_driveRate;
    s.sourceBlocked = double(g_sourceBlockedUs.exchange(0, std::memory_order_relaxed)) / 1e6;
    s.sourceWorst = double(g_sourceWorstUs.exchange(0, std::memory_order_relaxed)) / 1e6;
    return s;
}

bool DiscReadAhead::waitIdle(int timeoutMs)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_wake.notify_one();
    return m_idleCond.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this] {
        std::shared_ptr<File> file;
        int cursor = 0;
        qint64 offset = 0, size = 0;
        return !m_busy && !pick(file, cursor, offset, size);
    });
}
