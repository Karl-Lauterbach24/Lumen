<p align="center"><img src="resources/logo/lumen-logo.png" width="300" alt="Lumen – Disc · Cinema Player"></p>

# Lumen – Disc & Digital Cinema Player

**English** · [Deutsch](README.de.md) · [Français](README.fr.md) · [Español](README.es.md) · [Italiano](README.it.md) · [Português](README.pt.md)

A fast, minimalist player for home cinemas, screening rooms and small cinemas with **two windows**:

- **Player window** – native mpv window (gpu-next, D3D11/Vulkan/Wayland), can be pinned to an output device, HDR passthrough.
- **Control window** – Qt Quick: source, transport, titles, chapters, audio, subtitles, picture, cinema, output profiles.

Plays **Blu-ray / UHD / Blu-ray 3D, DVD-Video (with menus), HD DVD, Video-CD / Super Video-CD, Audio-CD,
Digital Cinema Packages (DCP, JPEG 2000, SMPTE and Interop, encrypted with KDM)** and every file format mpv can play.

> The interface is in English by default and can be switched on the start page to German, French, Spanish, Italian, Portuguese, Dutch or Polish.

## Screenshots

<p align="center">
  <img src="docs/screenshots/start.png" width="49%" alt="Start page with language selection">
  <img src="docs/screenshots/cinema.png" width="49%" alt="Playing a Digital Cinema Package (Cinema tab)">
</p>
<p align="center">
  <img src="docs/screenshots/streaming.png" width="49%" alt="Stream links and media servers (Jellyfin, Emby, Plex)">
  <img src="docs/screenshots/plugins.png" width="49%" alt="Installed plugins and the plugin store">
</p>
<p align="center">
  <img src="docs/screenshots/output.png" width="49%" alt="Output: language, updates, automatic screen assignment, profiles">
</p>

## Copy protection / encryption

Lumen **does not circumvent any copy protection**. Anything of that kind is the user's own choice and goes through
**[plugins](plugins/README.md)** the user installs and enables: a plugin can make user-provided libraries
(libaacs, libbdplus, libdvdcss) available to libbluray/libdvdread, register its own decrypting URL schemes, or
supply DCP keys. Lumen ships and downloads none of these.

- **Blu-ray:** discs are read exclusively through `libbluray`. If a disc is AACS/BD+ protected, this must already be
  handled outside of Lumen – e.g. a drive with LibreDrive firmware plus a user-installed AACS library that libbluray loads
  at runtime. The same applies to the second 3D view (`bd_open_file_dec`).
- **DVD:** discs are read through `libdvdread`/`libdvdnav`; Lumen contains no CSS code. The MSYS2 and Homebrew builds of
  libdvdread link `libdvdcss` directly. Lumen's Windows and macOS packages therefore contain **Lumen's own stand-in**
  (`src/dvdcss_shim.c`) under that name instead of libdvdcss. It reads sectors unchanged, so unencrypted discs, images
  and folders play. If the user adds a real libdvdcss through a plugin, the shim forwards to it
  (`LUMEN_DVDCSS_LIBRARY`).
- **HD DVD:** only unprotected (or already decrypted) discs play; AACS-protected HD DVDs are reported as such.
- **DCP:** encrypted DCPs are decrypted the way every cinema server does it – with a **KDM issued for this player's
  certificate** (see below). Without a matching, currently valid KDM (or keys the content owner entered themselves),
  encrypted content stays unreadable.

## Downloads

