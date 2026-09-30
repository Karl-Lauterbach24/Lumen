# Lumen – Leitor de Blu-ray

[English](README.md) · [Deutsch](README.de.md) · [Français](README.fr.md) · [Español](README.es.md) · [Italiano](README.it.md) · **Português**

> **Novidade:** o Lumen agora reproduz também **DVD-Video (com menus), HD DVD, Video-CD/SVCD (incl. imagens CUE/BIN), CD de áudio** e **Digital Cinema Packages (DCP, JPEG 2000, SMPTE/Interop, encriptados com KDM)**, com a aba «Kino» (certificado do leitor, KDM, fader de cinema, encaminhamento de canais, programa de projeção) e perfis de calibração (ICC, LUT 3D, qualidade de referência). Detalhes: [README em inglês](README.md).

Leitor de Blu-ray / UHD / 3D rápido e minimalista para home theater, com **duas janelas**:

- **Janela de reprodução** – janela nativa do mpv (gpu-next, D3D11/Vulkan/Wayland), fixável num dispositivo de saída, HDR passthrough.
- **Janela de controle** – Qt Quick: fonte, transporte, títulos, capítulos, áudio, legendas, imagem, perfis de saída.

> A interface está atualmente em alemão.

## Proteção contra cópia / LibreDrive

O Lumen **não contorna nenhuma proteção contra cópia**. Os discos são lidos exclusivamente via `libbluray`.
Se um disco for protegido por AACS/BD+, isso já precisa estar resolvido fora do Lumen – por exemplo, uma unidade
com firmware LibreDrive e uma biblioteca AACS instalada pelo usuário, carregada pela libbluray em tempo de
execução. O mesmo vale para a segunda vista 3D: ela é lida via libbluray (`bd_open_file_dec`) e, portanto,
pela mesma biblioteca externa.

## Recursos

| Área | Escopo |
|---|---|
| Fontes | Unidades ópticas (detecção automática, fabricante/modelo/firmware, ejetar, reprodução automática ao inserir), ISO, pastas BDMV, qualquer formato reproduzível pelo mpv |
| Disco | Menus do disco (HDMV, BD-J com Java), filme principal, títulos/playlists com duração, formato de vídeo/áudio, detecção de UHD e 3D |
| **Blu-ray 3D** | **Ambas as vistas (MVC)**, saída como HDMI Frame Packing 1080p, lado a lado / em cima-embaixo (half/full), entrelaçado por linhas, anáglifo ou 2D; legendas e menus por olho com profundidade ajustável |
| Transporte | Reproduzir/pausar, parar, ±10 s/±60 s, capítulos, quadro a quadro, loop A-B, velocidade, barra com marcas de capítulo, captura de tela |
| Áudio | Seleção de faixa, bitstream (TrueHD/Atmos, DTS-HD MA/DTS:X, DD+, DD), WASAPI exclusivo, layout de canais, atraso de áudio |
| Legendas | Seleção de faixa (PGS/SRT/ASS), somente forçadas, atraso, tamanho, posição (telas cinemascope) |
| Imagem | Proporção, pan & scan, zoom, brilho/contraste/saturação/gama, desentrelaçamento, formato de origem 3D para arquivos |
| Perfis de saída | Dispositivo de destino, tela cheia, ajuste de taxa de atualização (23,976 → 23/24 Hz), HDR do sistema automático, HDR passthrough ou tone mapping, qualidade de escala, sincronização, formato de saída 3D, dispositivo de áudio, opções avançadas |

Predefinições: *Desktop*, *Projetor DLP 3D 1080p* (half SBS), *Projetor DLP 3D 1080p (Frame Packing)*,
*Projetor LED 4K HDR (passthrough)*, *Projetor LED 4K HDR (tone mapping do leitor)*, *TV 4K HDR (OLED)*, *Desempenho/Notebook*.

## Blu-ray 3D

```
libbluray bd_read_ext()  ── vista base (PID 0x1011) ───────┐
                                                            ├─ MvcMerger ─ TS ─> mpv ─> FFmpeg-mvc ─> imagem SBS
libbluray bd_open_file_dec() ─ vista dependente (0x1012) ──┘   (pareadas por PTS a cada quadro)          │
                                                                                                          v
                                        vf: stereo3d / grafo de frame packing ─> formato do perfil de saída
```

