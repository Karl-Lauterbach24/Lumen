/*
 * Lumen: libdvdcss stand-in WITHOUT any copy-protection code.
 *
 * Some libdvdread builds (MSYS2, Homebrew) link libdvdcss directly, so a
 * player bundle would otherwise have to ship it. Lumen ships this shim under
 * the same name instead. It offers the five functions libdvdread uses and
 * reads DVD sectors unchanged: unencrypted discs, images and folders play,
 * CSS-scrambled ones do not.
 *
 * If the user installs a real libdvdcss through a Lumen plugin
 * ("discLibraries": {"dvdcss": …}), Lumen sets LUMEN_DVDCSS_LIBRARY to it and
 * every call is forwarded there. The decision and the responsibility for that
 * stay with the user.
 */
#ifndef _WIN32
#define _FILE_OFFSET_BITS 64
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#define SHIM_EXPORT __declspec(dllexport)
#else
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#define SHIM_EXPORT __attribute__((visibility("default")))
#endif

#define BLOCK 2048

SHIM_EXPORT void *dvdcss_open(const char *target);
SHIM_EXPORT int dvdcss_read(void *handle, void *buffer, int blocks, int flags);

typedef struct dvdcss_stream_cb {
    int (*pf_seek)(void *p_stream, uint64_t i_pos);
    int (*pf_read)(void *p_stream, void *buffer, int i_read);
    int (*pf_readv)(void *p_stream, const void *p_iovec, int i_blocks);
} dvdcss_stream_cb;

/* Echte Bibliothek (vom Nutzer per Plugin bereitgestellt) */
typedef void *(*open_fn)(const char *);
typedef void *(*open_stream_fn)(void *, dvdcss_stream_cb *);
typedef int (*close_fn)(void *);
typedef int (*seek_fn)(void *, int, int);
typedef int (*read_fn)(void *, void *, int, int);
typedef int (*cpxm_init_fn)(void *, uint8_t *);

static struct {
    int tried;
    open_fn open;
    open_stream_fn open_stream;
    close_fn close;
    seek_fn seek;
    read_fn read;
    cpxm_init_fn cpxm_init; /* CPPM/CPRM (DVD-Audio) – nur aus der echten Bibliothek */
    read_fn cpxm_read;
} g_real;

static void load_real(void)
{
    const char *path;
    if (g_real.tried)
        return;
    /* Ist (noch) keine Bibliothek genannt oder lässt sie sich (noch) nicht laden, wird beim nächsten
     * Öffnen wieder nachgesehen: LumenOS richtet libdvdcss ein, während Lumen läuft. */
    path = getenv("LUMEN_DVDCSS_LIBRARY");
    if (!path || !*path)
        return;
    g_real.tried = 1;
#ifdef _WIN32
    {
        wchar_t wpath[MAX_PATH * 2];
        HMODULE h;
        if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, MAX_PATH * 2))
            return;
        h = LoadLibraryW(wpath);
        if (!h) {
            g_real.tried = 0;
            return;
        }
        g_real.open = (open_fn)(void *)GetProcAddress(h, "dvdcss_open");
        g_real.open_stream = (open_stream_fn)(void *)GetProcAddress(h, "dvdcss_open_stream");
        g_real.close = (close_fn)(void *)GetProcAddress(h, "dvdcss_close");
        g_real.seek = (seek_fn)(void *)GetProcAddress(h, "dvdcss_seek");
        g_real.read = (read_fn)(void *)GetProcAddress(h, "dvdcss_read");
        g_real.cpxm_init = (cpxm_init_fn)(void *)GetProcAddress(h, "dvdcpxm_init");
        g_real.cpxm_read = (read_fn)(void *)GetProcAddress(h, "dvdcpxm_read");
    }
#else
    {
        void *h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if (!h) {
            g_real.tried = 0;
            return;
        }
        g_real.open = (open_fn)dlsym(h, "dvdcss_open");
        g_real.open_stream = (open_stream_fn)dlsym(h, "dvdcss_open_stream");
        g_real.close = (close_fn)dlsym(h, "dvdcss_close");
        g_real.seek = (seek_fn)dlsym(h, "dvdcss_seek");
        g_real.read = (read_fn)dlsym(h, "dvdcss_read");
        g_real.cpxm_init = (cpxm_init_fn)dlsym(h, "dvdcpxm_init");
        g_real.cpxm_read = (read_fn)dlsym(h, "dvdcpxm_read");
    }
#endif
    /* nie sich selbst laden (gleicher Dateiname, Loader liefert ggf. dieses Modul) */
    if (!g_real.open || !g_real.close || !g_real.seek || !g_real.read || (void *)g_real.open == (void *)dvdcss_open)
        memset(&g_real, 0, sizeof g_real), g_real.tried = 1;
}

typedef struct shim {
    void *real; /* Handle der echten Bibliothek */
    void *stream;
    dvdcss_stream_cb *cb;
#ifdef _WIN32
    HANDLE h;
#else
    int fd;
#endif
} shim;

