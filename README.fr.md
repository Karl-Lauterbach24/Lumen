# Lumen – Lecteur Blu-ray

[English](README.md) · [Deutsch](README.de.md) · **Français** · [Español](README.es.md) · [Italiano](README.it.md) · [Português](README.pt.md)

> **Nouveau :** Lumen lit désormais aussi les **DVD-Video (avec menus), HD DVD, Video-CD/SVCD (y compris images CUE/BIN), CD audio** et les **Digital Cinema Packages (DCP, JPEG 2000, SMPTE/Interop, chiffrés avec KDM)**, avec onglet « Kino » (certificat du lecteur, KDM, fader cinéma, routage des canaux, programme de projection) et des profils de calibration (ICC, LUT 3D, qualité de référence). Détails : [README en anglais](README.md).

Lecteur Blu-ray / UHD / 3D rapide et minimaliste pour le home cinéma, avec **deux fenêtres** :

- **Fenêtre de lecture** – fenêtre mpv native (gpu-next, D3D11/Vulkan/Wayland), attribuable à un périphérique de sortie, HDR passthrough.
- **Fenêtre de contrôle** – Qt Quick : source, transport, titres, chapitres, audio, sous-titres, image, profils de sortie.

> L'interface est actuellement en allemand.

## Protection contre la copie / LibreDrive

Lumen **ne contourne aucune protection contre la copie**. Les disques sont lus exclusivement via `libbluray`.
Si un disque est protégé par AACS/BD+, cela doit déjà être géré en dehors de Lumen – par exemple un lecteur
avec firmware LibreDrive et une bibliothèque AACS installée par l'utilisateur, chargée par libbluray à
l'exécution. Il en va de même pour la seconde vue 3D : elle est lue via libbluray (`bd_open_file_dec`),
donc via la même bibliothèque externe.

## Fonctionnalités