- **Decodificador:** o FFmpeg padrão decodifica apenas a vista base. O Lumen usa o
  [FFmpeg-mvc](https://github.com/tthayer93/FFmpeg-mvc) (branch `release/9.0`), que decodifica as duas vistas
  numa única imagem lado a lado. Suas DLLs têm os mesmos nomes/ABI do FFmpeg 9.0 e substituem uma a uma as
  bibliotecas da libmpv. O Lumen reconhece o decodificador pela string de versão (`…-mvc`).
- **Segunda vista:** a libbluray entrega apenas a vista base. O `MvcMerger` lê o subcaminho SS da playlist
  (qual clipe), o mapa EP do CLPI (pontos de salto) e o `.m2ts` dependente via libbluray, pareia as unidades
  de acesso por PTS e anexa as NALs MVC às NALs da vista base.
- **A reprodução 3D sempre passa pela libbluray** (`lumenbd://`), incluindo "filme principal" e a escolha de títulos.
  O disco recebe "3D preferido" (PSR21/23) para que os menus escolham a playlist 3D.
- **Legendas/menus:** a libbluray renderiza as legendas PG e os gráficos de menu; o Lumen os desenha uma vez
  por olho (profundidade ajustável no perfil ou na aba "Untertitel").
- **Frame Packing (HDMI 1.4):** 1920×2205 (1080 + 45 + 1080 linhas) a 23,976 Hz. O modo de vídeo precisa ser
  criado como resolução personalizada no driver gráfico; depois o Lumen alterna para ele automaticamente.
  Muitos projetores reconhecem frame packing pelo timing; caso contrário, use SBS/TAB.
- Não existe decodificação por hardware para MVC (nenhuma GPU oferece suporte); discos 3D detectados são
  decodificados por software (AVC 1080p24, sem problema para CPUs atuais).

## Arquitetura

```
src/MpvController   instância libmpv, observação de propriedades por eventos, perfis -> opções do mpv
src/BlurayNav       libbluray como stream do mpv "lumenbd://": menus, títulos, 3D, overlays por olho
src/MvcMerger       Blu-ray 3D: adiciona a vista dependente (subcaminho SS, mapa EP, pareamento por PTS)
src/DisplayManager  dispositivos de saída, modo de taxa / HDR / frame packing (Windows, xrandr, kscreen-doctor, CoreGraphics)
src/DriveManager    unidades/discos (thread de trabalho), ejeção
src/DiscScanner     libbluray: títulos, durações, estado AACS/BD+/BD-J/3D (thread de trabalho)
src/PlayerWindow    janela de reprodução embutida (API de renderização do mpv/OpenGL), principalmente para macOS
src/ProfileManager  predefinições + perfis do usuário
qml/                janela de controle
tools/              scripts de build/implantação, gerador de disco de teste
tests/              mvcmerge_test (fusão MVC contra uma estrutura de disco)
```

## Compilação

Requisitos: CMake ≥ 3.21, Qt ≥ 6.5 (Quick, QuickControls2, OpenGL), libmpv ≥ 0.38 **vinculada ao FFmpeg como
bibliotecas compartilhadas**, libbluray ≥ 1.2, FFmpeg-mvc na mesma versão principal de FFmpeg usada pela libmpv.

### Windows (MSYS2 UCRT64 – testado: GCC 16, Qt 6.11, mpv 0.41, libbluray 1.5, FFmpeg-mvc 9.0.2)

Todas as partes precisam usar o mesmo runtime C (UCRT) – por isso Qt, libmpv e libbluray vêm do MSYS2.
As ferramentas ficam propositalmente num **caminho curto** (`C:\lumen-build`), senão o GCC e o build do
FFmpeg falham no limite de 260 caracteres do Windows.

```bash
# 1. Pacotes (sem instalar o MSYS2, apenas extração)
python tools/msys2_fetch.py --repo ucrt64 --dest C:/lumen-build/msys2 mpv qt6-base qt6-declarative qt6-svg qt6-tools gcc nasm pkgconf dav1d libva cmake ninja --skip mingw-w64-ucrt-x86_64-ffmpeg
python tools/msys2_fetch.py --repo msys --dest C:/lumen-build/msys2-tools make diffutils --skip msys2-runtime bash
# 2. FFmpeg-mvc (Git Bash, ~20 min) -> 3rdparty/ffmpeg-mvc
tools/build_ffmpeg_mvc.sh
# 3. Lumen (a implantação roda após o build: windeployqt + DLLs, FFmpeg-mvc com prioridade)
U=C:/lumen-build/msys2/ucrt64; PATH=$U/bin:$PATH
cmake -S . -B build-ucrt -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$U -DMPV_ROOT=$U -DBLURAY_ROOT=$U -DLUMEN_DEPLOY_SEARCH=$U/bin
cmake --build build-ucrt
```

### Linux

```bash
sudo apt install qt6-declarative-dev qml6-module-qtquick-dialogs qml6-module-qtcore libbluray-dev nasm libdav1d-dev
tools/build_ffmpeg_mvc.sh            # FFMPEG_MVC_BRANCH=release/8.1 para FFmpeg 8.x
```
A libmpv precisa ser compilada com **a mesma versão principal do FFmpeg** (`ldd $(which mpv) | grep avcodec`).
Se a distribuição não corresponder, compile o mpv contra `3rdparty/ffmpeg-mvc` (`meson setup -Dlibmpv=true`,
`PKG_CONFIG_PATH=3rdparty/ffmpeg-mvc/lib/pkgconfig`). Em execução: `LD_LIBRARY_PATH=3rdparty/ffmpeg-mvc/lib`.

### macOS

`brew install qt mpv libbluray libdvdnav libcdio openssl@3 libxml2 pkgconf ninja`, depois
`cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=$(brew --prefix qt)`, `cmake --build build` (Lumen.app) e
`cmake --build build --target lumen_dmg` (Lumen.app autônomo + Lumen.dmg). O workflow `.github/workflows/macos.yml`
compila e testa no macOS 15. Blu-ray 3D: `brew install nasm dav1d`, FFmpeg-mvc como no Linux (branch correspondente ao FFmpeg do Homebrew),
`DYLD_LIBRARY_PATH` apontando para `3rdparty/ffmpeg-mvc/lib`. No macOS a janela de reprodução é embutida automaticamente.

### Sem 3D

Qualquer libmpv funciona (por exemplo, o SDK shinchiro com `-DMPV_ROOT=…`); o Blu-ray 3D então é reproduzido
em 2D e a interface avisa ("Kein MVC-Decoder").

## Testes

```bash
# Disco 3D sintético a partir de um stream MVC de teste (fixture do FFmpeg-mvc: olho esquerdo luma 165, direito 36)
perl <ffmpeg-mvc>/tests/fate/h264-mvc/mvc-mkfix.pl --out=mvc8.h264 --base=100 --dep=-100 --base-frames=8 --dep-frames=8
python tools/make_test_bd3d.py mvc8.h264 bd3d
# Verificar a fusão (cmake -DLUMEN_BUILD_TESTS=ON)
mvcmerge_test bd3d 0 merged.m2ts
ffmpeg -view_ids -1 -i merged.m2ts -fps_mode passthrough -f rawvideo -pix_fmt gray - | od -An -tu1 -w512 -v | awk '{print $1","$17}' | sort | uniq -c
#   -> 8 165,36  (cada quadro: vista base à esquerda, vista dependente à direita)
# No Lumen com um perfil 3D:  lumen bd3d
```

## Linha de comando

```bash
lumen D:\                      # unidade: filme principal diretamente (em 3D com perfil 3D)
lumen --menu D:\               # unidade com menu do disco (p. ex. para lançadores de HTPC)
lumen --menu Filme.iso         # ISO / pasta BDMV com menu
lumen filme.mkv                # qualquer arquivo reproduzível pelo mpv
```

## Teclado

| Tecla | Janela de controle | Janela de reprodução |
|---|---|---|
| Espaço | Reproduzir/pausar | Reproduzir/pausar |
| ← / → (Shift) | ±10 s (±60 s), nos menus: navegar | ±10 s, nos menus: navegar |
| ↑ / ↓ | Volume, nos menus: navegar | ±60 s, nos menus: navegar |
| Enter / clique | Nos menus: confirmar | Nos menus: confirmar; senão Enter = tela cheia |
| Home / End | Menu principal / menu pop-up | Menu principal / menu pop-up |
| Page Up / Down | Próximo / anterior capítulo | Próximo / anterior capítulo |
| , / . | Quadro a quadro | Quadro a quadro |
| F / clique duplo | Tela cheia | Tela cheia (Enter / clique duplo) |
| L | Loop A-B | Loop A-B |
| S | Captura de tela | Captura de tela |
| I | Estatísticas | Estatísticas |
| [ / ] / ⌫ | Velocidade ∓ / redefinir | |
| Ctrl+O / Ctrl+E | Abrir / ejetar | |

## Menus do disco

- A libbluray executa o programa de menu do disco e entrega o stream ao mpv via `lumenbd://`;
  os gráficos de menu (IG ou BD-J) chegam como overlay ARGB escalado para a área do vídeo (por olho no modo 3D).
- Operação: setas/Enter/mouse na janela de reprodução, direcional na janela de controle, Home = menu principal, End = pop-up.
- As escolhas de áudio/legenda feitas no menu do disco são transferidas pelo PID do stream para a faixa correspondente.
- **Menus BD-J** exigem um runtime Java (JRE ≥ 8) e `libbluray-j2se-*.jar`; sem eles, o modo de títulos continua disponível.

## Limitações / estado

| Tema | Estado |
|---|---|
| Blu-ray 3D | Implementado e testado de ponta a ponta com um disco 3D sintético (fusão, salto, SBS, frame packing). **Ainda não testado com um disco 3D real**; o mesmo vale para legendas/menus 3D (o disco de teste não tem PG/IG). O FFmpeg-mvc é um fork experimental. |
| Frame packing | Requer um modo 1920×2205 no driver gráfico; se o projetor o reconhece como 3D sem InfoFrame HDMI 3D depende do aparelho. |
| Dolby Vision | Detectado (perfil 5/7/8), o gpu-next aplica os metadados RPU; nenhum leitor de PC consegue gerar um sinal DV real via HDMI. |
| Menus de imagem estática | Menus puramente estáticos podem ficar pretos por um instante (latência do decodificador). |
| Troca de taxa/HDR | Windows: taxa + HDR · Linux X11: taxa (xrandr) · KDE Plasma: taxa + HDR · GNOME Wayland: somente exibição · macOS: taxa |
| Janela de reprodução embutida | Automática no macOS, caso contrário por perfil; API de renderização OpenGL → somente SDR. |

Ajuda para desenvolvimento: `LUMEN_SNAPSHOT=shot.png` (opcional `LUMEN_SNAPSHOT_DELAY=ms`) salva a janela de controle como imagem.
