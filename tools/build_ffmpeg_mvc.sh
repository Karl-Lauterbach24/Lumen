#!/usr/bin/env bash
# Baut FFmpeg-mvc (H.264/MVC-fähiger FFmpeg-Fork) als gemeinsame Bibliotheken.
#
# Die Hauptversion muss exakt zu der FFmpeg-Version passen, gegen die libmpv
# gebaut wurde (MSYS2 mpv 0.41 -> FFmpeg 9.0 -> Branch release/9.0). Die DLLs
# ersetzen dann 1:1 die normalen FFmpeg-DLLs.
#
#   Windows (Git-Bash):  tools/build_ffmpeg_mvc.sh
#   Linux/macOS:         tools/build_ffmpeg_mvc.sh   (System-Compiler, pkg-config)
#
# Variablen: FFMPEG_MVC_BRANCH (release/9.0), SRC (Quellordner), DEST (Ziel), JOBS
set -euo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"
BRANCH="${FFMPEG_MVC_BRANCH:-release/9.0}"
DEST="${DEST:-$HERE/3rdparty/ffmpeg-mvc}"
JOBS="${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu)}"

case "$(uname -s)" in
MINGW*|MSYS*)
    # Kurzer Build-Pfad: tiefe Ordner sprengen sonst das 260-Zeichen-Limit der Toolchain
    SRC="${SRC:-/c/lumen-build/ffmpeg-mvc}"
    # Toolchain ebenfalls über kurzen Pfad (Junction auf 3rdparty), sonst findet gcc cc1.exe nicht
    R="${TOOLROOT:-/c/lumen-build}"
    [ -d "$R/msys2" ] || R="$HERE/3rdparty"
    # Eigene MSYS2-Pakete (lokaler Build) – in einer echten MSYS2-Umgebung (CI) die vorhandene nutzen
    if [ -d "$R/msys2/ucrt64" ]; then
        export PATH="$R/msys2/ucrt64/bin:$R/msys2-tools/usr/bin:/usr/bin:$PATH"
        export PKG_CONFIG_PATH="$R/msys2/ucrt64/lib/pkgconfig"
    fi
    # configure verträgt keine Backslashes, gcc keine POSIX-Pfade -> "C:/..."-Form für beide
    mkdir -p "$(dirname "$SRC")/tmp"
    TMPDIR="$(cygpath -m "$(dirname "$SRC")/tmp")"
    export TMPDIR TEMP="$TMPDIR" TMP="$TMPDIR"
    PLATFORM_FLAGS=(--pkg-config=pkgconf --enable-d3d11va --enable-dxva2 --enable-d3d12va --enable-schannel)
    ;;
Darwin)
    SRC="${SRC:-$HERE/3rdparty/ffmpeg-mvc-src}"
    PLATFORM_FLAGS=(--enable-videotoolbox --enable-audiotoolbox)
    ;;
*)
    SRC="${SRC:-$HERE/3rdparty/ffmpeg-mvc-src}"
    PLATFORM_FLAGS=(--enable-vaapi --enable-vdpau)
    ;;
esac

if [ ! -d "$SRC/.git" ]; then
    mkdir -p "$(dirname "$SRC")"
    git clone --depth 1 --branch "$BRANCH" https://github.com/tthayer93/FFmpeg-mvc.git "$SRC"
fi

cd "$SRC"
echo "FFmpeg-mvc $(cat RELEASE) -> $DEST ($JOBS Jobs)"

# Blu-ray braucht nur die eingebauten Decoder (H.264/MVC, HEVC, VC-1, MPEG-2,
# TrueHD, DTS(-HD), (E-)AC-3, LPCM, PGS). dav1d für AV1-Dateien.
./configure \
    --prefix="$DEST" \
    --enable-shared --disable-static \
    --disable-doc --disable-debug \
    --enable-gpl --enable-version3 \
    --enable-libdav1d \
    --enable-zlib --enable-bzlib --enable-lzma \
    "${PLATFORM_FLAGS[@]}" \
    --extra-cflags="-O2"

make -j"$JOBS"
make install
echo "Fertig: $DEST"
