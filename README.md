# Lumen – Blu-ray Player

**English** · [Deutsch](README.de.md) · [Français](README.fr.md) · [Español](README.es.md) · [Italiano](README.it.md) · [Português](README.pt.md)

A fast, minimalist Blu-ray / UHD / 3D player for home-cinema setups with **two windows**:

- **Player window** – native mpv window (gpu-next, D3D11/Vulkan/Wayland), can be pinned to an output device, HDR passthrough.
- **Control window** – Qt Quick: source, transport, titles, chapters, audio, subtitles, picture, output profiles.

> The user interface is currently in German.

## Copy protection / LibreDrive

Lumen **does not circumvent any copy protection**. Discs are read exclusively through `libbluray`.
If a disc is AACS/BD+ protected, this must already be handled outside of Lumen – e.g. a drive with
LibreDrive firmware plus a user-installed AACS library that libbluray loads at runtime. The same
applies to the second 3D view: it is read through libbluray (`bd_open_file_dec`) and therefore through
the same external library.

## Features

| Area | Scope |
|---|---|
| Sources | Optical drives (auto-detection, vendor/model/firmware, eject, autoplay on insert), ISO, BDMV folders, every format mpv can play |
| Disc | Disc menus (HDMV, BD-J with Java), main feature, titles/playlists with duration, video/audio format, UHD and 3D detection |
| **Blu-ray 3D** | **Both views (MVC)**, output as HDMI Frame Packing 1080p, side-by-side / top-and-bottom (half/full), row-interleaved, anaglyph or 2D; subtitles and menus per eye with adjustable depth |
| Transport | Play/pause, stop, ±10 s/±60 s, chapters, frame step back/forward, A-B loop, speed, scrubbing with chapter marks, screenshot |
| Audio | Track selection, bitstream (TrueHD/Atmos, DTS-HD MA/DTS:X, DD+, DD), WASAPI exclusive, channel layout, audio delay |
| Subtitles | Track selection (PGS/SRT/ASS), forced only, delay, size, position (cinemascope screens) |
| Picture | Aspect ratio, pan & scan, zoom, brightness/contrast/saturation/gamma, deinterlacing, 3D source format for files |
| Output profiles | Target device, fullscreen, refresh-rate matching (23.976 → 23/24 Hz), automatic system HDR on/off, HDR passthrough or tone mapping, scaling quality, sync, 3D output format, audio device, expert options |

Presets: *Desktop*, *1080p DLP 3D projector* (half SBS), *1080p DLP 3D projector (Frame Packing)*,
*4K HDR LED projector (passthrough)*, *4K HDR LED projector (player tone mapping)*, *4K HDR TV (OLED)*, *Performance/Laptop*.

## Blu-ray 3D

```
libbluray bd_read_ext()  ── base view (PID 0x1011) ─────┐
                                                         ├─ MvcMerger ─ TS ─> mpv ─> FFmpeg-mvc ─> SBS frame
libbluray bd_open_file_dec() ─ dependent view (0x1012) ─┘   (paired per frame by PTS)             │
                                                                                                   v
                                         vf: stereo3d / frame-packing graph ─> format of the output profile
```

