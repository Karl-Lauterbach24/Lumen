# Lumen – Blu-ray-Player

Schneller, minimalistischer Blu-ray/UHD/3D-Player für Heimkino-Setups mit **zwei Fenstern**:

- **Player-Fenster** – natives mpv-Fenster (gpu-next, D3D11/Vulkan/Wayland), fest auf ein Ausgabegerät legbar, HDR-Passthrough.
- **Steuerfenster** – Qt Quick: Quelle, Transport, Titel, Kapitel, Ton, Untertitel, Bild, Ausgabeprofile.

## Kopierschutz / LibreDrive

Lumen **umgeht keinen Kopierschutz**. Discs werden ausschließlich über `libbluray` gelesen.
Ist eine Disc AACS/BD+-geschützt, muss das außerhalb von Lumen bereits gelöst sein – z. B. ein
Laufwerk mit LibreDrive-Firmware plus eine vom Nutzer installierte AACS-Bibliothek, die libbluray
zur Laufzeit selbst lädt. Das gilt auch für die zweite 3D-Ansicht: sie wird über libbluray
(`bd_open_file_dec`) und damit über dieselbe externe Bibliothek gelesen.

## Funktionen

| Bereich | Umfang |
|---|---|
| Quellen | Optische Laufwerke (Auto-Erkennung, Hersteller/Modell/Firmware, Auswerfen, Autostart beim Einlegen), ISO, BDMV-Ordner, alle Formate, die mpv abspielt |
| Disc | Disc-Menüs (HDMV, BD-J mit Java), Hauptfilm, Titel/Playlists mit Laufzeit, Video-/Tonformat, UHD- und 3D-Erkennung |
| **Blu-ray 3D** | **Beide Ansichten (MVC)**, Ausgabe als HDMI Frame Packing 1080p, Side-by-Side/Top-and-Bottom (Half/Full), Zeilenverschachtelt, Anaglyph oder 2D; Untertitel und Menüs je Auge mit einstellbarer Tiefe |
| Transport | Play/Pause, Stopp, ±10 s/±60 s, Kapitel, Einzelbild vor/zurück, A-B-Schleife, Geschwindigkeit, Scrubbing mit Kapitelmarken, Screenshot |
| Ton | Spurwahl, Bitstream (TrueHD/Atmos, DTS-HD MA/DTS:X, DD+, DD), WASAPI exklusiv, Kanal-Layout, Audio-Verzögerung |
| Untertitel | Spurwahl (PGS/SRT/ASS), nur erzwungene, Verzögerung, Größe, Position (Cinemascope-Leinwand) |
| Bild | Seitenverhältnis, Pan & Scan, Zoom, Helligkeit/Kontrast/Sättigung/Gamma, Deinterlacing, 3D-Quellformat für Dateien |
| Ausgabeprofile | Zielgerät, Vollbild, Bildraten-Anpassung (23.976 → 23/24 Hz), System-HDR automatisch an/aus, HDR-Passthrough oder Tonemapping, Skalierungsqualität, Sync, 3D-Ausgabeformat, Audiogerät, Experten-Optionen |

Vorlagen: *Desktop*, *1080p DLP 3D-Beamer* (Half-SBS), *1080p DLP 3D-Beamer (Frame Packing)*,
*4K HDR LED-Projektor (Passthrough)*, *4K HDR LED-Projektor (Player-Tonemapping)*, *4K HDR TV (OLED)*, *Leistung/Laptop*.

## Blu-ray 3D

```
libbluray bd_read_ext()  ── Basisansicht (PID 0x1011) ──┐
                                                         ├─ MvcMerger ─ TS ─> mpv ─> FFmpeg-mvc ─> SBS-Vollbild
libbluray bd_open_file_dec() ─ abhängige Ansicht (0x1012) ┘   (pro Bild per PTS gepaart)          │
                                                                                                   v
                                         vf: stereo3d / Frame-Packing-Graph ─> Format des Ausgabeprofils
```

