# Lumen – Lettore Blu-ray

[English](README.md) · [Deutsch](README.de.md) · [Français](README.fr.md) · [Español](README.es.md) · **Italiano** · [Português](README.pt.md)

> **Novità:** Lumen riproduce ora anche **DVD-Video (con menu), HD DVD, Video-CD/SVCD (incl. immagini CUE/BIN), CD audio** e **Digital Cinema Packages (DCP, JPEG 2000, SMPTE/Interop, cifrati con KDM)**, con la scheda «Kino» (certificato del lettore, KDM, fader cinema, instradamento canali, programma di proiezione) e profili di calibrazione (ICC, LUT 3D, qualità di riferimento). Dettagli: [README in inglese](README.md).

Lettore Blu-ray / UHD / 3D veloce e minimalista per l'home cinema, con **due finestre**:

- **Finestra di riproduzione** – finestra nativa di mpv (gpu-next, D3D11/Vulkan/Wayland), assegnabile a un dispositivo di uscita, HDR passthrough.
- **Finestra di controllo** – Qt Quick: sorgente, trasporto, titoli, capitoli, audio, sottotitoli, immagine, profili di uscita.

> L'interfaccia è attualmente in tedesco.

## Protezione dalla copia / LibreDrive

Lumen **non aggira alcuna protezione dalla copia**. I dischi vengono letti esclusivamente tramite `libbluray`.
Se un disco è protetto da AACS/BD+, questo deve essere già gestito al di fuori di Lumen – ad es. un'unità con
firmware LibreDrive più una libreria AACS installata dall'utente, che libbluray carica in fase di esecuzione.
Lo stesso vale per la seconda vista 3D: viene letta tramite libbluray (`bd_open_file_dec`) e quindi tramite la
stessa libreria esterna.

## Funzionalità

