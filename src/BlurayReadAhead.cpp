#include "DiscReadAhead.h"

#include <QByteArray>
#include <QDir>
#include <QFileInfo>

#ifdef LUMEN_HAVE_BLURAY

#include <libbluray/filesystem.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <mutex>
#include <new>
#include <set>
#include <string>
#include <thread>

#ifdef Q_OS_MACOS
#include <sys/stat.h>
#endif

namespace {

// Kleinere Dateien (Playlists, Clip-Infos, Menüprogramme) liest libbluray am Stück
constexpr int64_t kMinSize = int64_t(16) << 20;

BD_FILE_OPEN g_open = nullptr; // der Dateizugriff von libbluray selbst

QString keyFor(const char *filename)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(QString::fromUtf8(filename)));
}

// ---------------------------------------------------------------------------
// Entwickler-Hilfe: LUMEN_TEST_SLOW_DISC="rate=12e6,stall=2.5,every=9" lässt eine Disc in einem
// Ordner sich wie eine in einem optischen Laufwerk verhalten – so, wie es an einem USB-Laufwerk
// unter macOS gemessen ist: Das System holt die Daten in Blöcken von 3 MiB mit begrenzter Rate
// (rate, Byte/s), der Aufruf wartet so lange; was einmal geholt ist, liegt im Arbeitsspeicher und
// kommt sofort. Alle "every" Sekunden steht das Laufwerk "stall" Sekunden lang. Ein Laufwerk, ein
// Lesekopf: Was gerade geholt wird, hält jeden anderen Zugriff auf.
// Damit lässt sich ohne Laufwerk vergleichen, was die Wiedergabe mit und ohne Vorauslesen zeigt.
// ---------------------------------------------------------------------------
class SlowDisc
{
public:
    static SlowDisc *instance()
    {
        static SlowDisc *self = create();
        return self;
    }

    BD_FILE_H *wrap(BD_FILE_H *inner, const char *filename)
    {
        auto *f = new (std::nothrow) File;
        if (!f)
            return inner;
        f->inner = inner;
        f->disc = this;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            f->fetched = &m_fetched[filename];
        }
        f->self.internal = f;
        f->self.close = [](BD_FILE_H *h) {
            File *file = static_cast<File *>(h->internal);
            file->inner->close(file->inner);
            delete file;
        };
        f->self.seek = [](BD_FILE_H *h, int64_t offset, int32_t origin) {
            File *file = static_cast<File *>(h->internal);
            return file->inner->seek(file->inner, offset, origin);
        };
        f->self.tell = [](BD_FILE_H *h) {
            File *file = static_cast<File *>(h->internal);
            return file->inner->tell(file->inner);
        };
        f->self.eof = [](BD_FILE_H *h) {
            File *file = static_cast<File *>(h->internal);
            return file->inner->eof ? file->inner->eof(file->inner) : 0;
        };
        f->self.read = [](BD_FILE_H *h, uint8_t *buf, int64_t size) {
            File *file = static_cast<File *>(h->internal);
            const int64_t pos = file->inner->tell(file->inner);
            if (pos >= 0 && size > 0)
                file->disc->fetch(file->fetched, pos, size);
            return file->inner->read(file->inner, buf, size);
        };
        f->self.write = [](BD_FILE_H *, const uint8_t *, int64_t) -> int64_t { return -1; };
        return &f->self;
    }