- **Decoder:** Das normale FFmpeg dekodiert nur die Basisansicht. Lumen nutzt
  [FFmpeg-mvc](https://github.com/tthayer93/FFmpeg-mvc) (Branch `release/9.0`), das beide Ansichten zu
  einem Side-by-Side-Vollbild dekodiert. Die DLLs haben dieselben Namen/ABI wie FFmpeg 9.0 und ersetzen
  die Bibliotheken von libmpv 1:1. Lumen erkennt den Decoder an der Versionskennung (`…-mvc`).
- **Zweite Ansicht:** libbluray liefert nur die Basisansicht. `MvcMerger` liest den SS-Subpfad der
  Playlist (welcher Clip), die EP-Map der CLPI (Sprungmarken) und die abhängige `.m2ts` über libbluray,
  paart Zugriffseinheiten per PTS und hängt die MVC-NALs an die Basis-NALs an.
- **3D-Wiedergabe läuft immer über libbluray** (`lumenbd://`), auch „Hauptfilm“ und Titelwahl.
  Die Disc bekommt „3D bevorzugt“ gemeldet (PSR21/23), damit Menüs die 3D-Playlist wählen.
- **Untertitel/Menüs:** libbluray rendert PG-Untertitel und Menügrafik; Lumen zeichnet sie je Auge
  (einstellbare Tiefe im Profil bzw. Reiter „Untertitel“).
- **Frame Packing (HDMI 1.4):** 1920×2205 (1080 + 45 + 1080 Zeilen) @ 23.976 Hz. Der Anzeigemodus
  muss im Grafiktreiber als benutzerdefinierte Auflösung angelegt sein; Lumen schaltet ihn dann
  automatisch. Viele Beamer erkennen Frame Packing am Timing; wo nicht, SBS/TAB verwenden.
- Hardware-Decoding gibt es für MVC nicht (keine GPU unterstützt es); erkannte 3D-Discs laufen in
  Software (1080p24 AVC, für aktuelle CPUs unkritisch).

## Architektur

```
src/MpvController   libmpv-Instanz, Property-Beobachtung (ereignisgesteuert), Profile -> mpv-Optionen
src/BlurayNav       libbluray als mpv-Stream "lumenbd://": Menüs, Titel, 3D, Overlays je Auge
src/MvcMerger       Blu-ray 3D: abhängige Ansicht zumischen (SS-Subpfad, EP-Map, PTS-Paarung)
src/DisplayManager  Ausgabegeräte, Refresh-/HDR-/Frame-Packing-Modus (Windows, xrandr, kscreen-doctor, CoreGraphics)
src/DriveManager    Laufwerke/Discs (Worker-Thread), Auswerfen
src/DiscScanner     libbluray: Titel, Laufzeiten, AACS/BD+/BD-J/3D-Status (Worker-Thread)
src/PlayerWindow    eingebettetes Player-Fenster (mpv-Render-API/OpenGL), v. a. für macOS
src/ProfileManager  Vorlagen + Nutzerprofile
qml/                Steuerfenster
tools/              Build-/Deploy-Skripte, Testdisc-Generator
tests/              mvcmerge_test (MVC-Zusammenführung gegen eine Disc-Struktur)
```

## Bauen

Voraussetzungen: CMake ≥ 3.21, Qt ≥ 6.5 (Quick, QuickControls2, OpenGL), libmpv ≥ 0.38 **gegen FFmpeg
als gemeinsame Bibliotheken**, libbluray ≥ 1.2, FFmpeg-mvc passend zur FFmpeg-Hauptversion von libmpv.

### Windows (MSYS2 UCRT64 – getestet: GCC 16, Qt 6.11, mpv 0.41, libbluray 1.5, FFmpeg-mvc 9.0.2)

Alle Teile müssen dieselbe C-Laufzeit (UCRT) nutzen – daher Qt, libmpv und libbluray aus MSYS2.
Die Werkzeuge liegen bewusst in einem **kurzen Pfad** (`C:\lumen-build`), da GCC/FFmpeg-Build
sonst am 260-Zeichen-Limit von Windows scheitern.

```bash
# 1. Pakete (ohne MSYS2-Installation, nur entpacken)
python tools/msys2_fetch.py --repo ucrt64 --dest C:/lumen-build/msys2 mpv qt6-base qt6-declarative qt6-svg qt6-tools gcc nasm pkgconf dav1d libva cmake ninja --skip mingw-w64-ucrt-x86_64-ffmpeg
python tools/msys2_fetch.py --repo msys --dest C:/lumen-build/msys2-tools make diffutils --skip msys2-runtime bash
# 2. FFmpeg-mvc (Git-Bash, ~20 min) -> 3rdparty/ffmpeg-mvc
tools/build_ffmpeg_mvc.sh
# 3. Lumen (Deploy läuft als Post-Build: windeployqt + DLLs, FFmpeg-mvc vor den übrigen)
U=C:/lumen-build/msys2/ucrt64; PATH=$U/bin:$PATH
cmake -S . -B build-ucrt -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$U -DMPV_ROOT=$U -DBLURAY_ROOT=$U -DLUMEN_DEPLOY_SEARCH=$U/bin
cmake --build build-ucrt
```

### Linux

```bash
sudo apt install qt6-declarative-dev qml6-module-qtquick-dialogs qml6-module-qtcore libbluray-dev nasm libdav1d-dev
tools/build_ffmpeg_mvc.sh            # FFMPEG_MVC_BRANCH=release/8.1 bei FFmpeg 8.x
```
libmpv muss gegen **dieselbe FFmpeg-Hauptversion** gebaut sein (`ldd $(which mpv) | grep avcodec`).
Passt die Distribution nicht, mpv gegen `3rdparty/ffmpeg-mvc` bauen (`meson setup -Dlibmpv=true`,
`PKG_CONFIG_PATH=3rdparty/ffmpeg-mvc/lib/pkgconfig`). Zur Laufzeit `LD_LIBRARY_PATH=3rdparty/ffmpeg-mvc/lib`.

### macOS

`brew install qt mpv libbluray nasm dav1d`, FFmpeg-mvc wie unter Linux (Branch passend zu Homebrews FFmpeg),
`DYLD_LIBRARY_PATH` auf `3rdparty/ffmpeg-mvc/lib`. Das Player-Fenster ist dort automatisch eingebettet.

### Ohne 3D

Jedes libmpv funktioniert (z. B. shinchiro-SDK mit `-DMPV_ROOT=…`); Blu-ray 3D läuft dann in 2D,
die Oberfläche weist darauf hin („Kein MVC-Decoder“).

## Tests

```bash
# Synthetische 3D-Disc aus einem MVC-Teststrom (FFmpeg-mvc-Fixture: linkes Auge Luma 165, rechtes 36)
perl <ffmpeg-mvc>/tests/fate/h264-mvc/mvc-mkfix.pl --out=mvc8.h264 --base=100 --dep=-100 --base-frames=8 --dep-frames=8
python tools/make_test_bd3d.py mvc8.h264 bd3d
# Zusammenführung prüfen (cmake -DLUMEN_BUILD_TESTS=ON)
mvcmerge_test bd3d 0 merged.m2ts
ffmpeg -view_ids -1 -i merged.m2ts -fps_mode passthrough -f rawvideo -pix_fmt gray - | od -An -tu1 -w512 -v | awk '{print $1","$17}' | sort | uniq -c
#   -> 8 165,36  (alle Bilder: links Basisansicht, rechts abhängige Ansicht)
# In Lumen mit 3D-Profil:  lumen bd3d
```

## Kommandozeile

```bash
lumen D:\                      # Laufwerk: Hauptfilm direkt (mit 3D-Profil in 3D)
lumen --menu D:\               # Laufwerk mit Disc-Menü (z. B. für HTPC-Starter)
lumen --menu Film.iso          # ISO / BDMV-Ordner mit Menü
lumen film.mkv                 # jede Datei, die mpv abspielt
```

## Tastatur

| Taste | Steuerfenster | Player-Fenster |
|---|---|---|
| Leertaste | Play/Pause | Play/Pause |
| ← / → (Shift) | ±10 s (±60 s), im Menü: Auswahl | ±10 s, im Menü: Auswahl |
| ↑ / ↓ | Lautstärke, im Menü: Auswahl | ±60 s, im Menü: Auswahl |
| Enter / Klick | im Menü: bestätigen | im Menü: bestätigen, sonst Enter = Vollbild |
| Pos1 / Ende | Hauptmenü / Pop-up-Menü | Hauptmenü / Pop-up-Menü |
| Bild auf / ab | Kapitel vor / zurück | Kapitel vor / zurück |
| , / . | Einzelbild | Einzelbild |
| F / Doppelklick | Vollbild | Vollbild (Enter / Doppelklick) |
| L | A-B-Schleife | A-B-Schleife |
| S | Screenshot | Screenshot |
| I | Statistik-Overlay | Statistik-Overlay |
| [ / ] / ⌫ | Geschwindigkeit ∓ / zurücksetzen | |
| Strg+O / Strg+E | Öffnen / Auswerfen | |

## Disc-Menüs

- libbluray führt das Menüprogramm der Disc aus und liefert den Strom über `lumenbd://` an mpv;
  Menügrafiken (IG bzw. BD-J) kommen als ARGB-Overlay, skaliert auf den Videobereich (im 3D-Modus je Auge).
- Bedienung: Pfeile/Enter/Maus im Player-Fenster, Steuerkreuz im Steuerfenster, Pos1 = Hauptmenü, Ende = Pop-up.
- Audio-/Untertitelwahl im Disc-Menü wird über die Stream-PID auf die passende Spur übertragen.
- **BD-J-Menüs** benötigen eine Java-Laufzeit (JRE ≥ 8) und `libbluray-j2se-*.jar`; fehlt beides, bleibt der Titelmodus.

## Einschränkungen / Stand

| Thema | Stand |
|---|---|
| Blu-ray 3D | Umgesetzt und mit synthetischer 3D-Disc Ende-zu-Ende getestet (Merge, Seek, SBS, Frame Packing). **Mit echter 3D-Disc noch nicht getestet**; ebenso 3D-Untertitel/-Menüs (Testdisc ohne PG/IG). FFmpeg-mvc ist ein experimenteller Fork. |
| Frame Packing | Benötigt einen 1920×2205-Modus im Grafiktreiber; ob der Beamer ihn ohne HDMI-3D-InfoFrame als 3D erkennt, ist geräteabhängig. |
| Dolby Vision | Wird erkannt (Profil 5/7/8), gpu-next wendet die RPU-Metadaten an; ein echtes DV-Signal über HDMI kann kein PC-Player erzeugen. |
| Menü-Standbilder | Reine Standbild-Menüs können kurz schwarz bleiben (Decoder-Verzögerung). |
| Refresh/HDR-Umschaltung | Windows: Refresh + HDR · Linux X11: Refresh (xrandr) · KDE Plasma: Refresh + HDR · GNOME Wayland: nur Anzeige · macOS: Refresh |
| Eingebettetes Player-Fenster | Automatisch auf macOS, sonst per Profil; OpenGL-Render-API → nur SDR. |

Entwickler-Hilfe: `LUMEN_SNAPSHOT=shot.png` (optional `LUMEN_SNAPSHOT_DELAY=ms`) speichert das Steuerfenster als Bild.