| Area | Contenuto |
|---|---|
| Sorgenti | Unità ottiche (rilevamento automatico, produttore/modello/firmware, espulsione, avvio automatico all'inserimento), ISO, cartelle BDMV, ogni formato riproducibile da mpv |
| Disco | Menu del disco (HDMV, BD-J con Java), film principale, titoli/playlist con durata, formato video/audio, rilevamento UHD e 3D |
| **Blu-ray 3D** | **Entrambe le viste (MVC)**, uscita come HDMI Frame Packing 1080p, affiancato / sopra-sotto (half/full), interlacciato per righe, anaglifo o 2D; sottotitoli e menu per occhio con profondità regolabile |
| Trasporto | Play/pausa, stop, ±10 s/±60 s, capitoli, fotogramma per fotogramma, loop A-B, velocità, scorrimento con indicatori di capitolo, screenshot |
| Audio | Scelta traccia, bitstream (TrueHD/Atmos, DTS-HD MA/DTS:X, DD+, DD), WASAPI esclusivo, configurazione canali, ritardo audio |
| Sottotitoli | Scelta traccia (PGS/SRT/ASS), solo forzati, ritardo, dimensione, posizione (schermi cinemascope) |
| Immagine | Rapporto d'aspetto, pan & scan, zoom, luminosità/contrasto/saturazione/gamma, deinterlacciamento, formato sorgente 3D per i file |
| Profili di uscita | Dispositivo di destinazione, schermo intero, adattamento frequenza (23,976 → 23/24 Hz), HDR di sistema automatico, HDR passthrough o tone mapping, qualità di scalatura, sincronizzazione, formato di uscita 3D, dispositivo audio, opzioni avanzate |

Preimpostazioni: *Desktop*, *Proiettore DLP 3D 1080p* (half SBS), *Proiettore DLP 3D 1080p (Frame Packing)*,
*Proiettore LED 4K HDR (passthrough)*, *Proiettore LED 4K HDR (tone mapping del lettore)*, *TV 4K HDR (OLED)*, *Prestazioni/Portatile*.

## Blu-ray 3D

```
libbluray bd_read_ext()  ── vista base (PID 0x1011) ────────┐
                                                             ├─ MvcMerger ─ TS ─> mpv ─> FFmpeg-mvc ─> immagine SBS
libbluray bd_open_file_dec() ─ vista dipendente (0x1012) ───┘   (abbinate per PTS a ogni fotogramma)       │
                                                                                                            v
                                        vf: stereo3d / grafo frame packing ─> formato del profilo di uscita
```

- **Decoder:** FFmpeg standard decodifica solo la vista base. Lumen usa
  [FFmpeg-mvc](https://github.com/tthayer93/FFmpeg-mvc) (branch `release/9.0`), che decodifica entrambe le
  viste in un'unica immagine affiancata. Le sue DLL hanno gli stessi nomi/ABI di FFmpeg 9.0 e sostituiscono
  una per una le librerie di libmpv. Lumen riconosce il decoder dalla stringa di versione (`…-mvc`).
- **Seconda vista:** libbluray fornisce solo la vista base. `MvcMerger` legge il sotto-percorso SS della
  playlist (quale clip), la mappa EP del CLPI (punti di salto) e il `.m2ts` dipendente tramite libbluray,
  abbina le unità di accesso per PTS e aggiunge le NAL MVC alle NAL della vista base.
- **La riproduzione 3D passa sempre da libbluray** (`lumenbd://`), incluse «film principale» e la scelta dei titoli.
  Al disco viene comunicato «3D preferito» (PSR21/23), così i menu scelgono la playlist 3D.
- **Sottotitoli/menu:** libbluray effettua il rendering dei sottotitoli PG e della grafica dei menu; Lumen li
  disegna una volta per occhio (profondità regolabile nel profilo o nella scheda «Untertitel»).
- **Frame Packing (HDMI 1.4):** 1920×2205 (1080 + 45 + 1080 righe) a 23,976 Hz. La modalità video va creata
  come risoluzione personalizzata nel driver grafico; Lumen poi vi passa automaticamente. Molti proiettori
  riconoscono il frame packing dal timing; altrimenti usare SBS/TAB.
- Non esiste decodifica hardware per MVC (nessuna GPU la supporta); i dischi 3D rilevati vengono decodificati
  via software (AVC 1080p24, nessun problema per le CPU attuali).

## Architettura

```
src/MpvController   istanza libmpv, osservazione delle proprietà a eventi, profili -> opzioni mpv
src/BlurayNav       libbluray come stream mpv "lumenbd://": menu, titoli, 3D, overlay per occhio
src/MvcMerger       Blu-ray 3D: aggiunge la vista dipendente (sotto-percorso SS, mappa EP, abbinamento PTS)
src/DisplayManager  dispositivi di uscita, modalità frequenza / HDR / frame packing (Windows, xrandr, kscreen-doctor, CoreGraphics)
src/DriveManager    unità/dischi (thread di lavoro), espulsione
src/DiscScanner     libbluray: titoli, durate, stato AACS/BD+/BD-J/3D (thread di lavoro)
src/PlayerWindow    finestra di riproduzione integrata (API di rendering mpv/OpenGL), soprattutto per macOS
src/ProfileManager  preimpostazioni + profili utente
qml/                finestra di controllo
tools/              script di build/distribuzione, generatore di disco di prova
tests/              mvcmerge_test (unione MVC su una struttura di disco)
```

## Compilazione

Requisiti: CMake ≥ 3.21, Qt ≥ 6.5 (Quick, QuickControls2, OpenGL), libmpv ≥ 0.38 **collegato a FFmpeg come
librerie condivise**, libbluray ≥ 1.2, FFmpeg-mvc della stessa versione principale di FFmpeg usata da libmpv.

### Windows (MSYS2 UCRT64 – testato: GCC 16, Qt 6.11, mpv 0.41, libbluray 1.5, FFmpeg-mvc 9.0.2)

Tutte le parti devono usare lo stesso runtime C (UCRT) – per questo Qt, libmpv e libbluray provengono da MSYS2.
Gli strumenti si trovano volutamente in un **percorso breve** (`C:\lumen-build`), altrimenti GCC e la build
di FFmpeg falliscono per il limite di 260 caratteri di Windows.

```bash
# 1. Pacchetti (senza installare MSYS2, solo estrazione)
python tools/msys2_fetch.py --repo ucrt64 --dest C:/lumen-build/msys2 mpv qt6-base qt6-declarative qt6-svg qt6-tools gcc nasm pkgconf dav1d libva cmake ninja --skip mingw-w64-ucrt-x86_64-ffmpeg
python tools/msys2_fetch.py --repo msys --dest C:/lumen-build/msys2-tools make diffutils --skip msys2-runtime bash
# 2. FFmpeg-mvc (Git Bash, ~20 min) -> 3rdparty/ffmpeg-mvc
tools/build_ffmpeg_mvc.sh
# 3. Lumen (la distribuzione avviene dopo la build: windeployqt + DLL, FFmpeg-mvc con priorità)
U=C:/lumen-build/msys2/ucrt64; PATH=$U/bin:$PATH
cmake -S . -B build-ucrt -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$U -DMPV_ROOT=$U -DBLURAY_ROOT=$U -DLUMEN_DEPLOY_SEARCH=$U/bin
cmake --build build-ucrt
```

### Linux

```bash
sudo apt install qt6-declarative-dev qml6-module-qtquick-dialogs qml6-module-qtcore libbluray-dev nasm libdav1d-dev
tools/build_ffmpeg_mvc.sh            # FFMPEG_MVC_BRANCH=release/8.1 per FFmpeg 8.x
```
libmpv deve essere compilato con **la stessa versione principale di FFmpeg** (`ldd $(which mpv) | grep avcodec`).
Se la distribuzione non corrisponde, compilare mpv con `3rdparty/ffmpeg-mvc` (`meson setup -Dlibmpv=true`,
`PKG_CONFIG_PATH=3rdparty/ffmpeg-mvc/lib/pkgconfig`). In esecuzione: `LD_LIBRARY_PATH=3rdparty/ffmpeg-mvc/lib`.

### macOS

`brew install qt mpv libbluray libdvdnav libcdio openssl@3 libxml2 pkgconf ninja`, poi
`cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=$(brew --prefix qt)`, `cmake --build build` (Lumen.app) e
`cmake --build build --target lumen_dmg` (Lumen.app autonomo + Lumen.dmg). Il workflow `.github/workflows/macos.yml`
compila e testa su macOS 15. Blu-ray 3D: `brew install nasm dav1d`, FFmpeg-mvc come su Linux (branch corrispondente al FFmpeg di Homebrew),
`DYLD_LIBRARY_PATH` su `3rdparty/ffmpeg-mvc/lib`. Su macOS la finestra di riproduzione è integrata automaticamente.

### Senza 3D

Qualsiasi libmpv funziona (ad es. l'SDK shinchiro con `-DMPV_ROOT=…`); il Blu-ray 3D viene allora riprodotto
in 2D e l'interfaccia lo segnala («Kein MVC-Decoder»).

## Test

```bash
# Disco 3D sintetico da uno stream MVC di prova (fixture FFmpeg-mvc: occhio sinistro luma 165, destro 36)
perl <ffmpeg-mvc>/tests/fate/h264-mvc/mvc-mkfix.pl --out=mvc8.h264 --base=100 --dep=-100 --base-frames=8 --dep-frames=8
python tools/make_test_bd3d.py mvc8.h264 bd3d
# Verificare l'unione (cmake -DLUMEN_BUILD_TESTS=ON)
mvcmerge_test bd3d 0 merged.m2ts
ffmpeg -view_ids -1 -i merged.m2ts -fps_mode passthrough -f rawvideo -pix_fmt gray - | od -An -tu1 -w512 -v | awk '{print $1","$17}' | sort | uniq -c
#   -> 8 165,36  (ogni fotogramma: vista base a sinistra, vista dipendente a destra)
# In Lumen con un profilo 3D:  lumen bd3d
```

## Riga di comando

```bash
lumen D:\                      # unità: film principale direttamente (in 3D con profilo 3D)
lumen --menu D:\               # unità con menu del disco (ad es. per launcher HTPC)
lumen --menu Film.iso          # ISO / cartella BDMV con menu
lumen film.mkv                 # qualsiasi file riproducibile da mpv
```

## Tastiera

| Tasto | Finestra di controllo | Finestra di riproduzione |
|---|---|---|
| Spazio | Play/pausa | Play/pausa |
| ← / → (Maiusc) | ±10 s (±60 s), nei menu: navigazione | ±10 s, nei menu: navigazione |
| ↑ / ↓ | Volume, nei menu: navigazione | ±60 s, nei menu: navigazione |
| Invio / clic | Nei menu: conferma | Nei menu: conferma, altrimenti Invio = schermo intero |
| Home / Fine | Menu principale / menu pop-up | Menu principale / menu pop-up |
| Pag su / giù | Capitolo successivo / precedente | Capitolo successivo / precedente |
| , / . | Fotogramma per fotogramma | Fotogramma per fotogramma |
| F / doppio clic | Schermo intero | Schermo intero (Invio / doppio clic) |
| L | Loop A-B | Loop A-B |
| S | Screenshot | Screenshot |
| I | Statistiche | Statistiche |
| [ / ] / ⌫ | Velocità ∓ / ripristina | |
| Ctrl+O / Ctrl+E | Apri / espelli | |

## Menu del disco

- libbluray esegue il programma di menu del disco e passa lo stream a mpv tramite `lumenbd://`;
  la grafica dei menu (IG o BD-J) arriva come overlay ARGB scalato sull'area video (per occhio in modalità 3D).
- Comandi: frecce/Invio/mouse nella finestra di riproduzione, croce direzionale nella finestra di controllo, Home = menu principale, Fine = pop-up.
- Le scelte audio/sottotitoli fatte nel menu del disco vengono riportate tramite il PID dello stream sulla traccia corrispondente.
- **I menu BD-J** richiedono un runtime Java (JRE ≥ 8) e `libbluray-j2se-*.jar`; senza di essi resta disponibile la modalità titoli.

## Limitazioni / stato

| Tema | Stato |
|---|---|
| Blu-ray 3D | Implementato e testato end-to-end con un disco 3D sintetico (unione, salto, SBS, frame packing). **Non ancora testato con un vero disco 3D**; lo stesso vale per sottotitoli/menu 3D (il disco di prova non ha PG/IG). FFmpeg-mvc è un fork sperimentale. |
| Frame packing | Richiede una modalità 1920×2205 nel driver grafico; il riconoscimento come 3D senza InfoFrame HDMI 3D dipende dal proiettore. |
| Dolby Vision | Rilevato (profilo 5/7/8), gpu-next applica i metadati RPU; nessun lettore PC può emettere un vero segnale DV via HDMI. |
| Menu a immagine fissa | I menu puramente statici possono restare neri per un istante (latenza del decoder). |
| Cambio frequenza/HDR | Windows: frequenza + HDR · Linux X11: frequenza (xrandr) · KDE Plasma: frequenza + HDR · GNOME Wayland: solo visualizzazione · macOS: frequenza |
| Finestra di riproduzione integrata | Automatica su macOS, altrimenti per profilo; API di rendering OpenGL → solo SDR. |

Aiuto per sviluppatori: `LUMEN_SNAPSHOT=shot.png` (opzionale `LUMEN_SNAPSHOT_DELAY=ms`) salva la finestra di controllo come immagine.