private:
    struct File
    {
        BD_FILE_H self;
        BD_FILE_H *inner;
        SlowDisc *disc;
        std::set<int64_t> *fetched;
    };
    static constexpr int64_t kCluster = int64_t(3) << 20;
    using Clock = std::chrono::steady_clock;

    static SlowDisc *create()
    {
        const QString spec = qEnvironmentVariable("LUMEN_TEST_SLOW_DISC");
        if (spec.isEmpty())
            return nullptr;
        auto *d = new SlowDisc;
        for (const QString &part : spec.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            const double v = part.section(QLatin1Char('='), 1).toDouble();
            if (part.startsWith(QLatin1String("rate=")) && v > 0)
                d->m_rate = v;
            else if (part.startsWith(QLatin1String("stall=")))
                d->m_stall = v;
            else if (part.startsWith(QLatin1String("every=")) && v > 0)
                d->m_every = v;
        }
        d->m_start = Clock::now();
        return d;
    }

    // Die Blöcke holen, die der Bereich berührt und die noch nicht im "Arbeitsspeicher" liegen
    void fetch(std::set<int64_t> *fetched, int64_t pos, int64_t size)
    {
        for (int64_t c = pos / kCluster; c <= (pos + size - 1) / kCluster; ++c) {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (fetched->count(c))
                    continue;
            }
            std::lock_guard<std::mutex> drive(m_drive);
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (fetched->count(c))
                    continue;
            }
            waitOutStall();
            std::this_thread::sleep_for(std::chrono::duration<double>(double(kCluster) / m_rate));
            waitOutStall();
            std::lock_guard<std::mutex> lock(m_mutex);
            fetched->insert(c);
        }
    }

    void waitOutStall()
    {
        if (m_stall <= 0)
            return;
        const double t = std::chrono::duration<double>(Clock::now() - m_start).count();
        const double phase = t - m_every * std::floor(t / m_every);
        // die erste Pause nach "every" Sekunden: der Beginn der Wiedergabe bleibt frei
        if (t >= m_every && phase < m_stall)
            std::this_thread::sleep_for(std::chrono::duration<double>(m_stall - phase));
    }

    std::mutex m_mutex; // m_fetched
    std::mutex m_drive; // der Lesekopf
    std::map<std::string, std::set<int64_t>> m_fetched;
    double m_rate = 12e6, m_stall = 0, m_every = 10;
    Clock::time_point m_start;
};

// Datei öffnen, wie libbluray es selbst täte
BD_FILE_H *openInner(const char *filename, const char *mode)
{
    BD_FILE_H *f = g_open ? g_open(filename, mode) : nullptr;
    if (f && mode && mode[0] == 'r' && filename && SlowDisc::instance() && QByteArray(filename).contains("STREAM"))
        return SlowDisc::instance()->wrap(f, filename);
    return f;
}

// Zweiter Zugriff auf dieselbe Datei, über libbluray geöffnet wie der erste
class Reader final : public DiscReadAhead::Reader
{
public:
    explicit Reader(BD_FILE_H *file) : m_file(file) {}
    ~Reader() override { m_file->close(m_file); }

    qint64 readAt(qint64 offset, char *buffer, qint64 size) override
    {
        if (m_pos != offset) {
            m_pos = -1;
            if (m_file->seek(m_file, offset, SEEK_SET) < 0)
                return -1;
            m_pos = offset;
        }
        const int64_t got = m_file->read(m_file, reinterpret_cast<uint8_t *>(buffer), size);
        m_pos = got > 0 ? m_pos + got : -1;
        return got;
    }

private:
    BD_FILE_H *m_file;
    qint64 m_pos = 0;
};

DiscReadAhead::ReaderFactory factoryFor(const char *filename)
{
    return [name = std::string(filename)]() -> std::unique_ptr<DiscReadAhead::Reader> {
        BD_FILE_H *f = openInner(name.c_str(), "rb");
        return f ? std::make_unique<Reader>(f) : nullptr;
    };
}

// Was libbluray bekommt: reicht jeden Aufruf an die eigentliche Datei weiter und merkt sich, wo gelesen wird
struct Watched
{
    BD_FILE_H self;
    BD_FILE_H *inner;
    int id;
    int64_t pos;
};

Watched *watched(BD_FILE_H *file)
{
    return static_cast<Watched *>(file->internal);
}

void watchedClose(BD_FILE_H *file)
{
    Watched *w = watched(file);
    DiscReadAhead::instance().close(w->id);
    w->inner->close(w->inner);
    delete w;
}