| Domaine | Contenu |
|---|---|
| Sources | Lecteurs optiques (détection automatique, fabricant/modèle/firmware, éjection, lecture automatique à l'insertion), ISO, dossiers BDMV, tous les formats lisibles par mpv |
| Disque | Menus du disque (HDMV, BD-J avec Java), film principal, titres/playlists avec durée, format vidéo/audio, détection UHD et 3D |
| **Blu-ray 3D** | **Les deux vues (MVC)**, sortie en HDMI Frame Packing 1080p, côte à côte / haut-bas (demi/plein), entrelacé par lignes, anaglyphe ou 2D ; sous-titres et menus par œil avec profondeur réglable |
| Transport | Lecture/pause, stop, ±10 s/±60 s, chapitres, image par image avant/arrière, boucle A-B, vitesse, défilement avec repères de chapitres, capture d'écran |
| Audio | Choix de piste, bitstream (TrueHD/Atmos, DTS-HD MA/DTS:X, DD+, DD), WASAPI exclusif, configuration des canaux, décalage audio |
| Sous-titres | Choix de piste (PGS/SRT/ASS), forcés uniquement, décalage, taille, position (écrans cinémascope) |
| Image | Format d'image, pan & scan, zoom, luminosité/contraste/saturation/gamma, désentrelacement, format source 3D pour les fichiers |
| Profils de sortie | Périphérique cible, plein écran, adaptation de la fréquence (23,976 → 23/24 Hz), HDR système auto on/off, HDR passthrough ou tone mapping, qualité de mise à l'échelle, synchro, format de sortie 3D, périphérique audio, options expertes |

Préréglages : *Desktop*, *Projecteur DLP 3D 1080p* (demi SBS), *Projecteur DLP 3D 1080p (Frame Packing)*,
*Projecteur LED 4K HDR (passthrough)*, *Projecteur LED 4K HDR (tone mapping du lecteur)*, *TV 4K HDR (OLED)*, *Performance/Portable*.

## Blu-ray 3D

```
libbluray bd_read_ext()  ── vue de base (PID 0x1011) ───────┐
                                                             ├─ MvcMerger ─ TS ─> mpv ─> FFmpeg-mvc ─> image SBS
libbluray bd_open_file_dec() ─ vue dépendante (0x1012) ─────┘   (appariée image par image via PTS)       │
                                                                                                          v
                                          vf : stereo3d / graphe frame packing ─> format du profil de sortie
```

- **Décodeur :** FFmpeg standard ne décode que la vue de base. Lumen utilise
  [FFmpeg-mvc](https://github.com/tthayer93/FFmpeg-mvc) (branche `release/9.0`), qui décode les deux vues
  en une image côte à côte. Ses DLL ont les mêmes noms/ABI que FFmpeg 9.0 et remplacent une à une les
  bibliothèques de libmpv. Lumen détecte ce décodeur grâce à sa chaîne de version (`…-mvc`).
- **Seconde vue :** libbluray ne fournit que la vue de base. `MvcMerger` lit le sous-chemin SS de la
  playlist (quel clip), la carte EP du CLPI (points de saut) et le `.m2ts` dépendant via libbluray,
  apparie les unités d'accès par PTS et ajoute les NAL MVC aux NAL de la vue de base.
- **La lecture 3D passe toujours par libbluray** (`lumenbd://`), y compris « film principal » et choix du titre.
  Le disque reçoit « 3D préférée » (PSR21/23) afin que les menus choisissent la playlist 3D.
- **Sous-titres/menus :** libbluray effectue le rendu des sous-titres PG et des menus ; Lumen les dessine
  une fois par œil (profondeur réglable dans le profil ou l'onglet « Untertitel »).
- **Frame Packing (HDMI 1.4) :** 1920×2205 (1080 + 45 + 1080 lignes) à 23,976 Hz. Ce mode d'affichage doit
  être créé comme résolution personnalisée dans le pilote graphique ; Lumen y bascule ensuite automatiquement.
  Beaucoup de projecteurs détectent le frame packing d'après le timing ; sinon, utilisez SBS/TAB.
- Aucun décodage matériel n'existe pour MVC (aucun GPU ne le prend en charge) ; les disques 3D détectés
  sont décodés en logiciel (AVC 1080p24, sans problème pour les CPU actuels).

## Architecture

```
src/MpvController   instance libmpv, observation des propriétés par événements, profils -> options mpv
src/BlurayNav       libbluray comme flux mpv « lumenbd:// » : menus, titres, 3D, overlays par œil
src/MvcMerger       Blu-ray 3D : ajout de la vue dépendante (sous-chemin SS, carte EP, appariement PTS)
src/DisplayManager  périphériques de sortie, mode fréquence / HDR / frame packing (Windows, xrandr, kscreen-doctor, CoreGraphics)
src/DriveManager    lecteurs/disques (thread de travail), éjection
src/DiscScanner     libbluray : titres, durées, état AACS/BD+/BD-J/3D (thread de travail)
src/PlayerWindow    fenêtre de lecture intégrée (API de rendu mpv/OpenGL), surtout pour macOS
src/ProfileManager  préréglages + profils utilisateur
qml/                fenêtre de contrôle
tools/              scripts de build/déploiement, générateur de disque de test
tests/              mvcmerge_test (fusion MVC sur une structure de disque)
```

## Compilation

Prérequis : CMake ≥ 3.21, Qt ≥ 6.5 (Quick, QuickControls2, OpenGL), libmpv ≥ 0.38 **lié à FFmpeg en
bibliothèques partagées**, libbluray ≥ 1.2, FFmpeg-mvc correspondant à la version majeure de FFmpeg de libmpv.

### Windows (MSYS2 UCRT64 – testé : GCC 16, Qt 6.11, mpv 0.41, libbluray 1.5, FFmpeg-mvc 9.0.2)

Toutes les parties doivent utiliser le même runtime C (UCRT) – Qt, libmpv et libbluray proviennent donc de MSYS2.
Les outils sont volontairement placés dans un **chemin court** (`C:\lumen-build`), sinon GCC et le build
FFmpeg échouent à cause de la limite de 260 caractères de Windows.

```bash
# 1. Paquets (sans installation de MSYS2, simple extraction)
python tools/msys2_fetch.py --repo ucrt64 --dest C:/lumen-build/msys2 mpv qt6-base qt6-declarative qt6-svg qt6-tools gcc nasm pkgconf dav1d libva cmake ninja --skip mingw-w64-ucrt-x86_64-ffmpeg
python tools/msys2_fetch.py --repo msys --dest C:/lumen-build/msys2-tools make diffutils --skip msys2-runtime bash
# 2. FFmpeg-mvc (Git Bash, ~20 min) -> 3rdparty/ffmpeg-mvc
tools/build_ffmpeg_mvc.sh
# 3. Lumen (déploiement en post-build : windeployqt + DLL, FFmpeg-mvc en priorité)
U=C:/lumen-build/msys2/ucrt64; PATH=$U/bin:$PATH
cmake -S . -B build-ucrt -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$U -DMPV_ROOT=$U -DBLURAY_ROOT=$U -DLUMEN_DEPLOY_SEARCH=$U/bin
cmake --build build-ucrt
```

### Linux

```bash
sudo apt install qt6-declarative-dev qml6-module-qtquick-dialogs qml6-module-qtcore libbluray-dev nasm libdav1d-dev
tools/build_ffmpeg_mvc.sh            # FFMPEG_MVC_BRANCH=release/8.1 pour FFmpeg 8.x
```
libmpv doit être compilé avec **la même version majeure de FFmpeg** (`ldd $(which mpv) | grep avcodec`).
Si votre distribution ne correspond pas, compilez mpv avec `3rdparty/ffmpeg-mvc` (`meson setup -Dlibmpv=true`,
`PKG_CONFIG_PATH=3rdparty/ffmpeg-mvc/lib/pkgconfig`). À l'exécution : `LD_LIBRARY_PATH=3rdparty/ffmpeg-mvc/lib`.

### macOS

`brew install qt mpv libbluray nasm dav1d`, FFmpeg-mvc comme sous Linux (branche correspondant au FFmpeg de Homebrew),
`DYLD_LIBRARY_PATH` vers `3rdparty/ffmpeg-mvc/lib`. La fenêtre de lecture y est automatiquement intégrée.

### Sans 3D

N'importe quel libmpv fonctionne (p. ex. le SDK shinchiro avec `-DMPV_ROOT=…`) ; le Blu-ray 3D est alors lu
en 2D et l'interface l'indique (« Kein MVC-Decoder »).

## Tests

```bash
# Disque 3D synthétique à partir d'un flux MVC de test (fixture FFmpeg-mvc : œil gauche luma 165, droit 36)
perl <ffmpeg-mvc>/tests/fate/h264-mvc/mvc-mkfix.pl --out=mvc8.h264 --base=100 --dep=-100 --base-frames=8 --dep-frames=8
python tools/make_test_bd3d.py mvc8.h264 bd3d
# Vérifier la fusion (cmake -DLUMEN_BUILD_TESTS=ON)
mvcmerge_test bd3d 0 merged.m2ts
ffmpeg -view_ids -1 -i merged.m2ts -fps_mode passthrough -f rawvideo -pix_fmt gray - | od -An -tu1 -w512 -v | awk '{print $1","$17}' | sort | uniq -c
#   -> 8 165,36  (chaque image : vue de base à gauche, vue dépendante à droite)
# Dans Lumen avec un profil 3D :  lumen bd3d
```

## Ligne de commande

```bash
lumen D:\                      # lecteur : film principal directement (en 3D avec un profil 3D)
lumen --menu D:\               # lecteur avec menu du disque (p. ex. pour lanceurs HTPC)
lumen --menu Film.iso          # ISO / dossier BDMV avec menu
lumen film.mkv                 # tout fichier lisible par mpv
```

## Clavier

| Touche | Fenêtre de contrôle | Fenêtre de lecture |
|---|---|---|
| Espace | Lecture/pause | Lecture/pause |
| ← / → (Maj) | ±10 s (±60 s), dans les menus : navigation | ±10 s, dans les menus : navigation |
| ↑ / ↓ | Volume, dans les menus : navigation | ±60 s, dans les menus : navigation |
| Entrée / clic | Dans les menus : valider | Dans les menus : valider, sinon Entrée = plein écran |
| Début / Fin | Menu principal / menu pop-up | Menu principal / menu pop-up |
| Page préc. / suiv. | Chapitre suivant / précédent | Chapitre suivant / précédent |
| , / . | Image par image | Image par image |
| F / double-clic | Plein écran | Plein écran (Entrée / double-clic) |
| L | Boucle A-B | Boucle A-B |
| S | Capture d'écran | Capture d'écran |
| I | Statistiques | Statistiques |
| [ / ] / ⌫ | Vitesse ∓ / réinitialiser | |
| Ctrl+O / Ctrl+E | Ouvrir / éjecter | |

## Menus du disque

- libbluray exécute le programme de menu du disque et transmet le flux à mpv via `lumenbd://` ;
  les graphismes des menus (IG ou BD-J) arrivent en overlay ARGB mis à l'échelle de la zone vidéo (par œil en mode 3D).
- Commande : flèches/Entrée/souris dans la fenêtre de lecture, pavé directionnel dans la fenêtre de contrôle, Début = menu principal, Fin = pop-up.
- Les choix audio/sous-titres faits dans le menu du disque sont transposés via le PID du flux sur la piste correspondante.
- **Les menus BD-J** nécessitent un runtime Java (JRE ≥ 8) et `libbluray-j2se-*.jar` ; sinon, le mode titre reste disponible.

## Limitations / état

| Sujet | État |
|---|---|
| Blu-ray 3D | Implémenté et testé de bout en bout avec un disque 3D synthétique (fusion, saut, SBS, frame packing). **Pas encore testé avec un vrai disque 3D** ; de même pour les sous-titres/menus 3D (disque de test sans PG/IG). FFmpeg-mvc est un fork expérimental. |
| Frame packing | Nécessite un mode 1920×2205 dans le pilote graphique ; la reconnaissance 3D par le projecteur sans InfoFrame HDMI 3D dépend de l'appareil. |
| Dolby Vision | Détecté (profil 5/7/8), gpu-next applique les métadonnées RPU ; aucun lecteur PC ne peut produire un vrai signal DV en HDMI. |
| Menus en image fixe | Les menus purement fixes peuvent rester noirs un court instant (latence du décodeur). |
| Changement fréquence/HDR | Windows : fréquence + HDR · Linux X11 : fréquence (xrandr) · KDE Plasma : fréquence + HDR · GNOME Wayland : affichage seul · macOS : fréquence |
| Fenêtre de lecture intégrée | Automatique sur macOS, sinon par profil ; API de rendu OpenGL → SDR uniquement. |

Aide au développement : `LUMEN_SNAPSHOT=shot.png` (option `LUMEN_SNAPSHOT_DELAY=ms`) enregistre la fenêtre de contrôle en image.
