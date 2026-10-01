<p align="center"><img src="resources/logo/lumen-logo.png" width="300" alt="Lumen"></p>

# Lumen – ディスク・デジタルシネマプレーヤー

[English](README.md) · [Deutsch](README.de.md) · [Français](README.fr.md) · [Español](README.es.md) · [Italiano](README.it.md) · [Português](README.pt.md) · [Nederlands](README.nl.md) · [Polski](README.pl.md) · [Svenska](README.sv.md) · [Čeština](README.cs.md) · [Türkçe](README.tr.md) · [Українська](README.uk.md) · [Русский](README.ru.md) · **日本語** · [简体中文](README.zh.md) · [한국어](README.ko.md)

ホームシアター、試写室、小規模な映画館のための、高速でシンプルなプレーヤーです。**2 つのウィンドウ**で構成されています。

- **プレーヤーウィンドウ** – mpv のネイティブウィンドウ（gpu-next、D3D11/Vulkan/Wayland）。出力デバイスに固定でき、HDR パススルーに対応。
- **コントロールウィンドウ** – ソース、再生操作、タイトル、チャプター、音声、字幕、映像、シネマ、ストリーミング、出力プロファイル、プラグイン。

Lumen は **Blu-ray / UHD / Blu-ray 3D、メニュー付き DVD-Video、HD DVD、ビデオ CD、音楽 CD、Digital Cinema Package (DCP)**、ストリーム、そして mpv が再生できるすべてのファイル形式を再生します。

<p align="center">
  <img src="docs/screenshots/start.png" width="49%" alt="Lumen">
  <img src="docs/screenshots/cinema.png" width="49%" alt="Lumen">
</p>

## ダウンロード

ビルド済みパッケージは[リリースページ](https://github.com/Karl-Lauterbach24/Lumen/releases)にあります。

| システム | ファイル |
|---|---|
| Windows 10/11 (x64, ARM64) | `Lumen-<version>-windows-x64.msi`, `…-windows-arm64.msi` (インストーラー) · `.zip` (ポータブル版) |
| macOS 15 (Apple Silicon, Intel) | `Lumen-<version>-macos-arm64.dmg`, `…-macos-x64.dmg` |
| Debian 13 (x86-64, ARM64) | `Lumen-<version>-linux-amd64.deb`, `…-linux-arm64.deb` |
| Fedora 44 (x86-64, ARM64) | `Lumen-<version>-linux-x86_64.rpm`, `…-linux-aarch64.rpm` |

すべてのパッケージに同じメディアライブラリ（Blu-ray 3D デコーダー付き FFmpeg、libmpv）が含まれます。Linux では Qt 6 とディスク用ライブラリはディストリビューションのものを使います。 Lumen は新しいバージョンを確認し、Windows と macOS ではチェックサムを検証したうえでワンクリックでインストールします。

## 機能

- **ディスク:** メニュー付きの Blu-ray と DVD、タイトル、チャプター、音声・字幕トラック。ドライブは自動で検出されます。
- **デジタルシネマ:** JPEG 2000 の DCP（SMPTE / Interop）、KDM による暗号化パッケージ、Dolby Atmos/IAB のレンダリング、上映プログラム。
- **ストリーミング:** さまざまなリンク（HLS、DASH、RTSP など）と、メディアサーバー Jellyfin、Emby、Plex。
- **キャスト:** 映像と音声をネットワーク上のテレビや受信機に送ります。DLNA、Chromecast、AirPlay（ペアリング不要の受信機）、Android TV・Samsung・LG 用の [Lumen TV](https://github.com/Karl-Lauterbach24/Lumen-TV) アプリ、または任意のブラウザーに対応します。Miracast はシステムのワイヤレスディスプレイ設定を使います。
- **3D:** Blu-ray 3D (MVC) をフレームパッキング、サイドバイサイド、トップアンドボトム、アナグリフで出力。
- **出力プロファイル:** 出力先の画面、リフレッシュレート合わせ、HDR パススルーまたはトーンマッピング、キャリブレーション（ICC、3D LUT）。
- **音声:** AV レシーバーへのビットストリーム（TrueHD/Atmos、DTS-HD）、ナイトモード、遅延と速度の調整。
- **音楽 CD:** トラック名は CD-Text から、またはプラグイン *Disc identification* を使って MusicBrainz から取得。
- **ふだん使い:** 続きから再生できる「最近再生したもの」一覧、ドラッグ＆ドロップ、外部字幕ファイル、キーボードショートカット (F1)。
- **16 言語のインターフェース**。スタート画面で切り替えられます。

## コピー保護について

Lumen にはコピー保護を回避する機能は**一切含まれていません**。保護されたディスク（AACS、BD+、CSS）は、必要なライブラリをご自身でプラグインとして追加した場合にのみ再生できます。お住まいの国で合法かどうかはご自身の責任で確認してください。

## プラグイン

プラグインはソース、キー、スクリプト、機能を追加します。**プラグイン**タブから、[プラグインストア](https://github.com/Karl-Lauterbach24/Lumen-Plugins)または独自の提供元のプラグインをインストールできます。すべてのファイルはチェックサムで検証されます。

## さらに詳しく

ソースからのビルド、アーキテクチャ、テスト、コマンドライン、すべてのキーボードショートカットは[英語の README](README.md) に記載されています。この翻訳は機械の支援で作成しました。修正の提案を歓迎します。

## ライセンス

Lumen は **GNU Affero General Public License v3.0 以降**で提供されるフリーソフトウェアです（[LICENSE](LICENSE)）。構成要素とそのライセンス: [THIRD_PARTY.md](THIRD_PARTY.md)。
