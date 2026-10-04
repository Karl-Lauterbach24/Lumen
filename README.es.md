<p align="center"><img src="resources/logo/lumen-logo.png" width="300" alt="Lumen"></p>

# Lumen – Reproductor de discos y cine digital

[English](README.md) · [Deutsch](README.de.md) · [Français](README.fr.md) · **Español** · [Italiano](README.it.md) · [Português](README.pt.md) · [Nederlands](README.nl.md) · [Polski](README.pl.md) · [Svenska](README.sv.md) · [Čeština](README.cs.md) · [Türkçe](README.tr.md) · [Українська](README.uk.md) · [Русский](README.ru.md) · [日本語](README.ja.md) · [简体中文](README.zh.md) · [한국어](README.ko.md)

Un reproductor rápido y minimalista para cine en casa, salas de proyección y cines pequeños, con **dos ventanas**:

- **Ventana de reproducción** – ventana nativa de mpv (gpu-next, D3D11/Vulkan/Wayland), asignable a un dispositivo de salida, HDR passthrough.
- **Ventana de control** – fuente, transporte, títulos, capítulos, audio, subtítulos, imagen, cine, streaming, perfiles de salida y plugins.

Lumen reproduce **Blu-ray / UHD / Blu-ray 3D, DVD-Video con menús, HD DVD, Video CD, CD de audio, Digital Cinema Packages (DCP)**, transmisiones de red y todos los formatos de archivo que mpv puede reproducir.

<p align="center">
  <img src="docs/screenshots/start.png" width="49%" alt="Lumen">
  <img src="docs/screenshots/cinema.png" width="49%" alt="Lumen">
</p>

## Descargas

Los paquetes precompilados están en la [página de versiones](https://github.com/Karl-Lauterbach24/Lumen/releases):

| Sistema | Archivo |
|---|---|
| Windows 10/11 (x64, ARM64) | `Lumen-<version>-windows-x64.msi`, `…-windows-arm64.msi` (instalador) · `.zip` (portable) |
| macOS 15 (Apple Silicon, Intel) | `Lumen-<version>-macos-arm64.dmg`, `…-macos-x64.dmg` |
| Debian 13 (x86-64, ARM64) | `Lumen-<version>-linux-amd64.deb`, `…-linux-arm64.deb` |
| Fedora 44 (x86-64, ARM64) | `Lumen-<version>-linux-x86_64.rpm`, `…-linux-aarch64.rpm` |

Todos los paquetes contienen las mismas bibliotecas multimedia (FFmpeg con decodificador de Blu-ray 3D, libmpv); en Linux, Qt 6 y las bibliotecas de discos proceden de tu distribución. Lumen busca nuevas versiones y, en Windows y macOS, las instala con un clic tras comprobar la suma de verificación.

## Funciones

- **Discos:** Blu-ray y DVD con menús, títulos, capítulos, pistas de audio y subtítulos; las unidades se detectan automáticamente.
- **Cine digital:** DCP con JPEG 2000, SMPTE e Interop, paquetes cifrados con KDM, renderizado de Dolby Atmos/IAB, programas de proyección.
- **Streaming:** enlaces de todo tipo (HLS, DASH, RTSP, …) y los servidores multimedia Jellyfin, Emby y Plex.
- **Transmisión:** envía la imagen y el sonido a un televisor o receptor de la red: DLNA, Chromecast, AirPlay (receptores sin emparejamiento), las aplicaciones [Lumen TV](https://github.com/Karl-Lauterbach24/Lumen-TV) para Android TV, Samsung y LG, o cualquier navegador. Miracast funciona mediante el ajuste de pantalla inalámbrica del sistema.
- **3D:** Blu-ray 3D (MVC) como frame packing, lado a lado, arriba-abajo o anaglifo.
- **Perfiles de salida:** pantalla de destino, ajuste de la frecuencia, HDR passthrough o tone mapping, calibración (ICC, LUT 3D).
- **Audio:** bitstream a un receptor AV (TrueHD/Atmos, DTS-HD), modo nocturno, retardo y velocidad.
- **CD de audio:** nombres de las pistas desde CD-Text o, con el plugin *Disc identification*, desde MusicBrainz.
- **Uso diario:** lista de reproducidos recientemente con reanudación, arrastrar y soltar, archivos de subtítulos externos, atajos de teclado (F1).
- **Interfaz en 16 idiomas**, seleccionable en la cabecera de la ventana (símbolo del globo).

## Protección anticopia

Lumen **no** contiene ningún mecanismo para eludir la protección anticopia. Los discos protegidos (AACS, BD+, CSS) solo se reproducen si añades tú mismo las bibliotecas necesarias mediante un plugin; eres responsable de comprobar que sea legal en tu país.

## Plugins

Los plugins añaden fuentes, claves, scripts y funciones. La pestaña **Plugins** los instala desde la [tienda de plugins](https://github.com/Karl-Lauterbach24/Lumen-Plugins) o desde tus propias fuentes; cada archivo se comprueba con su suma de verificación.

## Más información

La compilación, la arquitectura, las pruebas, la línea de comandos y todos los atajos de teclado se describen en el [README en inglés](README.md). Esta traducción se hizo con ayuda automática; las correcciones son bienvenidas.

## Licencia

Lumen es software libre bajo la **GNU Affero General Public License v3.0 o posterior** ([LICENSE](LICENSE)). Componentes y licencias: [THIRD_PARTY.md](THIRD_PARTY.md).
