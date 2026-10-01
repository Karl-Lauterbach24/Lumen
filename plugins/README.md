# Lumen plugins

Plugins extend Lumen without changing the player itself: new sources and formats, decryption
done by software the **user** installs, keys, mpv scripts, automation, and buttons in the control
window.

> **Lumen contains no copy-protection circumvention.** Libraries such as libaacs, libbdplus or
> libdvdcss are never shipped or downloaded by Lumen. A plugin only makes libraries available that
> the user installed themselves, and only after the user enabled that plugin. You are responsible
> for making sure that using them is legal where you live.

## Installing

Put the plugin folder (the one with `plugin.json`) into the user plugin folder. The **Plugins** tab
has an "Open plugin folder" button:

| System  | Folder |
|---------|--------|
| Windows | `%APPDATA%\Lumen\Lumen\plugins` |
| macOS   | `~/Library/Application Support/Lumen/Lumen/plugins` |
| Linux   | `~/.local/share/Lumen/Lumen/plugins` |

Lumen also looks in `<program folder>/plugins` (macOS: `Lumen.app/Contents/PlugIns/lumen`, Linux:
`<prefix>/lib/lumen/plugins`) and in every folder listed in `LUMEN_PLUGIN_PATH`.

New plugins start **disabled**. Enable them in the Plugins tab and restart Lumen: native libraries
are loaded before libmpv and libbluray start, and they can't be unloaded safely while Lumen runs.

## plugin.json

```json
{
    "id": "my-plugin",
    "name": "My plugin",
    "version": "1.0",
    "author": "…",
    "description": "Shown in the Plugins tab",

    "library": "my_plugin",
    "scripts": ["scripts/thing.lua"],
    "mpvOptions": { "sub-font": "Inter" },
    "env": { "SOME_VARIABLE": "${pluginDir}/data" },
    "discLibraries": {
        "aacs":   { "windows": "lib/libaacs.dll",     "macos": "lib/libaacs.0.dylib",   "linux": "lib/libaacs.so.0" },
        "bdplus": { "windows": "lib/libbdplus.dll",   "macos": "lib/libbdplus.0.dylib", "linux": "lib/libbdplus.so.0" },
        "dvdcss": { "windows": "lib/libdvdcss-2.dll", "macos": "lib/libdvdcss.2.dylib", "linux": "lib/libdvdcss.so.2" }
    }
}
```

All keys except `id` are optional. Paths are relative to the plugin folder, and any path can be one
string or an object with `windows`/`macos`/`linux` entries. `${pluginDir}` and `${configDir}` (a
writable folder for this plugin) are expanded.

