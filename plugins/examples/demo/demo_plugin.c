/*
 * Lumen demo plugin – shows every hook of the plugin API:
 *   - an action button ("Show media info") and a status line
 *   - player events (logged)
 *   - an own URL scheme "xorfile://<path>": plays files that were XOR-ed with
 *     a one-byte key – a stand-in for any format a plugin can decode itself
 *   - DCP content keys from a text file ("<key id> <key>" per line), read from
 *     $LUMEN_DEMO_DCP_KEYS, <plugin dir>/dcp-keys.txt or <config dir>/dcp-keys.txt
 *
 * Build: see CMakeLists.txt (LUMEN_BUILD_PLUGIN_EXAMPLES) or
 *   cc -shared -fPIC -I<lumen>/include demo_plugin.c -o demo_plugin.so
 */
#ifndef _WIN32
#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L
#endif
#include "lumen/plugin.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define XOR_KEY 0x5A

typedef struct demo {
    const lumen_host *host;
    unsigned char keys[64][2][16]; /* [n][0] = key id, [n][1] = key */
    int key_count;
} demo;

static int hex_nibble(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c = tolower(c);
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

/* 32 hex digits (dashes and "urn:uuid:" ignored) -> 16 bytes */
static int parse_hex16(const char *s, unsigned char out[16])
{
    int n = 0, hi = -1;
    if (strncmp(s, "urn:uuid:", 9) == 0)
        s += 9;
    for (; *s && n < 16; ++s) {
        if (*s == '-')
            continue;
        const int v = hex_nibble((unsigned char)*s);
        if (v < 0)
            return 0;
        if (hi < 0)
            hi = v;
        else {
            out[n++] = (unsigned char)(hi << 4 | v);
            hi = -1;
        }
    }
    return n == 16;
}

static void load_keys(demo *d, const char *path)
{
    FILE *f = path ? fopen(path, "r") : NULL;
    char line[256];
    if (!f)
        return;
    while (d->key_count < 64 && fgets(line, sizeof line, f)) {
        char id[80], key[80];
        if (line[0] == '#' || sscanf(line, "%79s %79s", id, key) != 2)
            continue;
        if (parse_hex16(id, d->keys[d->key_count][0]) && parse_hex16(key, d->keys[d->key_count][1]))
            ++d->key_count;
    }
    fclose(f);
}

static void *demo_init(const lumen_host *host)
{
    demo *d = calloc(1, sizeof *d);
    char path[1024], status[128];
    if (!d)
        return NULL;
    d->host = host;
    load_keys(d, getenv("LUMEN_DEMO_DCP_KEYS"));
    snprintf(path, sizeof path, "%s/dcp-keys.txt", host->plugin_dir(host->ctx));
    load_keys(d, path);
    snprintf(path, sizeof path, "%s/dcp-keys.txt", host->config_dir(host->ctx));
    load_keys(d, path);

    host->add_action(host->ctx, "info", "Medieninfo anzeigen");
    snprintf(status, sizeof status, "Bereit – %d DCP-Schlüssel geladen", d->key_count);
    host->set_status(host->ctx, status);
    host->log(host->ctx, LUMEN_LOG_INFO, "demo plugin loaded");
    return d;
}

static void demo_shutdown(void *ctx)
{
    free(ctx);
}

static void demo_event(void *ctx, const char *event, const char *json)
{
    demo *d = ctx;
    char msg[1024];
    snprintf(msg, sizeof msg, "event %s %s", event, json);
    d->host->log(d->host->ctx, LUMEN_LOG_DEBUG, msg);
}

static void demo_action(void *ctx, const char *id)
{
    demo *d = ctx;
    if (strcmp(id, "info") == 0) {
        char *title = d->host->get_property(d->host->ctx, "media-title");
        char *codec = d->host->get_property(d->host->ctx, "video-codec");
        char text[512];
        snprintf(text, sizeof text, "%s\n%s", title ? title : "(nichts geladen)", codec ? codec : "");
        d->host->show_text(d->host->ctx, text, 3000);
        d->host->free_string(d->host->ctx, title);
        d->host->free_string(d->host->ctx, codec);
    }
}

/* ---- xorfile:// ------------------------------------------------------- */

static int64_t xor_read(void *cookie, char *buf, uint64_t size)
{
    const size_t n = fread(buf, 1, (size_t)size, (FILE *)cookie);
    for (size_t i = 0; i < n; ++i)
        buf[i] ^= XOR_KEY;
    return (int64_t)n;
}

static int64_t xor_seek(void *cookie, int64_t offset)
{
#ifdef _WIN32
    return _fseeki64((FILE *)cookie, offset, SEEK_SET) == 0 ? offset : -1;
#else
    return fseeko((FILE *)cookie, (off_t)offset, SEEK_SET) == 0 ? offset : -1;
#endif
}

static int64_t xor_size(void *cookie)
{
    FILE *f = cookie;
#ifdef _WIN32
    const int64_t pos = _ftelli64(f);
    _fseeki64(f, 0, SEEK_END);
    const int64_t size = _ftelli64(f);
    _fseeki64(f, pos, SEEK_SET);
#else
    const int64_t pos = ftello(f);
    fseeko(f, 0, SEEK_END);
    const int64_t size = ftello(f);
    fseeko(f, (off_t)pos, SEEK_SET);
#endif
    return size;
}

static void xor_close(void *cookie)
{
    fclose((FILE *)cookie);
}

static int demo_stream_open(void *ctx, const char *url, lumen_stream *out)
{
    (void)ctx;
    const char *path = strstr(url, "://");
    FILE *f = path ? fopen(path + 3, "rb") : NULL;
    if (!f)
        return -1;
    out->cookie = f;
    out->read = xor_read;
    out->seek = xor_seek;
    out->size = xor_size;
    out->close = xor_close;
    return 0;
}

/* ---- DCP-Schlüssel ------------------------------------------------------ */

static int demo_dcp_key(void *ctx, const uint8_t key_id[16], uint8_t key[16])
{
    demo *d = ctx;
    for (int i = 0; i < d->key_count; ++i) {
        if (memcmp(d->keys[i][0], key_id, 16) == 0) {
            memcpy(key, d->keys[i][1], 16);
            return 1;
        }
    }
    return 0;
}

static const char *const demo_schemes[] = {"xorfile", NULL};

LUMEN_PLUGIN_EXPORT const lumen_plugin *lumen_plugin_entry(void)
{
    static const lumen_plugin plugin = {
        sizeof(lumen_plugin),
        LUMEN_PLUGIN_API_VERSION,
        demo_init,
        demo_shutdown,
        demo_event,
        demo_action,
        demo_schemes,
        demo_stream_open,
        demo_dcp_key,
    };
    return &plugin;
}