- **Decoder:** stock FFmpeg only decodes the base view. Lumen uses
  [FFmpeg-mvc](https://github.com/tthayer93/FFmpeg-mvc) (branch `release/9.0`), which decodes both views
  into one side-by-side frame. Its DLLs have the same names/ABI as FFmpeg 9.0 and replace libmpv's
  libraries one-to-one. Lumen detects the decoder by its version string (`…-mvc`).
- **Second view:** libbluray only delivers the base view. `MvcMerger` reads the playlist's SS sub-path
  (which clip), the CLPI EP map (seek points) and the dependent `.m2ts` via libbluray, pairs access
  units by PTS and appends the MVC NAL units to the base-view NAL units.
- **3D playback always goes through libbluray** (`lumenbd://`), including "main feature" and title selection.
  The disc is told "3D preferred" (PSR21/23) so that menus select the 3D playlist.
- **Subtitles/menus:** libbluray renders PG subtitles and menu graphics; Lumen draws them once per eye
  (adjustable depth in the profile or in the "Untertitel" tab).
- **Frame Packing (HDMI 1.4):** 1920×2205 (1080 + 45 + 1080 lines) @ 23.976 Hz. The display mode must
  be created as a custom resolution in the graphics driver; Lumen then switches to it automatically.
  Many projectors detect frame packing from the timing; where they don't, use SBS/TAB.
- There is no hardware decoding for MVC (no GPU supports it); detected 3D discs are decoded in
  software (1080p24 AVC, no problem for current CPUs).

## Architecture

```
src/MpvController   libmpv instance, event-driven property observation, profiles -> mpv options
src/BlurayNav       libbluray as mpv stream "lumenbd://": menus, titles, 3D, overlays per eye
src/MvcMerger       Blu-ray 3D: mixes in the dependent view (SS sub-path, EP map, PTS pairing)
src/DisplayManager  output devices, refresh / HDR / frame-packing mode (Windows, xrandr, kscreen-doctor, CoreGraphics)
src/DriveManager    drives/discs (worker thread), eject
src/DiscScanner     libbluray: titles, durations, AACS/BD+/BD-J/3D status (worker thread)
src/PlayerWindow    embedded player window (mpv render API/OpenGL), mainly for macOS
src/ProfileManager  presets + user profiles
qml/                control window
tools/              build/deploy scripts, test-disc generator
tests/              mvcmerge_test (MVC merging against a disc structure)
```

## Building

Requirements: CMake ≥ 3.21, Qt ≥ 6.5 (Quick, QuickControls2, OpenGL), libmpv ≥ 0.38 **linked against
shared FFmpeg libraries**, libbluray ≥ 1.2, FFmpeg-mvc matching libmpv's FFmpeg major version.

### Windows (MSYS2 UCRT64 – tested: GCC 16, Qt 6.11, mpv 0.41, libbluray 1.5, FFmpeg-mvc 9.0.2)

All parts must use the same C runtime (UCRT) – therefore Qt, libmpv and libbluray come from MSYS2.
The tools deliberately live in a **short path** (`C:\lumen-build`), because GCC and the FFmpeg build
otherwise fail at Windows' 260-character path limit.

```bash
# 1. Packages (no MSYS2 installation needed, just extraction)
python tools/msys2_fetch.py --repo ucrt64 --dest C:/lumen-build/msys2 mpv qt6-base qt6-declarative qt6-svg qt6-tools gcc nasm pkgconf dav1d libva cmake ninja --skip mingw-w64-ucrt-x86_64-ffmpeg
python tools/msys2_fetch.py --repo msys --dest C:/lumen-build/msys2-tools make diffutils --skip msys2-runtime bash
# 2. FFmpeg-mvc (Git Bash, ~20 min) -> 3rdparty/ffmpeg-mvc
tools/build_ffmpeg_mvc.sh
# 3. Lumen (deployment runs as a post-build step: windeployqt + DLLs, FFmpeg-mvc before everything else)
U=C:/lumen-build/msys2/ucrt64; PATH=$U/bin:$PATH
cmake -S . -B build-ucrt -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$U -DMPV_ROOT=$U -DBLURAY_ROOT=$U -DLUMEN_DEPLOY_SEARCH=$U/bin
cmake --build build-ucrt
```

### Linux

```bash
sudo apt install qt6-declarative-dev qml6-module-qtquick-dialogs qml6-module-qtcore libbluray-dev nasm libdav1d-dev
tools/build_ffmpeg_mvc.sh            # FFMPEG_MVC_BRANCH=release/8.1 for FFmpeg 8.x
```
libmpv must be built against **the same FFmpeg major version** (`ldd $(which mpv) | grep avcodec`).
If your distribution doesn't match, build mpv against `3rdparty/ffmpeg-mvc` (`meson setup -Dlibmpv=true`,
`PKG_CONFIG_PATH=3rdparty/ffmpeg-mvc/lib/pkgconfig`). At runtime use `LD_LIBRARY_PATH=3rdparty/ffmpeg-mvc/lib`.

### macOS