| Key | Effect |
|-----|--------|
| `library` | Native plugin (C ABI, below). The file extension may be left out. |
| `scripts` | mpv Lua/JavaScript scripts, loaded into every mpv instance ([mpv scripting](https://mpv.io/manual/master/#lua-scripting)). |
| `mpvOptions` | mpv options, applied after Lumen's defaults and before the output profile. |
| `env` | Environment variables, set before any library starts. |
| `discLibraries` | Libraries for libbluray (`aacs` → `LIBAACS_PATH`, `bdplus` → `LIBBDPLUS_PATH`) and libdvdread (`dvdcss`). Lumen preloads them. On Windows and macOS, Lumen's own libdvdcss stand-in (no CSS code) forwards to the `dvdcss` library (`LUMEN_DVDCSS_LIBRARY`). On Linux, libdvdread finds it by its standard name (`libdvdcss.so.2`). On Windows the library's folder is added to `PATH` so that dependencies next to it are found. |

With the disc libraries in place, libbluray/libdvdread and mpv use them for Blu-ray, UHD Blu-ray
and DVD, both in the title list and in the disc menus. The **Titles** tab shows what libbluray
reports (for example "AACS · decrypted").
[`examples/disc-libraries`](examples/disc-libraries/plugin.json) is a template for this.

## Native plugins

Include [`include/lumen/plugin.h`](../include/lumen/plugin.h) and export `lumen_plugin_entry()`:

```c
#include "lumen/plugin.h"

static void *init(const lumen_host *host)
{
    host->add_action(host->ctx, "hello", "Say hello");
    return (void *)host;
}

static void on_action(void *ctx, const char *id)
{
    const lumen_host *host = ctx;
    host->show_text(host->ctx, "Hello from a plugin", 2000);
}

LUMEN_PLUGIN_EXPORT const lumen_plugin *lumen_plugin_entry(void)
{
    static const lumen_plugin p = { sizeof(lumen_plugin), LUMEN_PLUGIN_API_VERSION,
                                    init, NULL, NULL, on_action };
    return &p;
}
```

| Hook | Purpose |
|------|---------|
| `init` / `shutdown` | Set up and tear down. `init` returns the plugin's context, or NULL to refuse loading. |
| `on_event` | `file-loaded`, `end-file`, `disc`, `shutdown`, each with a JSON payload. `disc` describes the scanned disc: `device`, `kind`, `label`, `discName`, `volumeId`, `titles`; for audio CDs also `mbDiscId` (MusicBrainz disc ID), `toc` and `cdText` |
| `on_action` | Buttons registered with `host->add_action` |
| `schemes` + `stream_open` | Own URL schemes (`myscheme://…`) as byte streams for mpv: network sources, container formats, or files the plugin decrypts itself |
| `dcp_content_key` | DCP content keys by key ID, e.g. from an HSM or a key server; used when no KDM delivered the key |

The host (`lumen_host`) offers these functions:

- mpv: `command`, `get_property` and `set_property` for the full [mpv API](https://mpv.io/manual/master/#properties);
- in the window: `show_text`, `add_action` and `set_status`;
- playback: `open`;
- disc metadata: `set_disc_info` (title, artist, year, cover URL, track names; since Lumen 0.2.1,
  check with `LUMEN_HOST_HAS(host, set_disc_info)`);
- folders: `plugin_dir` and `config_dir`;
- `log`.

Stream callbacks and `dcp_content_key` run on worker threads.
The API is versioned (`LUMEN_PLUGIN_API_VERSION`), and every struct starts with its size, so new
fields can be added without breaking existing plugins.

## Script plugins and Lumen

mpv scripts of a plugin (Lua or JavaScript) talk to Lumen through script messages (since Lumen 0.2.1):

```lua
-- events, same names and JSON payloads as on_event of native plugins
mp.register_script_message("lumen-event", function(event, json) ... end)

-- tell Lumen the script is up; Lumen repeats the last "disc" event for scripts that started later
mp.commandv("script-message", "lumen-plugin", "ready")

-- HTTP GET through Lumen (no curl needed); the answer arrives as script-message <reply-name> <status> <body>,
-- status 0 means a network error and <body> is the error text. Optional 5th argument: headers as JSON.
mp.commandv("script-message", "lumen-plugin", "http", "my-reply-1", "https://example.org/data.json")

-- metadata for the scanned disc: shown as disc name, cover and track names
mp.commandv("script-message", "lumen-plugin", "disc-info",
            '{"device":"D:/","title":"Album","artist":"Artist","year":"1999","source":"My database",' ..
            '"cover":"https://example.org/cover.jpg","tracks":[{"title":"Track one"},{"title":"Track two"}]}')

-- status line under the plugin's name in the Plugins tab
mp.commandv("script-message", "lumen-plugin", "status", "my-plugin-id", "Ready")
```

Pass the `device` of the `disc` event back in `disc-info`; Lumen ignores metadata for a disc that is no
longer the current one. The store plugin
[disc-identify](https://github.com/Karl-Lauterbach24/Lumen-Plugins/tree/main/plugins/disc-identify) is a
complete example.

## Examples

| Folder | Shows |
|--------|-------|
| [`examples/demo`](examples/demo) | Native plugin (C): button, status, events, `xorfile://` scheme, DCP keys from a text file |
| [`examples/osd-clock`](examples/osd-clock) | Script-only plugin: Ctrl+T shows the clock, time remaining and when the film ends |
| [`examples/disc-libraries`](examples/disc-libraries) | Template for user-provided libaacs/libbdplus/libdvdcss |

Building with `-DLUMEN_BUILD_TESTS=ON` (or `-DLUMEN_BUILD_PLUGIN_EXAMPLES=ON`) builds the examples into
`<build>/plugins`. `plugin_test <build>/plugins [encrypted-test-dcp]` checks the whole chain:
- loading the plugins;
- the button and status line;
- the Lua script;
- playback through `xorfile://`;
- decrypting a DCP track with a key that comes from the plugin.
