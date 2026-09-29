# Lumen – Reproductor Blu-ray

[English](README.md) · [Deutsch](README.de.md) · [Français](README.fr.md) · **Español** · [Italiano](README.it.md) · [Português](README.pt.md)

Reproductor Blu-ray / UHD / 3D rápido y minimalista para cine en casa, con **dos ventanas**:

- **Ventana de reproducción** – ventana nativa de mpv (gpu-next, D3D11/Vulkan/Wayland), asignable a un dispositivo de salida, HDR passthrough.
- **Ventana de control** – Qt Quick: fuente, transporte, títulos, capítulos, audio, subtítulos, imagen, perfiles de salida.

> La interfaz está actualmente en alemán.

## Protección anticopia / LibreDrive

Lumen **no elude ninguna protección anticopia**. Los discos se leen exclusivamente a través de `libbluray`.
Si un disco está protegido con AACS/BD+, esto debe resolverse fuera de Lumen – p. ej. una unidad con
firmware LibreDrive más una biblioteca AACS instalada por el usuario que libbluray carga en tiempo de
ejecución. Lo mismo vale para la segunda vista 3D: se lee mediante libbluray (`bd_open_file_dec`) y, por
tanto, a través de la misma biblioteca externa.

## Funciones

| Área | Alcance |
|---|---|
| Fuentes | Unidades ópticas (detección automática, fabricante/modelo/firmware, expulsar, reproducción automática al insertar), ISO, carpetas BDMV, cualquier formato que reproduzca mpv |
| Disco | Menús del disco (HDMV, BD-J con Java), película principal, títulos/playlists con duración, formato de vídeo/audio, detección UHD y 3D |
| **Blu-ray 3D** | **Ambas vistas (MVC)**, salida como HDMI Frame Packing 1080p, lado a lado / arriba-abajo (half/full), entrelazado por líneas, anaglifo o 2D; subtítulos y menús por ojo con profundidad ajustable |
| Transporte | Reproducir/pausa, detener, ±10 s/±60 s, capítulos, fotograma a fotograma, bucle A-B, velocidad, barra con marcas de capítulo, captura de pantalla |
| Audio | Selección de pista, bitstream (TrueHD/Atmos, DTS-HD MA/DTS:X, DD+, DD), WASAPI exclusivo, disposición de canales, retardo de audio |
| Subtítulos | Selección de pista (PGS/SRT/ASS), solo forzados, retardo, tamaño, posición (pantallas cinemascope) |
| Imagen | Relación de aspecto, pan & scan, zoom, brillo/contraste/saturación/gamma, desentrelazado, formato de origen 3D para archivos |
| Perfiles de salida | Dispositivo de destino, pantalla completa, ajuste de frecuencia (23,976 → 23/24 Hz), HDR del sistema automático, HDR passthrough o tone mapping, calidad de escalado, sincronización, formato de salida 3D, dispositivo de audio, opciones avanzadas |

Perfiles incluidos: *Desktop*, *Proyector DLP 3D 1080p* (half SBS), *Proyector DLP 3D 1080p (Frame Packing)*,
*Proyector LED 4K HDR (passthrough)*, *Proyector LED 4K HDR (tone mapping del reproductor)*, *TV 4K HDR (OLED)*, *Rendimiento/Portátil*.

## Blu-ray 3D

```
libbluray bd_read_ext()  ── vista base (PID 0x1011) ────────┐
                                                             ├─ MvcMerger ─ TS ─> mpv ─> FFmpeg-mvc ─> imagen SBS
libbluray bd_open_file_dec() ─ vista dependiente (0x1012) ──┘   (emparejadas por PTS en cada fotograma)   │
                                                                                                           v
                                       vf: stereo3d / grafo de frame packing ─> formato del perfil de salida
```

