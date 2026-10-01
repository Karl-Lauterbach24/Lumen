<p align="center"><img src="resources/logo/lumen-logo.png" width="300" alt="Lumen"></p>

# Lumen – 디스크 및 디지털 시네마 플레이어

[English](README.md) · [Deutsch](README.de.md) · [Français](README.fr.md) · [Español](README.es.md) · [Italiano](README.it.md) · [Português](README.pt.md) · [Nederlands](README.nl.md) · [Polski](README.pl.md) · [Svenska](README.sv.md) · [Čeština](README.cs.md) · [Türkçe](README.tr.md) · [Українська](README.uk.md) · [Русский](README.ru.md) · [日本語](README.ja.md) · [简体中文](README.zh.md) · **한국어**

홈 시어터, 시사실, 소규모 영화관을 위한 빠르고 간결한 플레이어입니다. **두 개의 창**으로 이루어져 있습니다.

- **플레이어 창** – mpv 네이티브 창(gpu-next, D3D11/Vulkan/Wayland). 출력 장치에 고정할 수 있고 HDR 패스스루를 지원합니다.
- **조작 창** – 소스, 재생 조작, 타이틀, 챕터, 오디오, 자막, 화면, 시네마, 스트리밍, 출력 프로필, 플러그인.

Lumen은 **Blu-ray / UHD / Blu-ray 3D, 메뉴가 있는 DVD-Video, HD DVD, 비디오 CD, 오디오 CD, Digital Cinema Package(DCP)**, 스트림, 그리고 mpv가 재생할 수 있는 모든 파일 형식을 재생합니다.

<p align="center">
  <img src="docs/screenshots/start.png" width="49%" alt="Lumen">
  <img src="docs/screenshots/cinema.png" width="49%" alt="Lumen">
</p>

## 다운로드

미리 빌드된 패키지는 [릴리스 페이지](https://github.com/Karl-Lauterbach24/Lumen/releases)에 있습니다.

| 시스템 | 파일 |
|---|---|
| Windows 10/11 (x64, ARM64) | `Lumen-<version>-windows-x64.msi`, `…-windows-arm64.msi` (설치 프로그램) · `.zip` (포터블) |
| macOS 15 (Apple Silicon, Intel) | `Lumen-<version>-macos-arm64.dmg`, `…-macos-x64.dmg` |
| Debian 13 (x86-64, ARM64) | `Lumen-<version>-linux-amd64.deb`, `…-linux-arm64.deb` |
| Fedora 44 (x86-64, ARM64) | `Lumen-<version>-linux-x86_64.rpm`, `…-linux-aarch64.rpm` |

Linux 패키지는 배포판의 Qt 6, libmpv, 디스크 라이브러리를 사용합니다. Lumen은 새 버전을 확인하며, Windows와 macOS에서는 체크섬을 검증한 뒤 한 번의 클릭으로 설치합니다.

## 기능

- **디스크:** 메뉴가 있는 Blu-ray와 DVD, 타이틀, 챕터, 오디오·자막 트랙. 드라이브는 자동으로 인식됩니다.
- **디지털 시네마:** JPEG 2000 DCP(SMPTE 및 Interop), KDM을 사용하는 암호화 패키지, Dolby Atmos/IAB 렌더링, 상영 목록.
- **스트리밍:** 여러 종류의 링크(HLS, DASH, RTSP 등)와 미디어 서버 Jellyfin, Emby, Plex.
- **3D:** Blu-ray 3D(MVC)를 프레임 패킹, 사이드 바이 사이드, 톱 앤 보텀, 애너글리프로 출력.
- **출력 프로필:** 대상 화면, 주사율 맞춤, HDR 패스스루 또는 톤 매핑, 캘리브레이션(ICC, 3D LUT).
- **오디오:** AV 리시버로 비트스트림 출력(TrueHD/Atmos, DTS-HD), 야간 모드, 지연과 속도 조절.
- **오디오 CD:** 트랙 이름은 CD-Text에서, 또는 *Disc identification* 플러그인으로 MusicBrainz에서 가져옵니다.
- **일상 사용:** 이어 보기가 되는 최근 재생 목록, 끌어다 놓기, 외부 자막 파일, 키보드 단축키(F1).
- **16개 언어 인터페이스**, 시작 화면에서 선택할 수 있습니다.

## 복사 방지

Lumen에는 복사 방지를 우회하는 기능이 **전혀 없습니다**. 보호된 디스크(AACS, BD+, CSS)는 필요한 라이브러리를 사용자가 직접 플러그인으로 추가한 경우에만 재생됩니다. 거주 국가에서 합법인지는 사용자가 확인해야 합니다.

## 플러그인

플러그인은 소스, 키, 스크립트, 기능을 더합니다. **플러그인** 탭에서 [플러그인 스토어](https://github.com/Karl-Lauterbach24/Lumen-Plugins)나 직접 추가한 출처의 플러그인을 설치할 수 있으며, 모든 파일은 체크섬으로 검증됩니다.

## 더 알아보기

소스에서 빌드하는 방법, 구조, 테스트, 명령줄, 모든 키보드 단축키는 [영어 README](README.md)에 있습니다. 이 번역은 기계의 도움을 받아 작성했습니다. 수정 제안을 환영합니다.

## 라이선스

Lumen은 **GNU Affero General Public License v3.0 이상**으로 배포되는 자유 소프트웨어입니다([LICENSE](LICENSE)). 구성 요소와 라이선스: [THIRD_PARTY.md](THIRD_PARTY.md).
