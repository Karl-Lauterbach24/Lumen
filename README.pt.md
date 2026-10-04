<p align="center"><img src="resources/logo/lumen-logo.png" width="300" alt="Lumen"></p>

# Lumen – Leitor de discos e de cinema digital

[English](README.md) · [Deutsch](README.de.md) · [Français](README.fr.md) · [Español](README.es.md) · [Italiano](README.it.md) · **Português** · [Nederlands](README.nl.md) · [Polski](README.pl.md) · [Svenska](README.sv.md) · [Čeština](README.cs.md) · [Türkçe](README.tr.md) · [Українська](README.uk.md) · [Русский](README.ru.md) · [日本語](README.ja.md) · [简体中文](README.zh.md) · [한국어](README.ko.md)

Um leitor rápido e minimalista para cinema em casa, salas de projeção e pequenos cinemas, com **duas janelas**:

- **Janela de reprodução** – janela mpv nativa (gpu-next, D3D11/Vulkan/Wayland), atribuível a um dispositivo de saída, HDR passthrough.
- **Janela de controlo** – fonte, transporte, títulos, capítulos, áudio, legendas, imagem, cinema, streaming, perfis de saída e plugins.

O Lumen reproduz **Blu-ray / UHD / Blu-ray 3D, DVD-Video com menus, HD DVD, Video CD, CD de áudio, Digital Cinema Packages (DCP)**, transmissões de rede e todos os formatos de ficheiro que o mpv consegue reproduzir.

<p align="center">
  <img src="docs/screenshots/start.png" width="49%" alt="Lumen">
  <img src="docs/screenshots/cinema.png" width="49%" alt="Lumen">
</p>

## Transferências

Os pacotes pré-compilados estão na [página de versões](https://github.com/Karl-Lauterbach24/Lumen/releases):

| Sistema | Ficheiro |
|---|---|
| Windows 10/11 (x64, ARM64) | `Lumen-<version>-windows-x64.msi`, `…-windows-arm64.msi` (instalador) · `.zip` (portátil) |
| macOS 15 (Apple Silicon, Intel) | `Lumen-<version>-macos-arm64.dmg`, `…-macos-x64.dmg` |
| Debian 13 (x86-64, ARM64) | `Lumen-<version>-linux-amd64.deb`, `…-linux-arm64.deb` |
| Fedora 44 (x86-64, ARM64) | `Lumen-<version>-linux-x86_64.rpm`, `…-linux-aarch64.rpm` |

Todos os pacotes contêm as mesmas bibliotecas multimédia (FFmpeg com descodificador de Blu-ray 3D, libmpv); em Linux, o Qt 6 e as bibliotecas de discos vêm da sua distribuição. O Lumen procura novas versões e, no Windows e no macOS, instala-as com um clique depois de verificar a soma de verificação.

## Funcionalidades

- **Discos:** Blu-ray e DVD com menus, títulos, capítulos, faixas de áudio e legendas; as unidades são detetadas automaticamente.
- **Cinema digital:** DCP em JPEG 2000, SMPTE e Interop, pacotes cifrados com KDM, renderização Dolby Atmos/IAB, programas de projeção.
- **Streaming:** ligações de todo o tipo (HLS, DASH, RTSP, …) e os servidores multimédia Jellyfin, Emby e Plex.
- **Transmissão:** envia a imagem e o som para um televisor ou recetor na rede: DLNA, Chromecast, AirPlay (recetores sem emparelhamento), as aplicações [Lumen TV](https://github.com/Karl-Lauterbach24/Lumen-TV) para Android TV, Samsung e LG, ou qualquer navegador. O Miracast funciona através da definição de ecrã sem fios do sistema.
- **3D:** Blu-ray 3D (MVC) em frame packing, lado a lado, cima-baixo ou anáglifo.
- **Perfis de saída:** ecrã de destino, adaptação da frequência, HDR passthrough ou tone mapping, calibração (ICC, LUT 3D).
- **Áudio:** bitstream para um recetor AV (TrueHD/Atmos, DTS-HD), modo noturno, atraso e velocidade.
- **CD de áudio:** nomes das faixas a partir do CD-Text ou, com o plugin *Disc identification*, do MusicBrainz.
- **No dia a dia:** lista de reproduzidos recentemente com retoma, arrastar e largar, ficheiros de legendas externos, atalhos de teclado (F1).
- **Interface em 16 idiomas**, selecionável no cabeçalho da janela (símbolo do globo).

## Proteção contra cópia

O Lumen **não** contém nenhum mecanismo para contornar a proteção contra cópia. Os discos protegidos (AACS, BD+, CSS) só são reproduzidos se adicionar as bibliotecas necessárias através de um plugin; cabe-lhe verificar se isso é legal no seu país.

## Plugins

Os plugins acrescentam fontes, chaves, scripts e funções. O separador **Plugins** instala-os a partir da [loja de plugins](https://github.com/Karl-Lauterbach24/Lumen-Plugins) ou das suas próprias fontes; cada ficheiro é verificado pela sua soma de verificação.

## Mais informações

A compilação, a arquitetura, os testes, a linha de comandos e todos os atalhos de teclado estão descritos no [README em inglês](README.md). Esta tradução foi feita com ajuda automática; as correções são bem-vindas.

## Licença

O Lumen é software livre sob a **GNU Affero General Public License v3.0 ou posterior** ([LICENSE](LICENSE)). Componentes e licenças: [THIRD_PARTY.md](THIRD_PARTY.md).