SHIM_EXPORT void *dvdcss_open(const char *target)
{
    shim *s;
    load_real();
    if (g_real.open) {
        void *r = g_real.open(target);
        if (!r)
            return NULL;
        s = calloc(1, sizeof *s);
        if (s)
            s->real = r;
        return s;
    }
    if (!target)
        return NULL;
    s = calloc(1, sizeof *s);
    if (!s)
        return NULL;
#ifdef _WIN32
    {
        /* "D:" / "D:\" -> Rohzugriff auf das Laufwerk, sonst Datei (UTF-8) */
        wchar_t wpath[MAX_PATH * 2];
        const size_t n = strlen(target);
        if ((n == 2 || n == 3) && target[1] == ':') {
            wchar_t dev[] = L"\\\\.\\X:";
            dev[4] = (wchar_t)target[0];
            s->h = CreateFileW(dev, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        } else if (MultiByteToWideChar(CP_UTF8, 0, target, -1, wpath, MAX_PATH * 2)) {
            s->h = CreateFileW(wpath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        } else {
            s->h = INVALID_HANDLE_VALUE;
        }
        if (s->h == INVALID_HANDLE_VALUE) {
            free(s);
            return NULL;
        }
    }
#else
    s->fd = open(target, O_RDONLY);
    if (s->fd < 0) {
        free(s);
        return NULL;
    }
#endif
    return s;
}

SHIM_EXPORT void *dvdcss_open_stream(void *stream, dvdcss_stream_cb *cb)
{
    shim *s;
    load_real();
    if (g_real.open_stream) {
        void *r = g_real.open_stream(stream, cb);
        if (!r)
            return NULL;
        s = calloc(1, sizeof *s);
        if (s)
            s->real = r;
        return s;
    }
    if (!cb || !cb->pf_seek || !cb->pf_read)
        return NULL;
    s = calloc(1, sizeof *s);
    if (!s)
        return NULL;
    s->stream = stream;
    s->cb = cb;
#ifdef _WIN32
    s->h = INVALID_HANDLE_VALUE;
#else
    s->fd = -1;
#endif
    return s;
}

SHIM_EXPORT int dvdcss_close(void *handle)
{
    shim *s = handle;
    int r = 0;
    if (!s)
        return -1;
    if (s->real)
        r = g_real.close(s->real);
#ifdef _WIN32
    else if (s->h != INVALID_HANDLE_VALUE && s->h)
        CloseHandle(s->h);
#else
    else if (s->fd >= 0)
        close(s->fd);
#endif
    free(s);
    return r;
}

SHIM_EXPORT int dvdcss_seek(void *handle, int blocks, int flags)
{
    shim *s = handle;
    if (!s || blocks < 0)
        return -1;
    if (s->real)
        return g_real.seek(s->real, blocks, flags);
    if (s->cb)
        return s->cb->pf_seek(s->stream, (uint64_t)blocks * BLOCK) < 0 ? -1 : blocks;
#ifdef _WIN32
    {
        LARGE_INTEGER pos;
        pos.QuadPart = (LONGLONG)blocks * BLOCK;
        return SetFilePointerEx(s->h, pos, NULL, FILE_BEGIN) ? blocks : -1;
    }
#else
    return lseek(s->fd, (off_t)blocks * BLOCK, SEEK_SET) < 0 ? -1 : blocks;
#endif
}

SHIM_EXPORT int dvdcss_read(void *handle, void *buffer, int blocks, int flags)
{
    shim *s = handle;
    if (!s || blocks < 0)
        return -1;
    if (s->real)
        return g_real.read(s->real, buffer, blocks, flags);
    (void)flags; /* DVDCSS_READ_DECRYPT: hier gibt es nichts zu entschlüsseln */
    if (s->cb) {
        const int got = s->cb->pf_read(s->stream, buffer, blocks * BLOCK);
        return got < 0 ? -1 : got / BLOCK;
    }
#ifdef _WIN32
    {
        DWORD got = 0;
        if (!ReadFile(s->h, buffer, (DWORD)blocks * BLOCK, &got, NULL))
            return -1;
        return (int)(got / BLOCK);
    }
#else
    {
        const ssize_t got = read(s->fd, buffer, (size_t)blocks * BLOCK);
        return got < 0 ? -1 : (int)(got / BLOCK);
    }
#endif
}

/* CPPM/CPRM (DVD-Audio): libdvdread ruft init nur für solche Discs auf und liest
   bei einem Fehler normal über dvdcss_read weiter */
SHIM_EXPORT int dvdcpxm_init(void *handle, uint8_t *p_mkb)
{
    shim *s = handle;
    if (s && s->real && g_real.cpxm_init)
        return g_real.cpxm_init(s->real, p_mkb);
    return -1;
}

SHIM_EXPORT int dvdcpxm_read(void *handle, void *buffer, int blocks, int flags)
{
    shim *s = handle;
    if (s && s->real && g_real.cpxm_read)
        return g_real.cpxm_read(s->real, buffer, blocks, flags);
    return dvdcss_read(handle, buffer, blocks, 0);
}
