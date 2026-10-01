<p align="center"><img src="resources/logo/lumen-logo.png" width="300" alt="Lumen"></p>

# Lumen – 光盘与数字影院播放器

[English](README.md) · [Deutsch](README.de.md) · [Français](README.fr.md) · [Español](README.es.md) · [Italiano](README.it.md) · [Português](README.pt.md) · [Nederlands](README.nl.md) · [Polski](README.pl.md) · [Svenska](README.sv.md) · [Čeština](README.cs.md) · [Türkçe](README.tr.md) · [Українська](README.uk.md) · [Русский](README.ru.md) · [日本語](README.ja.md) · **简体中文** · [한국어](README.ko.md)

一款快速、简洁的播放器，适用于家庭影院、放映室和小型影院，采用**双窗口**设计：

- **播放窗口** – mpv 原生窗口（gpu-next、D3D11/Vulkan/Wayland），可固定到某个输出设备，支持 HDR 直通。
- **控制窗口** – 来源、播放控制、标题、章节、音频、字幕、画面、影院、流媒体、输出配置和插件。

Lumen 可播放 **Blu-ray / UHD / Blu-ray 3D、带菜单的 DVD-Video、HD DVD、Video CD、音乐 CD、Digital Cinema Package (DCP)**、流媒体，以及 mpv 支持的所有文件格式。

<p align="center">
  <img src="docs/screenshots/start.png" width="49%" alt="Lumen">
  <img src="docs/screenshots/cinema.png" width="49%" alt="Lumen">
</p>

## 下载

预编译的安装包在[发布页面](https://github.com/Karl-Lauterbach24/Lumen/releases)：

| 系统 | 文件 |
|---|---|
| Windows 10/11 (x64, ARM64) | `Lumen-<version>-windows-x64.msi`, `…-windows-arm64.msi` (安装程序) · `.zip` (便携版) |
| macOS 15 (Apple Silicon, Intel) | `Lumen-<version>-macos-arm64.dmg`, `…-macos-x64.dmg` |
| Debian 13 (x86-64, ARM64) | `Lumen-<version>-linux-amd64.deb`, `…-linux-arm64.deb` |
| Fedora 44 (x86-64, ARM64) | `Lumen-<version>-linux-x86_64.rpm`, `…-linux-aarch64.rpm` |

所有安装包都包含相同的媒体库（带 Blu-ray 3D 解码器的 FFmpeg、libmpv）；在 Linux 上，Qt 6 和光盘库来自你的发行版。 Lumen 会检查新版本；在 Windows 和 macOS 上，校验和验证通过后可一键安装。

## 功能

- **光盘：**带菜单的 Blu-ray 和 DVD，标题、章节、音轨和字幕；自动识别光驱。
- **数字影院：**JPEG 2000 的 DCP（SMPTE 和 Interop）、使用 KDM 的加密包、Dolby Atmos/IAB 渲染、放映单。
- **流媒体：**各类链接（HLS、DASH、RTSP 等）以及 Jellyfin、Emby、Plex 媒体服务器。
- **投放：**将画面和声音发送到网络中的电视或接收设备：DLNA、Chromecast、AirPlay（无需配对的接收设备）、适用于 Android TV、三星和 LG 的 [Lumen TV](https://github.com/Karl-Lauterbach24/Lumen-TV) 应用，或任意浏览器。Miracast 通过系统的无线显示器设置实现。
- **3D：**Blu-ray 3D (MVC) 可按帧封装、左右、上下或红蓝立体输出。
- **输出配置：**目标屏幕、刷新率匹配、HDR 直通或色调映射、校准（ICC、3D LUT）。
- **音频：**向 AV 功放输出码流（TrueHD/Atmos、DTS-HD）、夜间模式、延迟和速度调节。
- **音乐 CD：**曲目名称来自 CD-Text，或通过 *Disc identification* 插件从 MusicBrainz 获取。
- **日常使用：**可续播的“最近播放”列表、拖放、外挂字幕文件、键盘快捷键 (F1)。
- **16 种界面语言**，可在起始页选择。

## 复制保护

Lumen **不包含**任何规避复制保护的功能。受保护的光盘（AACS、BD+、CSS）只有在您自行通过插件添加所需的库之后才能播放；请自行确认这在您所在的国家或地区是否合法。

## 插件

插件可添加来源、密钥、脚本和功能。在**插件**选项卡中，可以从[插件商店](https://github.com/Karl-Lauterbach24/Lumen-Plugins)或您自己的来源安装；每个文件都会用校验和进行验证。

## 更多信息

从源码构建、架构、测试、命令行和全部键盘快捷键见[英文 README](README.md)。本译文借助机器翻译完成，欢迎指正。

## 许可证

Lumen 是自由软件，采用 **GNU Affero General Public License v3.0 或更高版本**（[LICENSE](LICENSE)）。各组件及其许可证：[THIRD_PARTY.md](THIRD_PARTY.md)。
