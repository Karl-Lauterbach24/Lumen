<p align="center"><img src="resources/logo/lumen-logo.png" width="300" alt="Lumen"></p>

# Lumen – Lettore di dischi e cinema digitale

[English](README.md) · [Deutsch](README.de.md) · [Français](README.fr.md) · [Español](README.es.md) · **Italiano** · [Português](README.pt.md) · [Nederlands](README.nl.md) · [Polski](README.pl.md) · [Svenska](README.sv.md) · [Čeština](README.cs.md) · [Türkçe](README.tr.md) · [Українська](README.uk.md) · [Русский](README.ru.md) · [日本語](README.ja.md) · [简体中文](README.zh.md) · [한국어](README.ko.md)

Un lettore veloce e minimalista per l'home cinema, le sale di proiezione e i piccoli cinema, con **due finestre**:

- **Finestra di riproduzione** – finestra mpv nativa (gpu-next, D3D11/Vulkan/Wayland), assegnabile a un dispositivo di uscita, HDR passthrough.
- **Finestra di controllo** – sorgente, trasporto, titoli, capitoli, audio, sottotitoli, immagine, cinema, streaming, profili di uscita e plugin.

Lumen riproduce **Blu-ray / UHD / Blu-ray 3D, DVD-Video con menu, HD DVD, Video CD, CD audio, Digital Cinema Packages (DCP)**, flussi di rete e tutti i formati di file che mpv sa riprodurre.

<p align="center">
  <img src="docs/screenshots/start.png" width="49%" alt="Lumen">
  <img src="docs/screenshots/cinema.png" width="49%" alt="Lumen">
</p>

## Download

I pacchetti precompilati si trovano nella [pagina delle versioni](https://github.com/Karl-Lauterbach24/Lumen/releases):

| Sistema | File |
|---|---|
| Windows 10/11 (x64, ARM64) | `Lumen-<version>-windows-x64.msi`, `…-windows-arm64.msi` (installer) · `.zip` (portatile) |
| macOS 15 (Apple Silicon, Intel) | `Lumen-<version>-macos-arm64.dmg`, `…-macos-x64.dmg` |
| Debian 13 (x86-64, ARM64) | `Lumen-<version>-linux-amd64.deb`, `…-linux-arm64.deb` |
| Fedora 44 (x86-64, ARM64) | `Lumen-<version>-linux-x86_64.rpm`, `…-linux-aarch64.rpm` |

I pacchetti Linux usano Qt 6, libmpv e le librerie per i dischi della tua distribuzione. Lumen controlla la presenza di nuove versioni e, su Windows e macOS, le installa con un clic dopo aver verificato il checksum.

## Funzioni

- **Dischi:** Blu-ray e DVD con menu, titoli, capitoli, tracce audio e sottotitoli; le unità vengono rilevate automaticamente.
- **Cinema digitale:** DCP in JPEG 2000, SMPTE e Interop, pacchetti cifrati con KDM, rendering Dolby Atmos/IAB, programmi di proiezione.
- **Streaming:** collegamenti di ogni tipo (HLS, DASH, RTSP, …) e i server multimediali Jellyfin, Emby e Plex.
- **3D:** Blu-ray 3D (MVC) in frame packing, affiancato, sopra-sotto o anaglifo.
- **Profili di uscita:** schermo di destinazione, adattamento della frequenza, HDR passthrough o tone mapping, calibrazione (ICC, LUT 3D).
- **Audio:** bitstream verso un ricevitore AV (TrueHD/Atmos, DTS-HD), modalità notte, ritardo e velocità.
- **CD audio:** nomi delle tracce dal CD-Text oppure, con il plugin *Disc identification*, da MusicBrainz.
- **Uso quotidiano:** elenco dei contenuti riprodotti di recente con ripresa, trascinamento, file di sottotitoli esterni, scorciatoie da tastiera (F1).
- **Interfaccia in 16 lingue**, selezionabile nella pagina iniziale.

## Protezione anticopia

Lumen **non** contiene alcun aggiramento della protezione anticopia. I dischi protetti (AACS, BD+, CSS) vengono riprodotti solo se aggiungi tu stesso le librerie necessarie tramite un plugin; spetta a te verificare che sia legale nel tuo paese.

## Plugin

I plugin aggiungono sorgenti, chiavi, script e funzioni. La scheda **Plugin** li installa dal [negozio dei plugin](https://github.com/Karl-Lauterbach24/Lumen-Plugins) o dalle tue sorgenti; ogni file viene verificato con il suo checksum.

## Altre informazioni

Compilazione, architettura, test, riga di comando e tutte le scorciatoie da tastiera sono descritti nel [README in inglese](README.md). Questa traduzione è stata realizzata con un aiuto automatico; le correzioni sono benvenute.

## Licenza

Lumen è software libero sotto la **GNU Affero General Public License v3.0 o successiva** ([LICENSE](LICENSE)). Componenti e licenze: [THIRD_PARTY.md](THIRD_PARTY.md).
