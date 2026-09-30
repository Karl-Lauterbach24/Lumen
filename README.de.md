<p align="center"><img src="resources/logo/lumen-logo.png" width="300" alt="Lumen – Disc · Cinema Player"></p>

# Lumen – Disc- & Digitalkino-Player

[English](README.md) · **Deutsch** · [Français](README.fr.md) · [Español](README.es.md) · [Italiano](README.it.md) · [Português](README.pt.md)

Schneller, minimalistischer Player für Heimkinos, Vorführräume und kleine Kinos mit **zwei Fenstern**:

- **Player-Fenster** – natives mpv-Fenster (gpu-next, D3D11/Vulkan/Wayland), fest auf ein Ausgabegerät legbar, HDR-Passthrough.
- **Steuerfenster** – Qt Quick: Quelle, Transport, Titel, Kapitel, Ton, Untertitel, Bild, Kino, Ausgabeprofile.

Spielt **Blu-ray / UHD / Blu-ray 3D, DVD-Video (mit Menüs), HD DVD, Video-CD / Super Video-CD, Audio-CD,
Digital Cinema Packages (DCP, JPEG 2000, SMPTE und Interop, verschlüsselt mit KDM)** und alle Dateiformate, die mpv abspielt.

## Kopierschutz / Verschlüsselung

Lumen **umgeht keinen Kopierschutz**. Alles in dieser Richtung ist Entscheidung des Nutzers und läuft über
**[Plugins](plugins/README.md)**, die er selbst installiert und aktiviert: Ein Plugin kann vom Nutzer beschaffte
Bibliotheken (libaacs, libbdplus, libdvdcss) für libbluray/libdvdread bereitstellen, eigene entschlüsselnde
URL-Schemata anmelden oder DCP-Schlüssel liefern. Lumen liefert nichts davon mit und lädt nichts davon herunter.

- **Blu-ray:** Discs werden ausschließlich über `libbluray` gelesen. Ist eine Disc AACS/BD+-geschützt, muss das außerhalb
  von Lumen bereits gelöst sein – z. B. ein Laufwerk mit LibreDrive-Firmware plus eine vom Nutzer installierte
  AACS-Bibliothek, die libbluray zur Laufzeit selbst lädt. Das gilt auch für die zweite 3D-Ansicht (`bd_open_file_dec`).
- **DVD:** Zugriff über `libdvdread`/`libdvdnav`; Lumen enthält keinen CSS-Code. Die MSYS2- und Homebrew-Builds von libdvdread
  sind fest gegen `libdvdcss` gelinkt. In Lumens Windows- und macOS-Paketen liegt deshalb unter diesem Namen **Lumens
  eigener Ersatz** (`src/dvdcss_shim.c`) statt libdvdcss. Er liest Sektoren unverändert, damit laufen unverschlüsselte Discs,
  Abbilder und Ordner. Stellt der Nutzer per Plugin eine echte libdvdcss bereit, reicht der Ersatz an sie durch
  (`LUMEN_DVDCSS_LIBRARY`).
- **HD DVD:** nur ungeschützte (bzw. bereits entschlüsselte) Discs; AACS-geschützte HD DVDs werden als solche gemeldet.
- **DCP:** verschlüsselte DCPs werden entschlüsselt wie auf jedem Kinoserver – mit einem **für das Zertifikat dieses Players
  ausgestellten KDM** (siehe unten). Ohne passenden, gültigen KDM (oder vom Rechteinhaber selbst eingetragene Schlüssel)
  bleibt verschlüsselter Inhalt unlesbar.

## Plugins

Ordner mit einer `plugin.json`, verwaltet im Reiter **Plugins** (aktivieren, deaktivieren, Schaltflächen, Status).
Ein Plugin kann Folgendes mitbringen, beliebig kombiniert:
- eine native C-ABI-Bibliothek ([`include/lumen/plugin.h`](include/lumen/plugin.h)): Ereignisse,
  Schaltflächen, eigene URL-Schemata als Quellen, DCP-Inhaltsschlüssel, mpv-Befehle/-Eigenschaften;
- mpv-Skripte (Lua/JavaScript);
- mpv-Optionen;
- Umgebungsvariablen;
- vom Nutzer bereitgestellte Disc-Bibliotheken.