`brew install qt mpv libbluray nasm dav1d`, build FFmpeg-mvc as on Linux (branch matching Homebrew's FFmpeg),
set `DYLD_LIBRARY_PATH` to `3rdparty/ffmpeg-mvc/lib`. The player window is embedded automatically on macOS.

### Without 3D

Any libmpv works (e.g. the shinchiro SDK with `-DMPV_ROOT=…`); Blu-ray 3D then plays in 2D and the
UI shows a notice ("Kein MVC-Decoder").

## Tests

```bash
# Synthetic 3D disc from an MVC test stream (FFmpeg-mvc fixture: left eye luma 165, right eye 36)
perl <ffmpeg-mvc>/tests/fate/h264-mvc/mvc-mkfix.pl --out=mvc8.h264 --base=100 --dep=-100 --base-frames=8 --dep-frames=8
python tools/make_test_bd3d.py mvc8.h264 bd3d
# Check merging (cmake -DLUMEN_BUILD_TESTS=ON)
mvcmerge_test bd3d 0 merged.m2ts
ffmpeg -view_ids -1 -i merged.m2ts -fps_mode passthrough -f rawvideo -pix_fmt gray - | od -An -tu1 -w512 -v | awk '{print $1","$17}' | sort | uniq -c
#   -> 8 165,36  (every frame: base view left, dependent view right)
# In Lumen with a 3D profile:  lumen bd3d
```

## Command line

```bash
lumen D:\                      # drive: play main feature directly (in 3D with a 3D profile)
lumen --menu D:\               # drive with disc menu (e.g. for HTPC launchers)
lumen --menu Movie.iso         # ISO / BDMV folder with menu
lumen movie.mkv                # any file mpv can play
```

## Keyboard

| Key | Control window | Player window |
|---|---|---|
| Space | Play/pause | Play/pause |
| ← / → (Shift) | ±10 s (±60 s), in menus: navigate | ±10 s, in menus: navigate |
| ↑ / ↓ | Volume, in menus: navigate | ±60 s, in menus: navigate |
| Enter / click | In menus: confirm | In menus: confirm, otherwise Enter = fullscreen |
| Home / End | Top menu / pop-up menu | Top menu / pop-up menu |
| Page up / down | Next / previous chapter | Next / previous chapter |
| , / . | Frame step | Frame step |
| F / double-click | Fullscreen | Fullscreen (Enter / double-click) |
| L | A-B loop | A-B loop |
| S | Screenshot | Screenshot |
| I | Statistics overlay | Statistics overlay |
| [ / ] / ⌫ | Speed ∓ / reset | |
| Ctrl+O / Ctrl+E | Open / eject | |

## Disc menus

- libbluray runs the disc's menu program and feeds the stream to mpv via `lumenbd://`;
  menu graphics (IG or BD-J) arrive as an ARGB overlay scaled to the video area (per eye in 3D mode).
- Operation: arrows/Enter/mouse in the player window, D-pad in the control window, Home = top menu, End = pop-up.
- Audio/subtitle choices made in the disc menu are mapped via the stream PID to the matching track.
- **BD-J menus** need a Java runtime (JRE ≥ 8) and `libbluray-j2se-*.jar`; without them, title mode remains available.

## Limitations / status

| Topic | Status |
|---|---|
| Blu-ray 3D | Implemented and tested end-to-end with a synthetic 3D disc (merge, seek, SBS, frame packing). **Not yet tested with a real 3D disc**; the same applies to 3D subtitles/menus (test disc has no PG/IG). FFmpeg-mvc is an experimental fork. |
| Frame packing | Requires a 1920×2205 mode in the graphics driver; whether the projector recognizes it as 3D without an HDMI 3D InfoFrame depends on the device. |
| Dolby Vision | Detected (profile 5/7/8), gpu-next applies the RPU metadata; no PC player can output a real DV signal over HDMI. |
| Still-frame menus | Pure still-frame menus may stay black briefly (decoder delay). |
| Refresh/HDR switching | Windows: refresh + HDR · Linux X11: refresh (xrandr) · KDE Plasma: refresh + HDR · GNOME Wayland: display only · macOS: refresh |
| Embedded player window | Automatic on macOS, otherwise per profile; OpenGL render API → SDR only. |

Developer aid: `LUMEN_SNAPSHOT=shot.png` (optionally `LUMEN_SNAPSHOT_DELAY=ms`) saves the control window as an image.
