<p align="center"><img src="resources/logo/lumen-logo.png" width="300" alt="Lumen"></p>

# Lumen – Lecteur de disques et de cinéma numérique

[English](README.md) · [Deutsch](README.de.md) · **Français** · [Español](README.es.md) · [Italiano](README.it.md) · [Português](README.pt.md) · [Nederlands](README.nl.md) · [Polski](README.pl.md) · [Svenska](README.sv.md) · [Čeština](README.cs.md) · [Türkçe](README.tr.md) · [Українська](README.uk.md) · [Русский](README.ru.md) · [日本語](README.ja.md) · [简体中文](README.zh.md) · [한국어](README.ko.md)

Un lecteur rapide et minimaliste pour le home cinéma, les salles de projection et les petits cinémas, avec **deux fenêtres** :

- **Fenêtre de lecture** – fenêtre mpv native (gpu-next, D3D11/Vulkan/Wayland), attribuable à un périphérique de sortie, HDR passthrough.
- **Fenêtre de contrôle** – source, transport, titres, chapitres, audio, sous-titres, image, cinéma, streaming, profils de sortie et plugins.

Lumen lit les **Blu-ray / UHD / Blu-ray 3D, DVD-Video avec menus, HD DVD, Video CD, CD audio, Digital Cinema Packages (DCP)**, les flux réseau et tous les formats de fichiers que mpv sait lire.

<p align="center">
  <img src="docs/screenshots/start.png" width="49%" alt="Lumen">
  <img src="docs/screenshots/cinema.png" width="49%" alt="Lumen">
</p>

## Téléchargements

Les paquets précompilés se trouvent sur la [page des versions](https://github.com/Karl-Lauterbach24/Lumen/releases) :

| Système | Fichier |
|---|---|
| Windows 10/11 (x64, ARM64) | `Lumen-<version>-windows-x64.msi`, `…-windows-arm64.msi` (installeur) · `.zip` (portable) |
| macOS 15 (Apple Silicon, Intel) | `Lumen-<version>-macos-arm64.dmg`, `…-macos-x64.dmg` |
| Debian 13 (x86-64, ARM64) | `Lumen-<version>-linux-amd64.deb`, `…-linux-arm64.deb` |
| Fedora 44 (x86-64, ARM64) | `Lumen-<version>-linux-x86_64.rpm`, `…-linux-aarch64.rpm` |

Tous les paquets contiennent les mêmes bibliothèques multimédias (FFmpeg avec décodeur Blu-ray 3D, libmpv) ; sous Linux, Qt 6 et les bibliothèques de disques viennent de votre distribution. Lumen vérifie la présence de nouvelles versions et, sous Windows et macOS, les installe en un clic après contrôle de la somme de contrôle.

## Fonctionnalités

- **Disques :** Blu-ray et DVD avec menus, titres, chapitres, pistes audio et sous-titres ; les lecteurs sont détectés automatiquement.
- **Cinéma numérique :** DCP en JPEG 2000, SMPTE et Interop, paquets chiffrés avec KDM, rendu Dolby Atmos/IAB, programmes de projection.
- **Streaming :** liens de toutes sortes (HLS, DASH, RTSP, …) et serveurs multimédias Jellyfin, Emby et Plex.
- **Diffusion :** envoie l'image et le son vers un téléviseur ou un récepteur du réseau : DLNA, Chromecast, AirPlay (récepteurs sans appairage), les applications [Lumen TV](https://github.com/Karl-Lauterbach24/Lumen-TV) pour Android TV, Samsung et LG, ou n'importe quel navigateur. Miracast passe par le réglage d'écran sans fil du système.
- **3D :** Blu-ray 3D (MVC) en frame packing, côte à côte, haut-bas ou anaglyphe.
- **Profils de sortie :** écran cible, adaptation de la fréquence, HDR passthrough ou tone mapping, calibration (ICC, LUT 3D).
- **Audio :** bitstream vers un ampli AV (TrueHD/Atmos, DTS-HD), mode nuit, décalage et vitesse.
- **CD audio :** noms des pistes depuis le CD-Text ou, avec le plugin *Disc identification*, depuis MusicBrainz.
- **Au quotidien :** liste des lectures récentes avec reprise, glisser-déposer, fichiers de sous-titres externes, raccourcis clavier (F1).
- **Interface en 16 langues**, à choisir dans l'en-tête de la fenêtre (symbole du globe).
- **LumenOS :** Lumen comme lecteur autonome – une image pour clé USB (x86-64 et ARM64) qui démarre directement dans Lumen sur le téléviseur, se pilote à la télécommande, se met à jour toute seule et copie les disques sur son disque, un disque USB ou un NAS. Voir [os/README.md](os/README.md).

## Protection contre la copie

Lumen ne contient **aucun** contournement de protection contre la copie. Les disques protégés (AACS, BD+, CSS) ne sont lus que si vous ajoutez vous-même les bibliothèques nécessaires au moyen d'un plugin ; il vous appartient de vérifier que c'est légal dans votre pays.

## Plugins

Les plugins ajoutent des sources, des clés, des scripts et des fonctions. L'onglet **Plugins** les installe depuis la [boutique de plugins](https://github.com/Karl-Lauterbach24/Lumen-Plugins) ou depuis vos propres sources ; chaque fichier est vérifié par sa somme de contrôle.

## Pour aller plus loin

La compilation, l'architecture, les tests, la ligne de commande et tous les raccourcis clavier sont décrits dans le [README en anglais](README.md). Cette traduction a été réalisée avec une aide automatique ; les corrections sont les bienvenues.

## Licence

Lumen est un logiciel libre sous **GNU Affero General Public License v3.0 ou ultérieure** ([LICENSE](LICENSE)). Composants et licences : [THIRD_PARTY.md](THIRD_PARTY.md).