Doku und Beispiele: [plugins/README.md](plugins/README.md) (englisch).

## Funktionen

| Bereich | Umfang |
|---|---|
| Quellen | Optische Laufwerke (Auto-Erkennung, Hersteller/Modell/Firmware, Auswerfen, Autostart beim Einlegen), ISO (Blu-ray/DVD/HD DVD automatisch erkannt), CUE/BIN/NRG-Abbilder, Disc-Ordner (BDMV, VIDEO_TS, HVDVD_TS, MPEGAV/MPEG2), DCP-Ordner und Kino-Festplatten, alle Formate, die mpv abspielt |
| Blu-ray | Disc-Menüs (HDMV, BD-J mit Java), Hauptfilm, Titel/Playlists mit Laufzeit, Video-/Tonformat, UHD- und 3D-Erkennung |
| **Blu-ray 3D** | **Beide Ansichten (MVC)**, Ausgabe als HDMI Frame Packing 1080p, Side-by-Side/Top-and-Bottom (Half/Full), Zeilenverschachtelt, Anaglyph oder 2D; Untertitel und Menüs je Auge mit einstellbarer Tiefe |
| **DVD-Video** | **Disc-Menüs** über libdvdnav (Haupt-/Titel-/Ton-/Untertitelmenü, Buttons per Tastatur, Fernbedienung und Maus), Standbilder, eigener Subpicture-Dekoder mit Disc-Palette und Button-Hervorhebung, erzwungene Untertitel, Mehrfachwinkel, Sprachen aus der IFO, Region/Sprache aus den Systemeinstellungen, Titel- und Kapitelwahl |
| **DCP** | SMPTE und Interop, OV/VF (Ergänzungspakete in Nachbarordnern), mehrrollige CPLs mit Einstiegspunkten, **JPEG 2000 (XYZ → Anzeigefarbraum)**, 24-Bit-PCM bis 16 Kanäle, **verschlüsselte DCPs (KDM, AES-128)**, Untertitel (Interop-XML und SMPTE Timed Text inkl. verschlüsseltem MXF und eingebetteten Schriften → positioniertes ASS), **CPL-Marker als Kapitel** (FFOC, LFOC, FFEC, FFMC …), **3D-DCPs**, Prüfsummen-Kontrolle gegen die PKL |
| HD DVD | Titel und Kapitel aus den Advanced-Content-Playlists (ADV_OBJ/*.XPL), EVO-Wiedergabe (VC-1/AVC/MPEG-2, DD+, DTS-HD, TrueHD) |
| Video-CD / SVCD | Sektorgenaues Lesen der Mode-2-Form-2-Tracks über libcdio (Laufwerk oder CUE/BIN/NRG), Einsprungpunkte (ENTRIES.VCD/SVD) als Kapitel, **PBC-Menüs** (VCD-2.0-Wiedergabesteuerung: Auswahllisten per Zifferntaste, Wiedergabelisten, Weiter/Zurück/Return/Default, Wartezeiten, Schleifen, Segment-Standbilder), Rückfall über das Dateisystem |
| Audio-CD | Tracks als Kapitel (mpv cdda) |
| Transport | Play/Pause, Stopp, ±10 s/±60 s, Kapitel, Einzelbild vor/zurück, A-B-Schleife, Geschwindigkeit, Scrubbing mit Kapitelmarken, Screenshot, **Vorführprogramm** (Werbung, Trailer, Hauptfilm nacheinander) |
| Ton | Spurwahl, Bitstream (TrueHD/Atmos, DTS-HD MA/DTS:X, DD+, DD), WASAPI exklusiv, Kanal-Layout, Audio-Verzögerung; **Kino-Fader** (Dolby-Skala, 7,0 = Referenz) und **DCP-Kanalbelegung** (5.1, 7.1 DS, HI, VI-N) |
| Untertitel | Spurwahl (PGS/SRT/ASS/VobSub/DCP), nur erzwungene, Verzögerung, Größe, Position (Cinemascope-Leinwand) |
| Bild | Seitenverhältnis, Pan & Scan, Zoom, Helligkeit/Kontrast/Sättigung/Gamma, Deinterlacing, 3D-Quellformat für Dateien |
| Ausgabeprofile | Zielgerät, Vollbild, Bildraten-Anpassung, System-HDR automatisch, HDR-Passthrough oder Tonemapping, **Referenz-Skalierung** (EWA Lanczos 4, Error Diffusion, HDR-Kontrastrückgewinnung), **Kalibrierung** (System-/eigenes ICC-Profil, 3D-LUT `.cube`, nativer Kontrast, Dither-Tiefe, GLSL-Shader), Sync, 3D-Ausgabeformat, Audiogerät, Experten-Optionen |

Vorlagen: *Desktop*, *1080p DLP 3D-Beamer* (Half-SBS), *1080p DLP 3D-Beamer (Frame Packing)*,
**Kino-Projektor DCI-P3 (Gamma 2.6, 48 cd/m²)**, **Mastering-Monitor P3-D65**,
*4K HDR LED-Projektor (Passthrough)*, *4K HDR LED-Projektor (Player-Tonemapping)*, *4K HDR TV (OLED)*, *Leistung/Laptop*.

## Digital Cinema Packages

```
ASSETMAP ─> PKL ─> CPL (Rollen: Bild | Ton | Untertitel | Marker)
                     │
                     ├─ Bild-MXF    ─ lumendcp:// (entschlüsselt beim Lesen) ─ FFmpeg J2K ─ XYZ → Anzeige
                     ├─ Ton-MXF     ─ lumendcp://                            ─ PCM 24 Bit ─ Fader / Kanalbelegung
                     ├─ Untertitel  ─ Interop-XML / SMPTE Timed Text          ─ positioniertes ASS (+ Schriften)
                     └─ Marker      ─ Kapitel
             alle Rollen ─> eine mpv-EDL (3D: linkes + rechtes Auge → lavfi hstack → 3D-Ausgabeformat)
```

- **Öffnen:** Tab „Kino“ → *DCP öffnen …*, der Ordner-Dialog, DCP-Ordner auf der Kommandozeile, oder eine Kino-Festplatte
  anschließen: DCPs im Wurzelordner oder eine Ordnerebene tiefer erscheinen in der Laufwerksliste.
- **Verschlüsselte DCPs / KDM-Ablauf**
  1. *Kino → Zertifikat erzeugen* legt eine Zertifikatskette nach SMPTE ST 430-2 an (RSA 2048, SHA-256).
     Alternativ übernimmt *Importieren …* ein vorhandenes Leaf-Zertifikat samt privatem Schlüssel (PEM), z. B. aus DCP-o-matic.
  2. *Leaf exportieren* – `lumen-leaf.pem` an den Verleih bzw. KDM-Ersteller senden.
  3. *KDM laden …* – die Inhaltsschlüssel werden per RSA-OAEP ausgepackt; KDMs werden (weiter verschlüsselt) im
     Konfigurationsordner abgelegt und bei jedem Start neu ausgepackt. Gültigkeitszeiträume werden geprüft; die CPL-Liste
     zeigt „KDM gültig bis …“, „abgelaufen“ oder „noch nicht gültig“.
  4. Bei der Wiedergabe wird jedes KLV-Triplet (SMPTE ST 429-6, AES-128-CBC) im Lesestrom entschlüsselt und durch
     *Essenz-KLV + gleich großes KLV-Fill* ersetzt – alle Index-Tabellen und Partitions-Offsets bleiben gültig, Spulen
     funktioniert. Ein falscher Schlüssel fällt über den Prüfwert des Triplets auf.
  - Für eigene DCPs nimmt *Schlüsseldatei …* Zeilen `<Key-ID> <Schlüssel-Hex>` an (nur für die Sitzung, nicht gespeichert).
- **JPEG-2000-Leistung:** J2K ist in Auflösungsstufen kodiert. Im Modus *automatisch* dekodiert Lumen nur die Stufe, die die
  Ausgabe braucht (4K-DCP auf 2K/1080p = halbe Arbeit ohne sichtbaren Verlust) und wechselt, wenn die CPU nicht mithält
  (verworfene Bilder in den ersten 30 s), im laufenden Betrieb eine Stufe tiefer. Von Hand: voll / halb / Viertel.
- **Ton:** 16-Kanal-DCPs werden nach SMPTE 428-12 zugeordnet (L R C LFE Ls Rs · HI · VI-N · … · Lrs Rrs); *automatisch* nutzt
  7.1 DS bei 16 Kanälen, sonst 5.1; HI und VI-N sind für Barrierefreiheit wählbar. Der Fader folgt der Skala der
  Kinoprozessoren (7,0 = 0 dB, 3,33 dB je Einheit oberhalb 4).
- **Immersive Audio (Dolby Atmos / SMPTE IAB):** Atmos-Spuren in DCPs (`AuxData`, ST 429-18) enthalten IAB-Frames nach
  SMPTE ST 2098-2. Lumen liest sie Frame für Frame, entschlüsselt sie mit dem KDM-Schlüssel (Typ MDEK) und rendert Betten
  und Objekte mit dem offenen [IAB-Renderer](https://github.com/DTSProAudio/iab-renderer) von DTS (BSD-3-Clause, VBAP) auf
  **7.1.4, 5.1.4, 7.1, 5.1 oder Stereo** (*Kino → Atmos/IAB-Ausgabe*). Das Ergebnis geht als 32-Bit-Float-WAV mit
  Kanalmaske an mpv und ist die bevorzugte Tonspur der Komposition; die PCM-Fassung bleibt wählbar.
  - Die 5.1/7.1-Layouts sind aus DTS' 5.1.4/7.1.4 abgeleitet, die Höhen werden dabei heruntergemischt (`resources/iab`).
  - Der Renderer wird beim Bauen automatisch geholt (festgelegter Commit) oder aus `-DIAB_SOURCE_DIR` genommen;
    `-DLUMEN_WITH_IAB=OFF` baut ohne ihn.
- **Prüfung:** *Prüfen* bildet die SHA-1-Summen aller Spurdateien einer CPL und vergleicht sie mit der Packing List.
- **Vorführprogramm:** CPLs (*Ins Programm*) oder die laufende Quelle hinzufügen und nacheinander abspielen.

## Blu-ray 3D

```
libbluray bd_read_ext()  ── Basisansicht (PID 0x1011) ─────┐
                                                            ├─ MvcMerger ─ TS ─> mpv ─> FFmpeg-mvc ─> SBS-Bild
libbluray bd_open_file_dec() ─ abhängige Ansicht (0x1012) ─┘   (je Bild per PTS gepaart)            │
                                                                                                     v
                                          vf: stereo3d / Frame-Packing-Graph ─> Format des Ausgabeprofils
```

- **Decoder:** Standard-FFmpeg dekodiert nur die Basisansicht. Lumen nutzt
  [FFmpeg-mvc](https://github.com/tthayer93/FFmpeg-mvc) (Branch `release/9.0`), das beide Ansichten zu einem
  Side-by-Side-Bild dekodiert. Dessen DLLs haben dieselben Namen/ABI wie FFmpeg 9.0 und ersetzen libmpvs
  Bibliotheken eins zu eins. Lumen erkennt den Decoder an der Versionskennung (`…-mvc`).
- **Zweite Ansicht:** libbluray liefert nur die Basisansicht. `MvcMerger` liest den SS-Subpfad der Playlist
  (welcher Clip), die CLPI-EP-Map (Sprungpunkte) und die abhängige `.m2ts` über libbluray, paart Access Units
  per PTS und hängt die MVC-NAL-Units an die der Basisansicht an.
- **3D-Wiedergabe läuft immer über libbluray** (`lumenbd://`), auch „Hauptfilm“ und Titelwahl.
  Der Disc wird „3D bevorzugt“ gemeldet (PSR21/23), damit Menüs die 3D-Playlist wählen.
- **Untertitel/Menüs:** libbluray rendert PG-Untertitel und Menügrafik; Lumen zeichnet sie einmal je Auge
  (Tiefe im Profil bzw. im Tab „Untertitel“ einstellbar).
- **Frame Packing (HDMI 1.4):** 1920×2205 (1080 + 45 + 1080 Zeilen) @ 23.976 Hz. Der Anzeigemodus muss im
  Grafiktreiber als benutzerdefinierte Auflösung angelegt sein; Lumen schaltet dann automatisch darauf um.
- MVC kann keine GPU dekodieren; erkannte 3D-Discs werden in Software dekodiert.

## DVD-Menüs

- libdvdnav führt die virtuelle Maschine der DVD aus und liefert MPEG-PS über `lumendvd://` an mpv.
- mpv kennt die DVD-Palette nicht; Lumen ersetzt die Subpicture-Pakete im Strom durch Füllpakete und dekodiert sie selbst
  (Lauflängen, Steuersequenzen, IFO-Palette, Button-Farben) – Menüs und Untertitel werden als Overlay auf die Bildfläche
  skaliert, Untertitel per PTS zur Wiedergabezeit synchronisiert.
- Standbild-Menüs: Lumen fügt ein Sequenzende ein, damit der Decoder das Standbild sofort ausgibt.
- Bedienung: Pfeile/Enter/Maus im Player-Fenster, Steuerkreuz im Steuerfenster, Pos1 = Hauptmenü, Ende = Titelmenü,
  Tasten für Ton-/Untertitelmenü, „Zurück“ und Winkelwechsel.

## Architektur

```
src/MpvController   libmpv-Instanz, ereignisgesteuerte Property-Beobachtung, Profile -> mpv-Optionen, Quellenwahl, Vorführprogramm
src/BlurayNav       libbluray als mpv-Stream "lumenbd://": Menüs, Titel, 3D, Overlays je Auge
src/MvcMerger       Blu-ray 3D: mischt die abhängige Ansicht zu (SS-Subpfad, EP-Map, PTS-Paarung)
src/DvdNav          libdvdnav als mpv-Stream "lumendvd://": Menüs, SPU-Dekoder, Hervorhebung, Spuren, Winkel
src/OpticalMedia    Video-CD/SVCD ("lumenvcd://", libcdio), Audio-CD, HD DVD (XPL-Playlists, EVO)
src/DcpPackage      DCP: ASSETMAP/PKL/CPL-Parser, MXF-Kopf (Auflösung, Kanäle, 3D, Verschlüsselung)
src/DcpCrypto       OpenSSL: Zertifikatskette (SMPTE 430-2), KDM auspacken (RSA-OAEP), AES-128-CBC
src/DcpStream       "lumendcp://": MXF-Leser mit größengleicher Triplet-Entschlüsselung und Augenfilter
src/DcpSubtitles    Interop/SMPTE-Untertitel -> ASS, eingebettete Schriften
src/DcpManager      DCP-Ablauf für die Oberfläche: Pakete, Schlüssel, EDL/Kapitel, Fader, Kanäle, J2K-Stufen, Prüfung
src/DisplayManager  Ausgabegeräte, Bildrate / HDR / Frame-Packing-Modus (Windows, xrandr, kscreen-doctor, CoreGraphics)
src/DriveManager    Laufwerke/Discs/Kino-Festplatten (Worker-Thread), Auswerfen
src/DiscScanner     Titel und Status für Blu-ray, DVD, HD DVD, VCD, Audio-CD (Worker-Thread)
src/PlayerWindow    eingebettetes Player-Fenster (mpv-Render-API/OpenGL), vor allem für macOS
src/ProfileManager  Vorlagen + eigene Profile
qml/                Steuerfenster (CinemaPane.qml = Tab „Kino“)
tools/              Build-/Deploy-Skripte, Generatoren für Test-Discs/-DCPs/-VCDs
tests/              mvcmerge_test (MVC-Zusammenführung), dcp_test (DCP-Kette)
```

## Bauen

Voraussetzungen: CMake ≥ 3.21, Qt ≥ 6.5 (Quick, QuickControls2, OpenGL, Xml), libmpv ≥ 0.38 **gegen gemeinsame
FFmpeg-Bibliotheken gebaut**, libbluray ≥ 1.2, FFmpeg-mvc passend zur FFmpeg-Hauptversion von libmpv.
Optional: **OpenSSL ≥ 1.1** (verschlüsselte DCPs), **libdvdnav ≥ 6** (DVD-Menüs), **libcdio + libiso9660** (Video-CD von
Laufwerk und Abbild) – jeweils automatisch aktiv, wenn gefunden (`-DLUMEN_WITH_OPENSSL/DVDNAV/CDIO=OFF` schaltet ab).

### Windows (MSYS2 UCRT64 – getestet: GCC 16, Qt 6.11, mpv 0.41, libbluray 1.5, libdvdnav 7, libcdio 2.4, OpenSSL 3.6, FFmpeg-mvc 9.0.2)

Alle Teile müssen dieselbe C-Laufzeit (UCRT) nutzen – daher kommen Qt, libmpv und libbluray aus MSYS2.
Die Werkzeuge liegen bewusst in einem **kurzen Pfad** (`C:\lumen-build`), weil GCC und der FFmpeg-Build
sonst an der 260-Zeichen-Grenze von Windows scheitern.

```bash
# 1. Pakete (keine MSYS2-Installation nötig, nur Entpacken)
python tools/msys2_fetch.py --repo ucrt64 --dest C:/lumen-build/msys2 mpv qt6-base qt6-declarative qt6-svg qt6-tools gcc nasm pkgconf dav1d libva cmake ninja openssl libdvdnav libcdio --skip mingw-w64-ucrt-x86_64-ffmpeg
python tools/msys2_fetch.py --repo msys --dest C:/lumen-build/msys2-tools make diffutils --skip msys2-runtime bash
# 2. FFmpeg-mvc (Git Bash, ~20 min) -> 3rdparty/ffmpeg-mvc
tools/build_ffmpeg_mvc.sh
# 3. Lumen (Deployment läuft als Post-Build-Schritt: windeployqt + DLLs, FFmpeg-mvc vor allem anderen)
U=C:/lumen-build/msys2/ucrt64; PATH=$U/bin:$PATH
cmake -S . -B build-ucrt -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$U -DMPV_ROOT=$U -DBLURAY_ROOT=$U -DLUMEN_DEPLOY_SEARCH=$U/bin
cmake --build build-ucrt
```

### Linux

```bash
sudo apt install qt6-declarative-dev qml6-module-qtquick-dialogs qml6-module-qtcore libbluray-dev libdvdnav-dev libcdio-dev libiso9660-dev libssl-dev nasm libdav1d-dev
tools/build_ffmpeg_mvc.sh            # FFMPEG_MVC_BRANCH=release/8.1 für FFmpeg 8.x
```
libmpv muss gegen **dieselbe FFmpeg-Hauptversion** gebaut sein (`ldd $(which mpv) | grep avcodec`).
Passt die Distribution nicht, mpv gegen `3rdparty/ffmpeg-mvc` bauen (`meson setup -Dlibmpv=true`,
`PKG_CONFIG_PATH=3rdparty/ffmpeg-mvc/lib/pkgconfig`). Zur Laufzeit `LD_LIBRARY_PATH=3rdparty/ffmpeg-mvc/lib`.

### macOS

```bash
brew install qt mpv libbluray libdvdnav libcdio openssl@3 libxml2 pkgconf ninja
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$(brew --prefix qt)
cmake --build build                      # build/Lumen.app
cmake --build build --target lumen_dmg   # eigenständiges Lumen.app + Lumen.dmg (macdeployqt)
```
OpenSSL und libxml2 (bei Homebrew „keg-only“) werden automatisch gefunden. libmpv kann unter macOS kein eigenes
Fenster öffnen, deshalb ist das Player-Fenster dort immer das eingebettete (mpv-Render-API, OpenGL 3.2 Core, SDR);
die Bildraten-Umschaltung läuft über CoreGraphics, HDR steuert macOS selbst. Der GitHub-Actions-Workflow
[`macos.yml`](.github/workflows/macos.yml) baut unter macOS 15 (Apple Silicon), führt die DCP- und DVD-Tests sowie
eine Bildprüfung des Player-Fensters aus und stellt `Lumen.dmg` bereit.
Blu-ray 3D: FFmpeg-mvc wie unter Linux bauen (`brew install nasm dav1d`, Branch passend zu Homebrews FFmpeg) und
Lumen mit `DYLD_LIBRARY_PATH=3rdparty/ffmpeg-mvc/lib` starten.

### Ohne 3D

Jedes libmpv funktioniert (z. B. das shinchiro-SDK mit `-DMPV_ROOT=…`); Blu-ray 3D läuft dann in 2D und die
Oberfläche zeigt einen Hinweis („Kein MVC-Decoder“). 3D-DCPs brauchen kein FFmpeg-mvc.

## Tests

```bash
cmake -DLUMEN_BUILD_TESTS=ON …        # baut mvcmerge_test und dcp_test

# Blu-ray 3D: synthetische 3D-Disc aus einem MVC-Teststrom (FFmpeg-mvc-Fixture: linkes Auge Luma 165, rechtes 36)
perl <ffmpeg-mvc>/tests/fate/h264-mvc/mvc-mkfix.pl --out=mvc8.h264 --base=100 --dep=-100 --base-frames=8 --dep-frames=8
python tools/make_test_bd3d.py mvc8.h264 bd3d
mvcmerge_test bd3d 0 merged.m2ts
ffmpeg -view_ids -1 -i merged.m2ts -fps_mode passthrough -f rawvideo -pix_fmt gray - | od -An -tu1 -w512 -v | awk '{print $1","$17}' | sort | uniq -c
#   -> 8 165,36  (jedes Bild: Basisansicht links, abhängige Ansicht rechts)

# DCP: Zertifikat -> verschlüsseltes Test-DCP + KDM -> auspacken -> entschlüsseln -> abspielen (libmpv, vo=null)
dcp_test gencert id                                   # id/leaf.pem, id/leaf.key
python tools/make_test_dcp.py ffmpeg openssl dcp --encrypt id/leaf.pem
dcp_test info dcp                                     # Rollen, Einstiegspunkte, Schlüssel-IDs, MXF-Kopf
dcp_test kdm dcp/kdm.xml id/leaf.key                  # == dcp/keys.txt
dcp_test decrypt dcp/picture.mxf <schlüssel> plain.mxf # Bilder identisch zur unverschlüsselten Quelle; falscher Schlüssel -> Prüfwertfehler
dcp_test play dcp dcp/kdm.xml id/leaf.key 1.5         # loaded=1 … keyErrors=0
dcp_test subs dcp                                     # erzeugtes ASS

# Video-CD: CUE/BIN-Abbild mit Mode-2-Form-2-Track
python tools/make_test_vcd.py ffmpeg vcd && lumen vcd/vcd.cue
```

Entwickler-Hilfen: `LUMEN_SNAPSHOT=shot.png` (optional `LUMEN_SNAPSHOT_DELAY=ms`) speichert das Steuerfenster als Bild;
`LUMEN_MPV_LOG=warn|info|v` gibt mpvs Log aus (unter Windows zusammen mit `QT_FORCE_STDERR_LOGGING=1`).

## Kommandozeile

```bash
lumen D:\                      # Laufwerk: erkennt Blu-ray / DVD / HD DVD / VCD / Audio-CD und spielt den Hauptfilm
lumen --menu D:\               # mit Disc-Menü (Blu-ray, DVD; z. B. für HTPC-Launcher)
lumen --menu Film.iso          # ISO (Blu-ray, DVD oder HD DVD) / Disc-Ordner mit Menü
lumen VideoCD.cue              # Video-CD-/SVCD-Abbild
lumen E:\DCP\Film_FTR          # DCP-Ordner: spielt die erste (Spielfilm-)CPL
lumen --kdm film.xml E:\DCP\Film_FTR   # vorher einen KDM laden
lumen film.mkv                 # jede Datei, die mpv abspielt
```

## Tastatur

| Taste | Steuerfenster | Player-Fenster |
|---|---|---|
| Leertaste | Wiedergabe/Pause | Wiedergabe/Pause |
| ← / → (Umschalt) | ±10 s (±60 s), im Menü: navigieren | ±10 s, im Menü: navigieren |
| ↑ / ↓ | Lautstärke, im Menü: navigieren | ±60 s, im Menü: navigieren |
| Enter / Klick | Im Menü: bestätigen | Im Menü: bestätigen, sonst Enter = Vollbild |
| Pos1 / Ende | Hauptmenü / Pop-up-Menü (DVD: Titelmenü) | Hauptmenü / Pop-up-Menü (DVD: Titelmenü) |
| Bild auf / ab | Nächstes / vorheriges Kapitel | Nächstes / vorheriges Kapitel |
| , / . | Einzelbild | Einzelbild |
| F / Doppelklick | Vollbild | Vollbild (Enter / Doppelklick) |
| L | A-B-Schleife | A-B-Schleife |
| S | Screenshot | Screenshot |
| I | Statistik im Bild | Statistik im Bild |
| [ / ] / ⌫ | Geschwindigkeit ∓ / zurücksetzen | |
| Strg+O / Strg+E / Strg+D | Öffnen / Auswerfen / Tab „Kino“ | |

## Blu-ray-Disc-Menüs

- libbluray führt das Menüprogramm der Disc aus und liefert den Strom über `lumenbd://` an mpv;
  Menügrafik (IG oder BD-J) kommt als ARGB-Overlay, auf die Bildfläche skaliert (im 3D-Modus je Auge).
- Im Disc-Menü gewählte Ton-/Untertitelspuren werden über die Stream-PID der passenden Spur zugeordnet.
- **BD-J-Menüs** brauchen eine Java-Laufzeit (JRE ≥ 8) und `libbluray-j2se-*.jar`; ohne bleibt der Titelmodus.

## Einschränkungen / Stand

| Thema | Stand |
|---|---|
| Blu-ray 3D | Implementiert und mit einer synthetischen 3D-Disc durchgehend getestet (Zusammenführung, Spulen, SBS, Frame Packing). **Noch nicht mit einer echten 3D-Disc getestet**. FFmpeg-mvc ist ein experimenteller Fork. |
| DCP | Mit synthetischen SMPTE-DCPs unter Windows und macOS (CI) durchgehend getestet. Die Test-DCPs haben 2 Rollen mit Einstiegspunkten, Marker, Interop-Text- und Bilduntertitel, Closed Captions, eine signierte KDM, 3D und eine Atmos/IAB-Spur. Die gesamte Essenz ist verschlüsselt, entschlüsselte Bilder sind bitgleich zur Quelle. Die Objekt- und Bettpositionen der IAB-Spur werden je Lautsprecher und Layout geprüft. **Noch nicht mit echten Kino-DCPs/KDMs oder echten Atmos-Mischungen getestet.** Nicht unterstützt: forensische Markierung. Atmos-Spuren aus der Zeit vor dem SMPTE-Standard können Elemente enthalten, die der IAB-Renderer ablehnt (ungetestet). |
| JPEG 2000 | Software-Dekodierung (FFmpeg, Frame- und Slice-Threads). 2K mit 24 fps braucht eine starke Mehrkern-CPU (≈ 24 fps auf 12 Threads bei 130 Mbit/s); automatischer Wechsel der Auflösungsstufe bei verworfenen Bildern. |
| DVD | Menüführung, SPU-Dekodierung und Hervorhebung sind implementiert, aber **noch nicht mit echten DVDs getestet** (in der Build-Umgebung gab es kein DVD-Authoring-Werkzeug). Ohne libdvdnav laufen DVDs über mpv `dvd://` (ohne Menüs). |
| HD DVD | Mit einer synthetischen HVDVD_TS/XPL-Struktur getestet. HDi-Interaktivität (Menüs) wird nicht unterstützt; Titel/Kapitel kommen aus der Playlist. AACS-geschützte Discs spielen nicht. |
| Video-CD | Die Wiedergabesteuerung (PBC) ist mit einer VCD 2.0 getestet, erstellt mit `vcdxbuild` (GNU VCDImager), unter Windows und macOS. Geprüft werden Auswahl per Ziffer, Default, Weiter/Zurück/Return, automatisches Weiterschalten, Einsprungpunkte und Segment-Menüs. Nicht unterstützt: erweiterte SVCD-Auswahlflächen (Mausbereiche) und Befehlslisten. **Noch nicht mit einer echten gepressten VCD getestet.** |
| Frame Packing | Erfordert einen 1920×2205-Modus im Grafiktreiber; ob der Projektor ihn ohne HDMI-3D-InfoFrame als 3D erkennt, hängt vom Gerät ab. |
| Dolby Vision | Erkannt (Profil 5/7/8), gpu-next wendet die RPU-Metadaten an; ein echtes DV-Signal über HDMI kann kein PC-Player ausgeben. |
| Bildrate/HDR-Umschaltung | Windows: Bildrate + HDR · Linux X11: Bildrate (xrandr) · KDE Plasma: Bildrate + HDR · GNOME Wayland: nur Anzeige · macOS: Bildrate |
| Eingebettetes Player-Fenster | Automatisch unter macOS, sonst per Profil; OpenGL-Render-API → nur SDR. |

## Lizenz

Lumen ist freie Software unter der **GNU Affero General Public License v3.0 oder später** ([LICENSE](LICENSE)). Verwendete Komponenten und ihre Lizenzen: [THIRD_PARTY.md](THIRD_PARTY.md).
