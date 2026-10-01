<p align="center"><img src="resources/logo/lumen-logo.png" width="300" alt="Lumen"></p>

# Lumen – Disk ve dijital sinema oynatıcısı

[English](README.md) · [Deutsch](README.de.md) · [Français](README.fr.md) · [Español](README.es.md) · [Italiano](README.it.md) · [Português](README.pt.md) · [Nederlands](README.nl.md) · [Polski](README.pl.md) · [Svenska](README.sv.md) · [Čeština](README.cs.md) · **Türkçe** · [Українська](README.uk.md) · [Русский](README.ru.md) · [日本語](README.ja.md) · [简体中文](README.zh.md) · [한국어](README.ko.md)

Ev sinemaları, gösterim odaları ve küçük sinemalar için hızlı, yalın bir oynatıcı; **iki pencereli**:

- **Oynatıcı penceresi** – yerel mpv penceresi (gpu-next, D3D11/Vulkan/Wayland), bir çıkış aygıtına sabitlenebilir, HDR geçişi.
- **Denetim penceresi** – kaynak, oynatma denetimleri, başlıklar, bölümler, ses, altyazı, görüntü, sinema, yayın, çıkış profilleri ve eklentiler.

Lumen **Blu-ray / UHD / Blu-ray 3D, menülü DVD-Video, HD DVD, Video CD, ses CD'si, Digital Cinema Package (DCP)**, yayınlar ve mpv'nin oynatabildiği tüm dosya biçimlerini oynatır.

<p align="center">
  <img src="docs/screenshots/start.png" width="49%" alt="Lumen">
  <img src="docs/screenshots/cinema.png" width="49%" alt="Lumen">
</p>

## İndirmeler

Derlenmiş paketler [sürümler sayfasındadır](https://github.com/Karl-Lauterbach24/Lumen/releases):

| Sistem | Dosya |
|---|---|
| Windows 10/11 (x64, ARM64) | `Lumen-<version>-windows-x64.msi`, `…-windows-arm64.msi` (kurulum programı) · `.zip` (taşınabilir) |
| macOS 15 (Apple Silicon, Intel) | `Lumen-<version>-macos-arm64.dmg`, `…-macos-x64.dmg` |
| Debian 13 (x86-64, ARM64) | `Lumen-<version>-linux-amd64.deb`, `…-linux-arm64.deb` |
| Fedora 44 (x86-64, ARM64) | `Lumen-<version>-linux-x86_64.rpm`, `…-linux-aarch64.rpm` |

Tüm paketler aynı medya kitaplıklarını içerir (Blu-ray 3D kod çözücülü FFmpeg, libmpv); Linux'ta Qt 6 ve disk kitaplıkları dağıtımınızdan gelir. Lumen yeni sürümleri denetler; Windows ve macOS'ta sağlama toplamını doğruladıktan sonra tek tıkla kurar.

## Özellikler

- **Diskler:** disk menüleri, başlıklar, bölümler, ses ve altyazı parçalarıyla Blu-ray ve DVD; sürücüler otomatik algılanır.
- **Dijital sinema:** JPEG 2000, SMPTE ve Interop DCP, KDM ile şifreli paketler, Dolby Atmos/IAB işleme, gösterim programları.
- **Yayın:** her türden bağlantı (HLS, DASH, RTSP, …) ve Jellyfin, Emby, Plex medya sunucuları.
- **Yayın:** görüntüyü ve sesi ağdaki bir televizyona veya alıcıya gönderir: DLNA, Chromecast, AirPlay (eşleştirme istemeyen alıcılar), Android TV, Samsung ve LG için [Lumen TV](https://github.com/Karl-Lauterbach24/Lumen-TV) uygulamaları veya herhangi bir tarayıcı. Miracast, sistemin kablosuz ekran ayarı üzerinden çalışır.
- **3D:** Blu-ray 3D (MVC); frame packing, yan yana, üst-alt veya anaglif olarak.
- **Çıkış profilleri:** hedef ekran, yenileme hızı eşleme, HDR geçişi veya ton eşleme, kalibrasyon (ICC, 3D LUT).
- **Ses:** AV alıcısına bitstream (TrueHD/Atmos, DTS-HD), gece kipi, gecikme ve hız.
- **Ses CD'si:** parça adları CD-Text'ten veya *Disc identification* eklentisiyle MusicBrainz'den.
- **Günlük kullanım:** kaldığı yerden sürdürmeli son oynatılanlar listesi, sürükle-bırak, harici altyazı dosyaları, klavye kısayolları (F1).
- **16 dilde arayüz**, başlangıç sayfasından seçilir.

## Kopya koruması

Lumen kopya korumasını aşan **hiçbir** şey içermez. Korumalı diskler (AACS, BD+, CSS) yalnızca gerekli kitaplıkları bir eklentiyle kendiniz eklerseniz oynar; bunun ülkenizde yasal olmasından siz sorumlusunuz.

## Eklentiler

Eklentiler kaynaklar, anahtarlar, betikler ve işlevler ekler. **Eklentiler** sekmesi bunları [eklenti mağazasından](https://github.com/Karl-Lauterbach24/Lumen-Plugins) veya kendi kaynaklarınızdan kurar; her dosya sağlama toplamıyla denetlenir.

## Daha fazlası

Kaynaktan derleme, mimari, testler, komut satırı ve tüm klavye kısayolları [İngilizce README](README.md) dosyasında anlatılır. Bu çeviri makine yardımıyla yapılmıştır; düzeltmeler memnuniyetle karşılanır.

## Lisans

Lumen, **GNU Affero General Public License v3.0 veya sonrası** kapsamında özgür yazılımdır ([LICENSE](LICENSE)). Bileşenler ve lisansları: [THIRD_PARTY.md](THIRD_PARTY.md).
