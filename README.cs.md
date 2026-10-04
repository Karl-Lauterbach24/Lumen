<p align="center"><img src="resources/logo/lumen-logo.png" width="300" alt="Lumen"></p>

# Lumen – Přehrávač disků a digitálního kina

[English](README.md) · [Deutsch](README.de.md) · [Français](README.fr.md) · [Español](README.es.md) · [Italiano](README.it.md) · [Português](README.pt.md) · [Nederlands](README.nl.md) · [Polski](README.pl.md) · [Svenska](README.sv.md) · **Čeština** · [Türkçe](README.tr.md) · [Українська](README.uk.md) · [Русский](README.ru.md) · [日本語](README.ja.md) · [简体中文](README.zh.md) · [한국어](README.ko.md)

Rychlý, minimalistický přehrávač pro domácí kina, promítací sály a malá kina, se **dvěma okny**:

- **Okno přehrávače** – nativní okno mpv (gpu-next, D3D11/Vulkan/Wayland), které lze připnout k výstupnímu zařízení, průchod HDR.
- **Ovládací okno** – zdroj, ovládání přehrávání, tituly, kapitoly, zvuk, titulky, obraz, kino, streamování, výstupní profily a pluginy.

Lumen přehrává **Blu-ray / UHD / Blu-ray 3D, DVD-Video s nabídkami, HD DVD, Video CD, zvuková CD, Digital Cinema Packages (DCP)**, streamy a každý formát souboru, který umí přehrát mpv.

<p align="center">
  <img src="docs/screenshots/start.png" width="49%" alt="Lumen">
  <img src="docs/screenshots/cinema.png" width="49%" alt="Lumen">
</p>

## Ke stažení

Hotové balíčky najdete na [stránce vydání](https://github.com/Karl-Lauterbach24/Lumen/releases):

| Systém | Soubor |
|---|---|
| Windows 10/11 (x64, ARM64) | `Lumen-<version>-windows-x64.msi`, `…-windows-arm64.msi` (instalátor) · `.zip` (přenosná verze) |
| macOS 15 (Apple Silicon, Intel) | `Lumen-<version>-macos-arm64.dmg`, `…-macos-x64.dmg` |
| Debian 13 (x86-64, ARM64) | `Lumen-<version>-linux-amd64.deb`, `…-linux-arm64.deb` |
| Fedora 44 (x86-64, ARM64) | `Lumen-<version>-linux-x86_64.rpm`, `…-linux-aarch64.rpm` |

Všechny balíčky obsahují stejné multimediální knihovny (FFmpeg s dekodérem Blu-ray 3D, libmpv); v Linuxu pocházejí Qt 6 a knihovny pro disky z vaší distribuce. Lumen hledá nové verze a ve Windows a macOS je po ověření kontrolního součtu nainstaluje jedním kliknutím.

## Funkce

- **Disky:** Blu-ray a DVD s nabídkami, tituly, kapitolami, zvukovými stopami a titulky; jednotky se rozpoznají automaticky.
- **Digitální kino:** DCP s JPEG 2000, SMPTE a Interop, šifrované balíčky s KDM, renderování Dolby Atmos/IAB, programy promítání.
- **Streamování:** odkazy všeho druhu (HLS, DASH, RTSP, …) a mediální servery Jellyfin, Emby a Plex.
- **Přenos:** odešle obraz a zvuk do televizoru nebo přijímače v síti: DLNA, Chromecast, AirPlay (přijímače bez párování), aplikace [Lumen TV](https://github.com/Karl-Lauterbach24/Lumen-TV) pro Android TV, Samsung a LG nebo libovolný prohlížeč. Miracast funguje přes systémové nastavení bezdrátové obrazovky.
- **3D:** Blu-ray 3D (MVC) jako frame packing, vedle sebe, nad sebou nebo anaglyf.
- **Výstupní profily:** cílová obrazovka, přizpůsobení obnovovací frekvence, průchod HDR nebo mapování tónů, kalibrace (ICC, 3D LUT).
- **Zvuk:** bitstream do AV receiveru (TrueHD/Atmos, DTS-HD), noční režim, zpoždění a rychlost.
- **Zvukové CD:** názvy stop z CD-Textu nebo, s pluginem *Disc identification*, z MusicBrainz.
- **Každodenní používání:** seznam naposledy přehraných s pokračováním, přetažení myší, externí soubory s titulky, klávesové zkratky (F1).
- **Rozhraní v 16 jazycích**, volitelné v záhlaví okna (symbol zeměkoule).

## Ochrana proti kopírování

Lumen **neobsahuje** žádné obcházení ochrany proti kopírování. Chráněné disky (AACS, BD+, CSS) se přehrají jen tehdy, když si potřebné knihovny přidáte sami pomocí pluginu; za soulad s právem vaší země odpovídáte vy.

## Pluginy

Pluginy přidávají zdroje, klíče, skripty a funkce. Karta **Pluginy** je instaluje z [obchodu s pluginy](https://github.com/Karl-Lauterbach24/Lumen-Plugins) nebo z vašich vlastních zdrojů; každý soubor se ověřuje kontrolním součtem.

## Další informace

Sestavení ze zdrojových kódů, architektura, testy, příkazový řádek a všechny klávesové zkratky jsou popsány v [anglickém README](README.md). Tento překlad vznikl se strojovou pomocí; opravy jsou vítány.

## Licence

Lumen je svobodný software pod licencí **GNU Affero General Public License v3.0 nebo novější** ([LICENSE](LICENSE)). Součásti a licence: [THIRD_PARTY.md](THIRD_PARTY.md).