Precompiled releases are on the [Releases page](https://github.com/Karl-Lauterbach24/Lumen/releases):
- Windows: a portable ZIP;
- macOS: a DMG for Apple Silicon.

Lumen checks for new releases at start, at most once a day and only if enabled. It installs an update with
one click, after verifying the download against the release's `SHA256SUMS.txt`.

## Streaming and media servers

The **Streaming** tab plays links of every kind mpv understands: HTTP(S) files, HLS (`.m3u8`), DASH (`.mpd`),
RTSP, RTMP, SRT, UDP and SMB. It keeps a history of recent links. Web pages such as YouTube play through
[yt-dlp](https://github.com/yt-dlp/yt-dlp) when it is installed next to Lumen or in the `PATH`.

Media servers:

| Server | Sign-in | Features |
|--------|---------|----------|
| **Jellyfin**, **Emby** | Username + password; only the access token is stored | Libraries, series/seasons/episodes, search, continue watching, posters, direct play from the resume position, progress reported to the server |
| **Plex** | *Sign in with Plex*, via a PIN in your own browser, or server URL + `X-Plex-Token` | Libraries, series/seasons/episodes, search, on deck, posters, direct play, progress reported through the timeline |

## Screens

With the output set to *Automatic* (the default), Lumen assigns screens itself:
- the player uses the screen with the most pixels (then refresh rate and HDR);
- the control window moves to the smallest remaining screen, e.g. the laptop next to a projector.

Plugging screens in or out updates the assignment. You can switch this off in the *Output* tab.

## Plugin store

The *Plugins* tab installs plugins from the official store
[Lumen-Plugins](https://github.com/Karl-Lauterbach24/Lumen-Plugins) and from your own sources. A source can be
`owner/repo`, a GitHub URL, or a URL or folder with an `index.json`. Every file is checked against its SHA-256 sum,
and newly installed plugins start disabled.

## Plugins

Folders with a `plugin.json`, managed in the **Plugins** tab (enable, disable, buttons, status). A plugin
can include any of these:
- a native C-ABI library ([`include/lumen/plugin.h`](include/lumen/plugin.h)): events, buttons, its own
  URL schemes as sources, DCP content keys, mpv commands/properties;
- mpv Lua/JavaScript scripts;
- mpv options;
- environment variables;
- user-provided disc libraries.

Documentation and examples: [plugins/README.md](plugins/README.md).

## Features

| Area | Scope |
|---|---|
| Sources | Optical drives (auto-detection, vendor/model/firmware, eject, autoplay on insert), ISO (Blu-ray/DVD/HD DVD detected automatically), CUE/BIN/NRG images, disc folders (BDMV, VIDEO_TS, HVDVD_TS, MPEGAV/MPEG2), DCP folders and cinema drives, every format mpv can play |
| Blu-ray | Disc menus (HDMV, BD-J with Java), main feature, titles/playlists with duration, video/audio format, UHD and 3D detection |
| **Blu-ray 3D** | **Both views (MVC)**, output as HDMI Frame Packing 1080p, side-by-side / top-and-bottom (half/full), row-interleaved, anaglyph or 2D; subtitles and menus per eye with adjustable depth |
| **DVD-Video** | **Disc menus** via libdvdnav (root/title/audio/subtitle menus, buttons with keyboard, remote and mouse), stills, own subpicture decoder with the disc palette and button highlights, forced subtitles, multi-angle, languages from the IFO, region/language from system settings, title & chapter selection |
| **DCP** | SMPTE and Interop, OV/VF (supplemental packages in neighbouring folders), multi-reel CPLs with entry points, **JPEG 2000 (XYZ → display colour space)**, 24-bit PCM up to 16 channels, **encrypted DCPs (KDM, AES-128)**, subtitles (Interop XML and SMPTE Timed Text incl. encrypted MXF and embedded fonts → positioned ASS), **CPL markers as chapters** (FFOC, LFOC, FFEC, FFMC …), **3D DCPs**, hash verification against the PKL |
| HD DVD | Titles and chapters from the Advanced Content playlists (ADV_OBJ/*.XPL), EVO playback (VC-1/AVC/MPEG-2, DD+, DTS-HD, TrueHD) |
| Video-CD / SVCD | Sector-exact reading of Mode 2 Form 2 tracks via libcdio (drive or CUE/BIN/NRG), entry points (ENTRIES.VCD/SVD) as chapters, **PBC menus** (VCD 2.0 playback control: selection lists with number keys, play lists, Next/Previous/Return/Default, wait times, loops, segment stills), fallback via the file system |
| Audio-CD | Tracks as chapters (mpv cdda) |
| Transport | Play/pause, stop, ±10 s/±60 s, chapters, frame step back/forward, A-B loop, speed, scrubbing with chapter marks, screenshot, **show playlist** (ads, trailers, feature back to back) |
| Audio | Track selection, bitstream (TrueHD/Atmos, DTS-HD MA/DTS:X, DD+, DD), WASAPI exclusive, channel layout, audio delay; **cinema fader** (Dolby scale, 7.0 = reference) and **DCP channel routing** (5.1, 7.1 DS, HI, VI-N) |
| Subtitles | Track selection (PGS/SRT/ASS/VobSub/DCP), forced only, delay, size, position (cinemascope screens) |
| Picture | Aspect ratio, pan & scan, zoom, brightness/contrast/saturation/gamma, deinterlacing, 3D source format for files |
| Output profiles | Target device, fullscreen, refresh-rate matching, automatic system HDR, HDR passthrough or tone mapping, **reference scaling** (EWA Lanczos 4, error diffusion, HDR contrast recovery), **calibration** (system/own ICC profile, 3D LUT `.cube`, native contrast, dither depth, GLSL shaders), sync, 3D output format, audio device, expert options |

Presets: *Desktop*, *1080p DLP 3D projector* (half SBS), *1080p DLP 3D projector (Frame Packing)*,
**Cinema projector DCI-P3 (gamma 2.6, 48 cd/m²)**, **Mastering monitor P3-D65**,
*4K HDR LED projector (passthrough)*, *4K HDR LED projector (player tone mapping)*, *4K HDR TV (OLED)*, *Performance/Laptop*.

## Digital Cinema Packages

```
ASSETMAP ─> PKL ─> CPL (reels: picture | sound | subtitles | markers)
                     │
                     ├─ picture MXF ─ lumendcp:// (decrypts on read) ─ FFmpeg J2K ─ XYZ → display
                     ├─ sound MXF   ─ lumendcp://                    ─ PCM 24 bit ─ fader / channel routing
                     ├─ Atmos/IAB   ─ lumeniab:// (decrypt → DTS IAB renderer → 7.1.4/5.1.4/7.1/5.1/2.0)
                     ├─ subtitles   ─ Interop XML / SMPTE Timed Text  ─ positioned ASS (+ fonts)
                     └─ markers     ─ chapters
          all reels ─> one mpv EDL (3D: left + right eye → lavfi hstack → 3D output format)
```

- **Opening:** "Kino" tab → *DCP öffnen …*, the folder dialog, drag a DCP folder onto the command line, or plug in a
  cinema drive: DCPs in the root or one folder level down show up in the drive list.
- **Encrypted DCPs / KDM workflow**
  1. *Kino → Zertifikat erzeugen* creates a SMPTE ST 430-2 certificate chain (RSA 2048, SHA-256) for this player.
     Alternatively *Importieren …* uses an existing leaf certificate + private key (PEM), e.g. from DCP-o-matic.
  2. *Leaf exportieren* – send `lumen-leaf.pem` to the distributor / KDM creator.
  3. *KDM laden …* – the content keys are unwrapped with RSA-OAEP; KDMs are stored (still encrypted) in the
     configuration folder and unwrapped again on every start. Validity windows are checked; the CPL list shows
     "KDM gültig bis …", "abgelaufen" or "noch nicht gültig".
  4. Playback decrypts each KLV triplet (SMPTE ST 429-6, AES-128-CBC) on the fly. The decrypted triplet is replaced by
     *essence KLV + KLV fill of the same size*, so all index tables and partition offsets stay valid and seeking works.
     A wrong key is detected via the triplet check value.
  - For your own DCPs, *Schlüsseldatei …* accepts `<key id> <key hex>` lines (session only, not stored).
- **JPEG 2000 performance:** J2K has resolution levels. In *automatic* mode Lumen decodes only the level the output
  needs (4K DCP on a 2K/1080p output = half the work, no visible loss) and, if the CPU cannot keep up (dropped frames
  in the first 30 s), switches one level lower on the fly. Manual: full / half / quarter.
- **Sound:** 16-channel DCPs are routed per SMPTE 428-12 (L R C LFE Ls Rs · HI · VI-N · … · Lrs Rrs); *auto* uses 7.1 DS
  for 16 channels, 5.1 otherwise; HI and VI-N can be selected for accessibility. The fader follows the cinema processor
  scale (7.0 = 0 dB, 3.33 dB per step above 4).
- **Immersive audio (Dolby Atmos / SMPTE IAB):** Atmos DCP tracks (`AuxData`, ST 429-18) carry SMPTE ST 2098-2 IAB
  frames. Lumen reads them frame by frame (and decrypts them with the KDM key, type MDEK) and renders beds and objects
  with DTS's open [IAB renderer](https://github.com/DTSProAudio/iab-renderer) (BSD-3-Clause), using VBAP, onto
  **7.1.4, 5.1.4, 7.1, 5.1 or stereo** (*Kino → Atmos/IAB-Ausgabe*). mpv receives this as a 32-bit float WAV stream
  with a proper channel mask, and it becomes the preferred audio track of the composition. The PCM version stays
  selectable. The 5.1 and 7.1 layouts are derived from DTS's 5.1.4/7.1.4 configurations, with the heights folded down
  (`resources/iab`). The renderer is fetched automatically at build time (pinned commit), or from `-DIAB_SOURCE_DIR`;
  `-DLUMEN_WITH_IAB=OFF` builds without it.
- **Verification:** *Prüfen* hashes all track files of a CPL (SHA-1) and compares them with the packing list.
- **Show playlist:** add CPLs (*Ins Programm*) or the running source and play them back to back.

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
- There is no hardware decoding for MVC; detected 3D discs are decoded in software.

## DVD menus

- libdvdnav runs the DVD's virtual machine and feeds MPEG-PS to mpv via `lumendvd://`.
- mpv does not know the DVD palette, so Lumen replaces the subpicture packets in the stream with padding and decodes them
  itself (RLE, display control sequences, IFO palette, button highlight colours) – menus and subtitles are drawn as an
  overlay scaled to the video area, subtitles synchronised to the playback clock by PTS.
- Still-frame menus: a sequence end code is injected so the decoder shows the still immediately.
- Operation: arrows/Enter/mouse in the player window, D-pad in the control window, Home = root menu, End = title menu,
  buttons for audio/subtitle menu, "back" and angle switching.

## Architecture

```
src/MpvController   libmpv instance, event-driven property observation, profiles -> mpv options, source dispatch, show playlist
src/BlurayNav       libbluray as mpv stream "lumenbd://": menus, titles, 3D, overlays per eye
src/MvcMerger       Blu-ray 3D: mixes in the dependent view (SS sub-path, EP map, PTS pairing)
src/DvdNav          libdvdnav as mpv stream "lumendvd://": menus, SPU decoder, highlights, streams, angles
src/OpticalMedia    Video-CD/SVCD ("lumenvcd://", libcdio), Audio-CD, HD DVD (XPL playlists, EVO)
src/DcpPackage      DCP: ASSETMAP/PKL/CPL parser, MXF header probe (resolution, channels, 3D, encryption)
src/DcpCrypto       OpenSSL: certificate chain (SMPTE 430-2), KDM unwrap (RSA-OAEP), AES-128-CBC
src/DcpStream       "lumendcp://": MXF reader with size-preserving triplet decryption and eye filter
src/DcpSubtitles    Interop/SMPTE subtitles -> ASS, embedded fonts
src/DcpManager      DCP workflow for the UI: packages, keys, EDL/chapters, fader, routing, J2K levels, verification
src/DisplayManager  output devices, refresh / HDR / frame-packing mode (Windows, xrandr, kscreen-doctor, CoreGraphics)
src/DriveManager    drives/discs/cinema drives (worker thread), eject
src/DiscScanner     titles and status for Blu-ray, DVD, HD DVD, VCD, Audio-CD (worker thread)
src/PlayerWindow    embedded player window (mpv render API/OpenGL), mainly for macOS
src/ProfileManager  presets + user profiles
qml/                control window (CinemaPane.qml = "Kino" tab)
tools/              build/deploy scripts, test-disc/DCP/VCD generators
tests/              mvcmerge_test (MVC merging), dcp_test (DCP chain)
```

## Building

Requirements: CMake ≥ 3.21, Qt ≥ 6.5 (Quick, QuickControls2, OpenGL, Xml), libmpv ≥ 0.38 **linked against
shared FFmpeg libraries**, libbluray ≥ 1.2, FFmpeg-mvc matching libmpv's FFmpeg major version.
Optional: **OpenSSL ≥ 1.1** (encrypted DCPs), **libdvdnav ≥ 6** (DVD menus), **libcdio + libiso9660** (Video-CD from
drives and images) – each enabled automatically when found (`-DLUMEN_WITH_OPENSSL/DVDNAV/CDIO=OFF` to disable).

### Windows (MSYS2 UCRT64 – tested: GCC 16, Qt 6.11, mpv 0.41, libbluray 1.5, libdvdnav 7, libcdio 2.4, OpenSSL 3.6, FFmpeg-mvc 9.0.2)

All parts must use the same C runtime (UCRT) – therefore Qt, libmpv and libbluray come from MSYS2.
The tools deliberately live in a **short path** (`C:\lumen-build`), because GCC and the FFmpeg build
otherwise fail at Windows' 260-character path limit.

```bash
# 1. Packages (no MSYS2 installation needed, just extraction)
python tools/msys2_fetch.py --repo ucrt64 --dest C:/lumen-build/msys2 mpv qt6-base qt6-declarative qt6-svg qt6-tools gcc nasm pkgconf dav1d libva cmake ninja openssl libdvdnav libcdio --skip mingw-w64-ucrt-x86_64-ffmpeg
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
sudo apt install qt6-declarative-dev qml6-module-qtquick-dialogs qml6-module-qtcore libbluray-dev libdvdnav-dev libcdio-dev libiso9660-dev libssl-dev nasm libdav1d-dev
tools/build_ffmpeg_mvc.sh            # FFMPEG_MVC_BRANCH=release/8.1 for FFmpeg 8.x
```
libmpv must be built against **the same FFmpeg major version** (`ldd $(which mpv) | grep avcodec`).
If your distribution doesn't match, build mpv against `3rdparty/ffmpeg-mvc` (`meson setup -Dlibmpv=true`,
`PKG_CONFIG_PATH=3rdparty/ffmpeg-mvc/lib/pkgconfig`). At runtime use `LD_LIBRARY_PATH=3rdparty/ffmpeg-mvc/lib`.

### macOS

```bash
brew install qt mpv libbluray libdvdnav libcdio openssl@3 libxml2 pkgconf ninja
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$(brew --prefix qt)
cmake --build build                      # build/Lumen.app
cmake --build build --target lumen_dmg   # self-contained Lumen.app + Lumen.dmg (macdeployqt)
```
Homebrew's keg-only OpenSSL and libxml2 are found automatically. libmpv can't open its own window on macOS,
so the player window is always the embedded one (mpv render API, OpenGL 3.2 Core, SDR); refresh-rate switching
uses CoreGraphics, HDR is left to macOS. The GitHub Actions workflow [`macos.yml`](.github/workflows/macos.yml)
builds on macOS 15 (Apple Silicon), runs the DCP and DVD tests plus a render check of the player window, and
uploads `Lumen.dmg`.
Blu-ray 3D: build FFmpeg-mvc as on Linux (`brew install nasm dav1d`, branch matching Homebrew's FFmpeg) and
start Lumen with `DYLD_LIBRARY_PATH=3rdparty/ffmpeg-mvc/lib`.

### Without 3D

Any libmpv works (e.g. the shinchiro SDK with `-DMPV_ROOT=…`); Blu-ray 3D then plays in 2D and the
UI shows a notice ("Kein MVC-Decoder"). 3D DCPs don't need FFmpeg-mvc.

## Tests

```bash
cmake -DLUMEN_BUILD_TESTS=ON …        # builds mvcmerge_test and dcp_test

# Blu-ray 3D: synthetic 3D disc from an MVC test stream (FFmpeg-mvc fixture: left eye luma 165, right eye 36)
perl <ffmpeg-mvc>/tests/fate/h264-mvc/mvc-mkfix.pl --out=mvc8.h264 --base=100 --dep=-100 --base-frames=8 --dep-frames=8
python tools/make_test_bd3d.py mvc8.h264 bd3d
mvcmerge_test bd3d 0 merged.m2ts
ffmpeg -view_ids -1 -i merged.m2ts -fps_mode passthrough -f rawvideo -pix_fmt gray - | od -An -tu1 -w512 -v | awk '{print $1","$17}' | sort | uniq -c
#   -> 8 165,36  (every frame: base view left, dependent view right)

# DCP: certificate -> encrypted test DCP + KDM -> unwrap -> decrypt -> play (libmpv, vo=null)
dcp_test gencert id                                   # id/leaf.pem, id/leaf.key
python tools/make_test_dcp.py ffmpeg openssl dcp --encrypt id/leaf.pem
dcp_test info dcp                                     # reels, entry points, key IDs, MXF header
dcp_test kdm dcp/kdm.xml id/leaf.key                  # == dcp/keys.txt
dcp_test decrypt dcp/picture.mxf <key> plain.mxf      # frames identical to the unencrypted source; wrong key -> check value errors
dcp_test play dcp dcp/kdm.xml id/leaf.key 1.5         # loaded=1 … keyErrors=0
dcp_test subs dcp                                     # generated ASS

# Atmos/IAB: IAB frames (DTS packer) -> encrypted DCP track -> render + check every layout -> play through mpv
iab_testgen iab.bin 48
python tools/make_test_dcp.py ffmpeg openssl atmos --encrypt id/leaf.pem --iab iab.bin
dcp_test iab atmos/atmos.mxf <key> 7.1.4 --check       # object L -> RFH, LFE tone, 0 frame errors
dcp_test play atmos atmos/kdm.xml id/leaf.key 1.5 --iab # audio-ch=12

# Plugins: examples in <build>/plugins
plugin_test <build>/plugins dcp                       # native + script plugin, xorfile://, DCP key from plugin

# Video-CD with PBC menus (authored with vcdxbuild from GNU VCDImager)
python tools/make_test_vcd_pbc.py ffmpeg vcdxbuild vcd && vcd_test vcd/pbc.cue

# Video-CD: CUE/BIN image with Mode 2 Form 2 track
python tools/make_test_vcd.py ffmpeg vcd && lumen vcd/vcd.cue
```

Developer aids: `LUMEN_SNAPSHOT=shot.png` (optionally `LUMEN_SNAPSHOT_DELAY=ms`) saves the control window as an image;
`LUMEN_MPV_LOG=warn|info|v` forwards mpv's log (with `QT_FORCE_STDERR_LOGGING=1` on Windows).

## Command line

```bash
lumen D:\                      # drive: detects Blu-ray / DVD / HD DVD / VCD / Audio-CD and plays the main feature
lumen --menu D:\               # with disc menu (Blu-ray, DVD; e.g. for HTPC launchers)
lumen --menu Movie.iso         # ISO (Blu-ray, DVD or HD DVD) / disc folder with menu
lumen VideoCD.cue              # Video-CD / SVCD image
lumen E:\DCP\Feature_FTR       # DCP folder: plays the first (feature) CPL
lumen --kdm feature.xml E:\DCP\Feature_FTR   # load a KDM first
lumen movie.mkv                # any file mpv can play
```

## Keyboard

| Key | Control window | Player window |
|---|---|---|
| Space | Play/pause | Play/pause |
| ← / → (Shift) | ±10 s (±60 s), in menus: navigate | ±10 s, in menus: navigate |
| ↑ / ↓ | Volume, in menus: navigate | ±60 s, in menus: navigate |
| Enter / click | In menus: confirm | In menus: confirm, otherwise Enter = fullscreen |
| Home / End | Top menu / pop-up menu (DVD: title menu) | Top menu / pop-up menu (DVD: title menu) |
| Page up / down | Next / previous chapter | Next / previous chapter |
| , / . | Frame step | Frame step |
| F / double-click | Fullscreen | Fullscreen (Enter / double-click) |
| L | A-B loop | A-B loop |
| S | Screenshot | Screenshot |
| I | Statistics overlay | Statistics overlay |
| [ / ] / ⌫ | Speed ∓ / reset | |
| Ctrl+O / Ctrl+E / Ctrl+D | Open / eject / "Kino" tab | |

## Blu-ray disc menus

- libbluray runs the disc's menu program and feeds the stream to mpv via `lumenbd://`;
  menu graphics (IG or BD-J) arrive as an ARGB overlay scaled to the video area (per eye in 3D mode).
- Audio/subtitle choices made in the disc menu are mapped via the stream PID to the matching track.
- **BD-J menus** need a Java runtime (JRE ≥ 8) and `libbluray-j2se-*.jar`; without them, title mode remains available.

## Limitations / status

| Topic | Status |
|---|---|
| Blu-ray 3D | Implemented and tested end-to-end with a synthetic 3D disc (merge, seek, SBS, frame packing). **Not yet tested with a real 3D disc**. FFmpeg-mvc is an experimental fork. |
| DCP | Tested end-to-end with synthetic SMPTE DCPs on Windows and macOS (CI). The test DCPs have 2 reels with entry points, markers, Interop text and image subtitles, closed captions, a signed KDM, 3D, and an Atmos/IAB track. All essence is encrypted, and decrypted frames are bit-identical to the source. The IAB object and bed positions are checked per speaker for every layout. **Not yet tested with real cinema DCPs/KDMs or real Atmos mixes.** Not supported: forensic marking. Atmos tracks from before the SMPTE standard may contain elements the IAB renderer rejects (untested). |
| JPEG 2000 | Software decoding (FFmpeg, frame + slice threads). 2K at 24 fps needs a strong multi-core CPU (≈ 24 fps on 12 threads at 130 Mbit/s); automatic resolution-level fallback when frames drop. |
| DVD | Menu navigation, SPU decoding and highlights are implemented but **not yet tested with real DVDs** (no DVD authoring tools available in the build environment). Without libdvdnav, DVDs play via mpv `dvd://` (no menus). |
| HD DVD | Tested with a synthetic HVDVD_TS/XPL structure. HDi interactivity (menus) is not supported; titles/chapters come from the playlist. AACS-protected discs do not play. |
| Video-CD | Playback control (PBC) is tested with a VCD 2.0 authored with GNU VCDImager's `vcdxbuild`, on Windows and macOS. The test covers selection by number, Default, Next/Prev/Return, automatic continuation, entry points and segment menus. Not supported: SVCD extended selection areas (mouse areas) and command lists. **Not yet tested with a real pressed VCD.** |
| Frame packing | Requires a 1920×2205 mode in the graphics driver; whether the projector recognizes it as 3D without an HDMI 3D InfoFrame depends on the device. |
| Dolby Vision | Detected (profile 5/7/8), gpu-next applies the RPU metadata; no PC player can output a real DV signal over HDMI. |
| Refresh/HDR switching | Windows: refresh + HDR · Linux X11: refresh (xrandr) · KDE Plasma: refresh + HDR · GNOME Wayland: display only · macOS: refresh |
| Embedded player window | Automatic on macOS, otherwise per profile; OpenGL render API → SDR only. |

## License

Lumen is free software under the **GNU Affero General Public License v3.0 or later** ([LICENSE](LICENSE)). Components and their licenses: [THIRD_PARTY.md](THIRD_PARTY.md).
