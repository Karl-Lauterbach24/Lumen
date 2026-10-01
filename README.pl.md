<p align="center"><img src="resources/logo/lumen-logo.png" width="300" alt="Lumen"></p>

# Lumen – Odtwarzacz płyt i kina cyfrowego

[English](README.md) · [Deutsch](README.de.md) · [Français](README.fr.md) · [Español](README.es.md) · [Italiano](README.it.md) · [Português](README.pt.md) · [Nederlands](README.nl.md) · **Polski** · [Svenska](README.sv.md) · [Čeština](README.cs.md) · [Türkçe](README.tr.md) · [Українська](README.uk.md) · [Русский](README.ru.md) · [日本語](README.ja.md) · [简体中文](README.zh.md) · [한국어](README.ko.md)

Szybki, minimalistyczny odtwarzacz do kina domowego, sal projekcyjnych i małych kin, z **dwoma oknami**:

- **Okno odtwarzacza** – natywne okno mpv (gpu-next, D3D11/Vulkan/Wayland), które można przypiąć do urządzenia wyjściowego, HDR passthrough.
- **Okno sterowania** – źródło, sterowanie odtwarzaniem, tytuły, rozdziały, dźwięk, napisy, obraz, kino, streaming, profile wyjścia i wtyczki.

Lumen odtwarza **Blu-ray / UHD / Blu-ray 3D, DVD-Video z menu, HD DVD, Video CD, płyty audio CD, Digital Cinema Packages (DCP)**, strumienie oraz każdy format pliku obsługiwany przez mpv.

<p align="center">
  <img src="docs/screenshots/start.png" width="49%" alt="Lumen">
  <img src="docs/screenshots/cinema.png" width="49%" alt="Lumen">
</p>

## Pobieranie

Gotowe pakiety znajdują się na [stronie wydań](https://github.com/Karl-Lauterbach24/Lumen/releases):

| System | Plik |
|---|---|
| Windows 10/11 (x64, ARM64) | `Lumen-<version>-windows-x64.msi`, `…-windows-arm64.msi` (instalator) · `.zip` (wersja przenośna) |
| macOS 15 (Apple Silicon, Intel) | `Lumen-<version>-macos-arm64.dmg`, `…-macos-x64.dmg` |
| Debian 13 (x86-64, ARM64) | `Lumen-<version>-linux-amd64.deb`, `…-linux-arm64.deb` |
| Fedora 44 (x86-64, ARM64) | `Lumen-<version>-linux-x86_64.rpm`, `…-linux-aarch64.rpm` |

Wszystkie pakiety zawierają te same biblioteki multimedialne (FFmpeg z dekoderem Blu-ray 3D, libmpv); w Linuksie Qt 6 i biblioteki do płyt pochodzą z dystrybucji. Lumen sprawdza dostępność nowych wersji, a w systemach Windows i macOS instaluje je jednym kliknięciem po weryfikacji sumy kontrolnej.

## Funkcje

- **Płyty:** Blu-ray i DVD z menu, tytuły, rozdziały, ścieżki dźwiękowe i napisy; napędy są wykrywane automatycznie.
- **Kino cyfrowe:** DCP z JPEG 2000, SMPTE i Interop, zaszyfrowane pakiety z KDM, renderowanie Dolby Atmos/IAB, programy pokazów.
- **Streaming:** odnośniki wszelkiego rodzaju (HLS, DASH, RTSP, …) oraz serwery multimediów Jellyfin, Emby i Plex.
- **Przesyłanie:** wysyła obraz i dźwięk do telewizora lub odbiornika w sieci: DLNA, Chromecast, AirPlay (odbiorniki bez parowania), aplikacje [Lumen TV](https://github.com/Karl-Lauterbach24/Lumen-TV) na Android TV, telewizory Samsung i LG albo dowolna przeglądarka. Miracast działa przez systemowe ustawienie ekranu bezprzewodowego.
- **3D:** Blu-ray 3D (MVC) jako frame packing, obok siebie, góra-dół lub anaglif.
- **Profile wyjścia:** ekran docelowy, dopasowanie częstotliwości odświeżania, HDR passthrough lub mapowanie tonów, kalibracja (ICC, LUT 3D).
- **Dźwięk:** bitstream do amplitunera AV (TrueHD/Atmos, DTS-HD), tryb nocny, opóźnienie i prędkość.
- **Audio CD:** nazwy utworów z CD-Text lub, dzięki wtyczce *Disc identification*, z MusicBrainz.
- **Na co dzień:** lista ostatnio odtwarzanych ze wznawianiem, przeciąganie i upuszczanie, zewnętrzne pliki napisów, skróty klawiszowe (F1).
- **Interfejs w 16 językach**, do wyboru na stronie startowej.

## Zabezpieczenia przed kopiowaniem

Lumen **nie** zawiera żadnych mechanizmów obchodzenia zabezpieczeń przed kopiowaniem. Zabezpieczone płyty (AACS, BD+, CSS) są odtwarzane tylko wtedy, gdy samodzielnie dodasz potrzebne biblioteki za pomocą wtyczki; odpowiadasz za to, czy jest to zgodne z prawem w Twoim kraju.

## Wtyczki

Wtyczki dodają źródła, klucze, skrypty i funkcje. Karta **Wtyczki** instaluje je ze [sklepu z wtyczkami](https://github.com/Karl-Lauterbach24/Lumen-Plugins) lub z własnych źródeł; każdy plik jest sprawdzany na podstawie sumy kontrolnej.

## Więcej

Kompilację ze źródeł, architekturę, testy, wiersz poleceń i wszystkie skróty klawiszowe opisano w [angielskim README](README.md). To tłumaczenie powstało z pomocą maszynową; poprawki są mile widziane.

## Licencja

Lumen jest wolnym oprogramowaniem na licencji **GNU Affero General Public License v3.0 lub nowszej** ([LICENSE](LICENSE)). Komponenty i licencje: [THIRD_PARTY.md](THIRD_PARTY.md).