int64_t watchedSeek(BD_FILE_H *file, int64_t offset, int32_t origin)
{
    Watched *w = watched(file);
    const int64_t r = w->inner->seek(w->inner, offset, origin);
    // die Stelle erfragen: nicht jede Fassung von libbluray gibt sie hier zurück (Windows: 0)
    w->pos = w->inner->tell(w->inner);
    return r;
}

int64_t watchedTell(BD_FILE_H *file)
{
    Watched *w = watched(file);
    return w->inner->tell(w->inner);
}

int watchedEof(BD_FILE_H *file)
{
    Watched *w = watched(file);
    return w->inner->eof ? w->inner->eof(w->inner) : 0;
}

int64_t watchedRead(BD_FILE_H *file, uint8_t *buf, int64_t size)
{
    Watched *w = watched(file);
    const int64_t got = w->inner->read(w->inner, buf, size);
    if (got > 0 && w->pos >= 0) {
        DiscReadAhead::instance().noteRead(w->id, w->pos, got);
        w->pos += got;
    } else if (got < 0) {
        w->pos = w->inner->tell(w->inner);
    }
    return got;
}

int64_t watchedWrite(BD_FILE_H *file, const uint8_t *buf, int64_t size)
{
    Watched *w = watched(file);
    return w->inner->write ? w->inner->write(w->inner, buf, size) : -1;
}

// Größe der Datei; -1, wenn sie sich nicht ermitteln lässt. Lässt die Leseposition am Anfang.
int64_t sizeOf(BD_FILE_H *file)
{
    if (!file->seek || !file->tell || file->seek(file, 0, SEEK_END) < 0)
        return -1;
    const int64_t size = file->tell(file);
    if (file->seek(file, 0, SEEK_SET) < 0)
        return -1;
    return size;
}

BD_FILE_H *openWatched(const char *filename, const char *mode)
{
    BD_FILE_H *inner = openInner(filename, mode);
    if (!inner || !filename || !mode || mode[0] != 'r' || !inner->read || !DiscReadAhead::enabled())
        return inner;
#ifdef Q_OS_MACOS
    // Zeichengerät (/dev/rdisk…): das System puffert dort nichts, Vorauslesen brächte nur doppelte Arbeit
    struct stat st;
    if (::stat(filename, &st) == 0 && S_ISCHR(st.st_mode))
        return inner;
#endif
    const int64_t size = sizeOf(inner);
    if (size < kMinSize)
        return inner;
    auto *w = new (std::nothrow) Watched;
    if (!w)
        return inner;
    w->inner = inner;
    w->pos = 0;
    w->self.internal = w;
    w->self.close = watchedClose;
    w->self.seek = watchedSeek;
    w->self.tell = watchedTell;
    w->self.eof = watchedEof;
    w->self.read = watchedRead;
    w->self.write = watchedWrite;
    w->id = DiscReadAhead::instance().open(keyFor(filename), size, factoryFor(filename));
    return &w->self;
}

} // namespace

void BlurayReadAhead::install()
{
    if (g_open || (!DiscReadAhead::enabled() && !SlowDisc::instance()))
        return;
    g_open = bd_register_file(openWatched);
    if (!g_open) // kein Dateizugriff, an den sich weiterreichen ließe: den Zustand von vorher herstellen
        bd_register_file(nullptr);
}

void BlurayReadAhead::hintNext(const QString &path)
{
    if (!g_open)
        return;
    const QFileInfo fi(path);
    if (path.isEmpty() || !fi.isFile() || fi.size() < kMinSize) {
        DiscReadAhead::instance().hintNext(QString(), 0, nullptr);
        return;
    }
    const QByteArray name = QDir::toNativeSeparators(path).toUtf8();
    DiscReadAhead::instance().hintNext(keyFor(name.constData()), fi.size(), factoryFor(name.constData()));
}

#else

void BlurayReadAhead::install() {}
void BlurayReadAhead::hintNext(const QString &) {}

#endif