- **Decodificador:** FFmpeg estándar solo decodifica la vista base. Lumen usa
  [FFmpeg-mvc](https://github.com/tthayer93/FFmpeg-mvc) (rama `release/9.0`), que decodifica ambas vistas en
  una imagen lado a lado. Sus DLL tienen los mismos nombres/ABI que FFmpeg 9.0 y sustituyen una a una las
  bibliotecas de libmpv. Lumen detecta el decodificador por su cadena de versión (`…-mvc`).
- **Segunda vista:** libbluray solo entrega la vista base. `MvcMerger` lee el subtrayecto SS de la playlist
  (qué clip), el mapa EP del CLPI (puntos de salto) y el `.m2ts` dependiente mediante libbluray, empareja las
  unidades de acceso por PTS y añade las NAL MVC a las NAL de la vista base.
- **La reproducción 3D siempre pasa por libbluray** (`lumenbd://`), incluidos «película principal» y la selección de títulos.
  Al disco se le indica «3D preferido» (PSR21/23) para que los menús elijan la playlist 3D.
- **Subtítulos/menús:** libbluray renderiza los subtítulos PG y los gráficos de menú; Lumen los dibuja una vez
  por ojo (profundidad ajustable en el perfil o en la pestaña «Untertitel»).
- **Frame Packing (HDMI 1.4):** 1920×2205 (1080 + 45 + 1080 líneas) a 23,976 Hz. El modo de pantalla debe
  crearse como resolución personalizada en el controlador gráfico; después Lumen cambia a él automáticamente.
  Muchos proyectores detectan el frame packing por el timing; si no, use SBS/TAB.
- No existe decodificación por hardware para MVC (ninguna GPU lo admite); los discos 3D detectados se
  decodifican por software (AVC 1080p24, sin problema para las CPU actuales).

## Arquitectura

```
src/MpvController   instancia libmpv, observación de propiedades por eventos, perfiles -> opciones de mpv
src/BlurayNav       libbluray como flujo mpv «lumenbd://»: menús, títulos, 3D, overlays por ojo
src/MvcMerger       Blu-ray 3D: añade la vista dependiente (subtrayecto SS, mapa EP, emparejamiento PTS)
src/DisplayManager  dispositivos de salida, modo de frecuencia / HDR / frame packing (Windows, xrandr, kscreen-doctor, CoreGraphics)
src/DriveManager    unidades/discos (hilo de trabajo), expulsión
src/DiscScanner     libbluray: títulos, duraciones, estado AACS/BD+/BD-J/3D (hilo de trabajo)
src/PlayerWindow    ventana de reproducción integrada (API de renderizado de mpv/OpenGL), sobre todo para macOS
src/ProfileManager  perfiles predefinidos + perfiles de usuario
qml/                ventana de control
tools/              scripts de compilación/despliegue, generador de disco de prueba
tests/              mvcmerge_test (fusión MVC contra una estructura de disco)
```

## Compilación

Requisitos: CMake ≥ 3.21, Qt ≥ 6.5 (Quick, QuickControls2, OpenGL), libmpv ≥ 0.38 **enlazado con FFmpeg como
bibliotecas compartidas**, libbluray ≥ 1.2, FFmpeg-mvc de la misma versión mayor de FFmpeg que usa libmpv.

### Windows (MSYS2 UCRT64 – probado: GCC 16, Qt 6.11, mpv 0.41, libbluray 1.5, FFmpeg-mvc 9.0.2)

Todas las partes deben usar el mismo runtime de C (UCRT); por eso Qt, libmpv y libbluray proceden de MSYS2.
Las herramientas están a propósito en una **ruta corta** (`C:\lumen-build`), porque de lo contrario GCC y la
compilación de FFmpeg fallan por el límite de 260 caracteres de Windows.

```bash
# 1. Paquetes (sin instalar MSYS2, solo se extraen)
python tools/msys2_fetch.py --repo ucrt64 --dest C:/lumen-build/msys2 mpv qt6-base qt6-declarative qt6-svg qt6-tools gcc nasm pkgconf dav1d libva cmake ninja --skip mingw-w64-ucrt-x86_64-ffmpeg
python tools/msys2_fetch.py --repo msys --dest C:/lumen-build/msys2-tools make diffutils --skip msys2-runtime bash
# 2. FFmpeg-mvc (Git Bash, ~20 min) -> 3rdparty/ffmpeg-mvc
tools/build_ffmpeg_mvc.sh
# 3. Lumen (el despliegue se ejecuta tras compilar: windeployqt + DLL, FFmpeg-mvc con prioridad)
U=C:/lumen-build/msys2/ucrt64; PATH=$U/bin:$PATH
cmake -S . -B build-ucrt -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$U -DMPV_ROOT=$U -DBLURAY_ROOT=$U -DLUMEN_DEPLOY_SEARCH=$U/bin
cmake --build build-ucrt
```

### Linux

```bash
sudo apt install qt6-declarative-dev qml6-module-qtquick-dialogs qml6-module-qtcore libbluray-dev nasm libdav1d-dev
tools/build_ffmpeg_mvc.sh            # FFMPEG_MVC_BRANCH=release/8.1 para FFmpeg 8.x
```
libmpv debe estar compilado con **la misma versión mayor de FFmpeg** (`ldd $(which mpv) | grep avcodec`).
Si su distribución no coincide, compile mpv contra `3rdparty/ffmpeg-mvc` (`meson setup -Dlibmpv=true`,
`PKG_CONFIG_PATH=3rdparty/ffmpeg-mvc/lib/pkgconfig`). En ejecución: `LD_LIBRARY_PATH=3rdparty/ffmpeg-mvc/lib`.

### macOS

`brew install qt mpv libbluray nasm dav1d`, FFmpeg-mvc como en Linux (rama acorde al FFmpeg de Homebrew),
`DYLD_LIBRARY_PATH` apuntando a `3rdparty/ffmpeg-mvc/lib`. En macOS la ventana de reproducción se integra automáticamente.

### Sin 3D

Cualquier libmpv funciona (p. ej. el SDK de shinchiro con `-DMPV_ROOT=…`); el Blu-ray 3D se reproduce entonces
en 2D y la interfaz lo indica («Kein MVC-Decoder»).

## Pruebas

```bash
# Disco 3D sintético a partir de un flujo MVC de prueba (fixture de FFmpeg-mvc: ojo izquierdo luma 165, derecho 36)
perl <ffmpeg-mvc>/tests/fate/h264-mvc/mvc-mkfix.pl --out=mvc8.h264 --base=100 --dep=-100 --base-frames=8 --dep-frames=8
python tools/make_test_bd3d.py mvc8.h264 bd3d
# Comprobar la fusión (cmake -DLUMEN_BUILD_TESTS=ON)
mvcmerge_test bd3d 0 merged.m2ts
ffmpeg -view_ids -1 -i merged.m2ts -fps_mode passthrough -f rawvideo -pix_fmt gray - | od -An -tu1 -w512 -v | awk '{print $1","$17}' | sort | uniq -c
#   -> 8 165,36  (cada fotograma: vista base a la izquierda, vista dependiente a la derecha)
# En Lumen con un perfil 3D:  lumen bd3d
```

## Línea de comandos

```bash
lumen D:\                      # unidad: película principal directamente (en 3D con perfil 3D)
lumen --menu D:\               # unidad con menú del disco (p. ej. para lanzadores HTPC)
lumen --menu Pelicula.iso      # ISO / carpeta BDMV con menú
lumen pelicula.mkv             # cualquier archivo que reproduzca mpv
```

## Teclado

| Tecla | Ventana de control | Ventana de reproducción |
|---|---|---|
| Espacio | Reproducir/pausa | Reproducir/pausa |
| ← / → (Mayús) | ±10 s (±60 s), en menús: navegar | ±10 s, en menús: navegar |
| ↑ / ↓ | Volumen, en menús: navegar | ±60 s, en menús: navegar |
| Intro / clic | En menús: confirmar | En menús: confirmar; si no, Intro = pantalla completa |
| Inicio / Fin | Menú principal / menú emergente | Menú principal / menú emergente |
| Re Pág / Av Pág | Capítulo siguiente / anterior | Capítulo siguiente / anterior |
| , / . | Fotograma a fotograma | Fotograma a fotograma |
| F / doble clic | Pantalla completa | Pantalla completa (Intro / doble clic) |
| L | Bucle A-B | Bucle A-B |
| S | Captura | Captura |
| I | Estadísticas | Estadísticas |
| [ / ] / ⌫ | Velocidad ∓ / restablecer | |
| Ctrl+O / Ctrl+E | Abrir / expulsar | |

## Menús del disco

- libbluray ejecuta el programa de menú del disco y entrega el flujo a mpv mediante `lumenbd://`;
  los gráficos de menú (IG o BD-J) llegan como overlay ARGB escalado al área de vídeo (por ojo en modo 3D).
- Manejo: flechas/Intro/ratón en la ventana de reproducción, cruceta en la ventana de control, Inicio = menú principal, Fin = emergente.
- Las elecciones de audio/subtítulos del menú del disco se trasladan mediante el PID del flujo a la pista correspondiente.
- **Los menús BD-J** requieren un runtime de Java (JRE ≥ 8) y `libbluray-j2se-*.jar`; sin ellos queda disponible el modo de títulos.

## Limitaciones / estado

| Tema | Estado |
|---|---|
| Blu-ray 3D | Implementado y probado de extremo a extremo con un disco 3D sintético (fusión, salto, SBS, frame packing). **Aún no probado con un disco 3D real**; lo mismo para subtítulos/menús 3D (el disco de prueba no tiene PG/IG). FFmpeg-mvc es un fork experimental. |
| Frame packing | Requiere un modo 1920×2205 en el controlador gráfico; que el proyector lo reconozca como 3D sin InfoFrame HDMI 3D depende del dispositivo. |
| Dolby Vision | Se detecta (perfil 5/7/8), gpu-next aplica los metadatos RPU; ningún reproductor de PC puede emitir una señal DV real por HDMI. |
| Menús de imagen fija | Los menús puramente estáticos pueden quedar en negro un instante (latencia del decodificador). |
| Cambio de frecuencia/HDR | Windows: frecuencia + HDR · Linux X11: frecuencia (xrandr) · KDE Plasma: frecuencia + HDR · GNOME Wayland: solo visualización · macOS: frecuencia |
| Ventana de reproducción integrada | Automática en macOS, en otros casos por perfil; API de renderizado OpenGL → solo SDR. |

Ayuda para desarrollo: `LUMEN_SNAPSHOT=shot.png` (opcional `LUMEN_SNAPSHOT_DELAY=ms`) guarda la ventana de control como imagen.
