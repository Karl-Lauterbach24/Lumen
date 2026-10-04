<p align="center"><img src="resources/logo/lumen-logo.png" width="300" alt="Lumen"></p>

# Lumen – Spelare för skivor och digital bio

[English](README.md) · [Deutsch](README.de.md) · [Français](README.fr.md) · [Español](README.es.md) · [Italiano](README.it.md) · [Português](README.pt.md) · [Nederlands](README.nl.md) · [Polski](README.pl.md) · **Svenska** · [Čeština](README.cs.md) · [Türkçe](README.tr.md) · [Українська](README.uk.md) · [Русский](README.ru.md) · [日本語](README.ja.md) · [简体中文](README.zh.md) · [한국어](README.ko.md)

En snabb, minimalistisk spelare för hemmabio, visningsrum och små biografer, med **två fönster**:

- **Spelarfönster** – eget mpv-fönster (gpu-next, D3D11/Vulkan/Wayland) som kan låsas till en utenhet, HDR-genomströmning.
- **Kontrollfönster** – källa, transport, titlar, kapitel, ljud, undertexter, bild, bio, strömning, utprofiler och plugins.

Lumen spelar **Blu-ray / UHD / Blu-ray 3D, DVD-Video med menyer, HD DVD, Video-cd, ljud-cd, Digital Cinema Packages (DCP)**, strömmar och alla filformat som mpv kan spela.

<p align="center">
  <img src="docs/screenshots/start.png" width="49%" alt="Lumen">
  <img src="docs/screenshots/cinema.png" width="49%" alt="Lumen">
</p>

## Nedladdningar

Färdiga paket finns på [versionssidan](https://github.com/Karl-Lauterbach24/Lumen/releases):

| System | Fil |
|---|---|
| Windows 10/11 (x64, ARM64) | `Lumen-<version>-windows-x64.msi`, `…-windows-arm64.msi` (installationsprogram) · `.zip` (portabel) |
| macOS 15 (Apple Silicon, Intel) | `Lumen-<version>-macos-arm64.dmg`, `…-macos-x64.dmg` |
| Debian 13 (x86-64, ARM64) | `Lumen-<version>-linux-amd64.deb`, `…-linux-arm64.deb` |
| Fedora 44 (x86-64, ARM64) | `Lumen-<version>-linux-x86_64.rpm`, `…-linux-aarch64.rpm` |

Alla paket innehåller samma mediebibliotek (FFmpeg med Blu-ray 3D-avkodare, libmpv); under Linux kommer Qt 6 och skivbiblioteken från din distribution. Lumen söker efter nya versioner och installerar dem i Windows och macOS med ett klick, efter att kontrollsumman har verifierats.

## Funktioner

- **Skivor:** Blu-ray och dvd med skivmenyer, titlar, kapitel, ljud- och undertextspår; enheter hittas automatiskt.
- **Digital bio:** DCP med JPEG 2000, SMPTE och Interop, krypterade paket med KDM, rendering av Dolby Atmos/IAB, visningsprogram.
- **Strömning:** länkar av alla slag (HLS, DASH, RTSP, …) och medieservrarna Jellyfin, Emby och Plex.
- **Castning:** skickar bild och ljud till en tv eller mottagare i nätverket: DLNA, Chromecast, AirPlay (mottagare utan parkoppling), [Lumen TV](https://github.com/Karl-Lauterbach24/Lumen-TV)-apparna för Android TV, Samsung och LG, eller valfri webbläsare. Miracast går via systemets inställning för trådlös skärm.
- **3D:** Blu-ray 3D (MVC) som frame packing, side-by-side, top-and-bottom eller anaglyf.
- **Utprofiler:** målskärm, anpassning av bildfrekvens, HDR-genomströmning eller tonmappning, kalibrering (ICC, 3D-LUT).
- **Ljud:** bitström till en AV-förstärkare (TrueHD/Atmos, DTS-HD), nattläge, fördröjning och hastighet.
- **Ljud-cd:** spårnamn från CD-Text eller, med pluginet *Disc identification*, från MusicBrainz.
- **I vardagen:** lista över senast spelade med återupptagning, dra och släpp, externa undertextfiler, kortkommandon (F1).
- **Gränssnitt på 16 språk**, väljs i fönstrets rubrikrad (jordglobssymbolen).

## Kopieringsskydd

Lumen innehåller **ingen** kringgående av kopieringsskydd. Skyddade skivor (AACS, BD+, CSS) spelas bara om du själv lägger till de bibliotek som behövs via ett plugin; du ansvarar för att det är lagligt i ditt land.

## Plugins

Plugins lägger till källor, nycklar, skript och funktioner. Fliken **Plugins** installerar dem från [plugin-butiken](https://github.com/Karl-Lauterbach24/Lumen-Plugins) eller från dina egna källor; varje fil kontrolleras mot sin kontrollsumma.

## Mer

Bygga från källkod, arkitektur, tester, kommandorad och alla kortkommandon beskrivs i den [engelska README-filen](README.md). Översättningen är gjord med maskinhjälp; rättelser tas gärna emot.

## Licens

Lumen är fri programvara under **GNU Affero General Public License v3.0 eller senare** ([LICENSE](LICENSE)). Komponenter och licenser: [THIRD_PARTY.md](THIRD_PARTY.md).
