#!/usr/bin/env bash
# Builds the media libraries Lumen ships, from the same pinned sources on every platform:
#
#   x264        H.264 encoder (casting)
#   FFmpeg-mvc  FFmpeg fork with the H.264/MVC decoder (Blu-ray 3D)
#   mpv         libmpv, linked against that FFmpeg, with Lumen's patches (tools/patches):
#               audio tap on the timed null output, used for casting
#   dvdnav      libdvdread + libdvdnav 7 (only where the system has an older one)
#
# All other libraries (Qt, libass, libplacebo, libbluray, libcdio, OpenSSL, libxml2 ...) come from
# the platform's package manager. The result is one prefix that CMake picks up automatically:
#
#   tools/build_deps.sh                 # everything into 3rdparty/prefix
#   tools/build_deps.sh x264 ffmpeg     # only these parts
#
# Variables: PREFIX (target), SRCROOT (sources/build trees), JOBS
# Windows: run inside an MSYS2 shell (UCRT64/CLANGARM64) or in Git Bash with the local MSYS2 tree.
set -euo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"

# --- pinned versions: change them here, for all platforms at once ------------------------------
X264_REPO=https://code.videolan.org/videolan/x264.git
X264_REF=b35605ace3ddf7c1a5d67a2eb553f034aef41d55          # branch "stable"
FFMPEG_REPO=https://github.com/tthayer93/FFmpeg-mvc.git
FFMPEG_REF=86c0b25eac28aafc3e09b76c8b319854c1f396e9        # release/9.0 (n9.0.2-mvc8)
MPV_REPO=https://github.com/mpv-player/mpv.git
MPV_REF=v0.41.0
DVDREAD_REPO=https://code.videolan.org/videolan/libdvdread.git
DVDREAD_REF=7.1.1
DVDNAV_REPO=https://code.videolan.org/videolan/libdvdnav.git
DVDNAV_REF=7.0.0
# -----------------------------------------------------------------------------------------------

PREFIX="${PREFIX:-$HERE/3rdparty/prefix}"
SRCROOT="${SRCROOT:-$HERE/3rdparty/src}"
JOBS="${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu)}"
PARTS=("$@")

OS=linux
FF_PLATFORM=()
MPV_PLATFORM=()
case "$(uname -s)" in
MINGW*|MSYS*)
    OS=windows
    # Local build (Git Bash): own MSYS2 tree under a short path, see tools/msys2_fetch.py.
    # In a real MSYS2 shell (CI) the environment is already complete.
    R="${TOOLROOT:-/c/lumen-build}"
    if [ -z "${MSYSTEM_PREFIX:-}" ] || [ ! -x "${MSYSTEM_PREFIX}/bin/gcc.exe" -a ! -x "${MSYSTEM_PREFIX}/bin/clang.exe" ]; then
        if [ -d "$R/msys2/ucrt64" ]; then
            export PATH="$R/msys2/ucrt64/bin:$R/msys2-tools/usr/bin:/usr/bin:$PATH"
            export PKG_CONFIG_PATH="$R/msys2/ucrt64/lib/pkgconfig"
            # deep build paths exceed the toolchain's 260 character limit
            SRCROOT="${SRCROOT_SHORT:-$R/deps-src}"
            mkdir -p "$R/tmp"
            TMPDIR="$(cygpath -m "$R/tmp")"; export TMPDIR TEMP="$TMPDIR" TMP="$TMPDIR"
        fi
    fi
    # configure scripts cannot handle backslashes, compilers no POSIX paths: use "C:/..."
    PREFIX="$(cygpath -m "$PREFIX")"
    FF_PLATFORM=(--pkg-config=pkgconf --enable-d3d11va --enable-dxva2 --enable-d3d12va --enable-mediafoundation)
    ;;
Darwin)
    OS=macos
    FF_PLATFORM=(--enable-videotoolbox --enable-audiotoolbox)
    # the embedded player window uses the render API; mpv's own Cocoa window (Swift) is not needed
    MPV_PLATFORM=(-Dswift-build=disabled -Dmacos-cocoa-cb=disabled -Dmacos-media-player=disabled)
    for keg in openssl@3 libxml2 libarchive; do
        p="$(brew --prefix "$keg" 2>/dev/null || true)"
        [ -d "$p/lib/pkgconfig" ] && export PKG_CONFIG_PATH="$p/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
    done
    ;;
*)
    FF_PLATFORM=(--enable-vaapi --enable-vdpau)
    ;;
esac

# Search paths in POSIX form: the MSYS2 runtime converts such lists for native programs
PREFIX_POSIX="$PREFIX"
[ "$OS" = windows ] && PREFIX_POSIX="$(cygpath -u "$PREFIX")"
export PKG_CONFIG_PATH="$PREFIX_POSIX/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
export PATH="$PREFIX_POSIX/bin:$PATH"
mkdir -p "$PREFIX" "$SRCROOT"

want() {
    [ ${#PARTS[@]} -eq 0 ] && return 0
    local p
    for p in "${PARTS[@]}"; do [ "$p" = "$1" ] && return 0; done
    return 1
}

# checkout <dir> <repo> <ref>: shallow clone of exactly this commit/tag
checkout() {
    local dir="$SRCROOT/$1"
    if [ ! -d "$dir/.git" ]; then
        git init -q "$dir"
        git -C "$dir" remote add origin "$2"
    fi
    if [ "$(cat "$dir/.lumen-ref" 2>/dev/null || true)" != "$3" ]; then
        git -C "$dir" fetch -q --depth 1 origin "$3"
        git -C "$dir" checkout -q -f FETCH_HEAD
        echo "$3" > "$dir/.lumen-ref"
    fi
    echo "$dir"
}

if want x264; then
    echo "=== x264 ($X264_REF)"
    src="$(checkout x264 "$X264_REPO" "$X264_REF")"
    cd "$src"
    ./configure --prefix="$PREFIX" --enable-shared --enable-pic --disable-cli --disable-opencl \
                --disable-avs --disable-swscale --disable-lavf --disable-ffms --disable-gpac --disable-lsmash
    make -j"$JOBS"
    make install
fi

if want ffmpeg; then
    echo "=== FFmpeg-mvc ($FFMPEG_REF)"
    src="$(checkout ffmpeg "$FFMPEG_REPO" "$FFMPEG_REF")"
    mkdir -p "$SRCROOT/ffmpeg-build"
    cd "$SRCROOT/ffmpeg-build"
    # Built-in decoders cover Blu-ray, DVD, VCD and DCP (H.264/MVC, HEVC, VC-1, MPEG-2, JPEG 2000,
    # TrueHD, DTS(-HD), (E-)AC-3, LPCM, PGS). dav1d: AV1 files. libxml2: DASH streams.
    # libx264 + built-in AAC: the stream Lumen sends to cast receivers.
    "$src/configure" \
        --prefix="$PREFIX" \
        --enable-shared --disable-static \
        --disable-doc --disable-debug --disable-ffplay \
        --enable-gpl --enable-version3 \
        --enable-libx264 --enable-libdav1d --enable-libxml2 --enable-openssl \
        --enable-zlib --enable-bzlib --enable-lzma \
        --pkg-config-flags=--static \
        "${FF_PLATFORM[@]}" \
        --extra-cflags="-O2"
    make -j"$JOBS"
    make install
    "$PREFIX/bin/ffmpeg" -hide_banner -version | head -1
    "$PREFIX/bin/ffmpeg" -hide_banner -encoders 2>/dev/null | grep -E " libx264 | aac " || { echo "FFmpeg: H.264/AAC encoder missing" >&2; exit 1; }
fi

# libdvdread/libdvdnav 7: exact seeking, audio/subtitle selection in the navigator, region code.
# Only built where the system still has version 6 (e.g. Debian 13).
if want dvdnav && [ "$OS" = linux ] && ! pkg-config --atleast-version=7.0 dvdnav 2>/dev/null; then
    for lib in dvdread dvdnav; do
        echo "=== lib$lib"
        if [ $lib = dvdread ]; then src="$(checkout dvdread "$DVDREAD_REPO" "$DVDREAD_REF")"; else src="$(checkout dvdnav "$DVDNAV_REPO" "$DVDNAV_REF")"; fi
        rm -rf "$SRCROOT/$lib-build"
        meson setup "$SRCROOT/$lib-build" "$src" --prefix="$PREFIX" --libdir=lib --buildtype=release -Ddefault_library=shared
        meson compile -C "$SRCROOT/$lib-build"
        meson install -C "$SRCROOT/$lib-build"
    done
fi

if want mpv; then
    echo "=== mpv ($MPV_REF)"
    src="$(checkout mpv "$MPV_REPO" "$MPV_REF")"
    # Lumen's changes to mpv (tools/patches/mpv-*.patch), always on a clean tree
    git -C "$src" checkout -q -f HEAD -- .
    for patch in "$HERE"/tools/patches/mpv-*.patch; do
        git -C "$src" apply "$patch"
        echo "applied: $(basename "$patch")"
    done
    rm -rf "$SRCROOT/mpv-build"
    meson setup "$SRCROOT/mpv-build" "$src" --prefix="$PREFIX" --libdir=lib --buildtype=release \
        -Dlibmpv=true -Dcplayer=false -Dtests=false \
        -Dmanpage-build=disabled -Dhtml-build=disabled -Dpdf-build=disabled \
        -Dlua=enabled -Dlibbluray=enabled -Ddvdnav=enabled -Dlcms2=enabled -Dlibarchive=enabled \
        -Dvapoursynth=disabled -Dcdda=disabled -Dcaca=disabled -Dsdl2-audio=disabled -Dsdl2-video=disabled \
        -Dsdl2-gamepad=disabled -Dopenal=disabled -Djack=disabled -Ddvbin=disabled -Dsixel=disabled \
        "${MPV_PLATFORM[@]}"
    meson compile -C "$SRCROOT/mpv-build"
    meson install -C "$SRCROOT/mpv-build"
fi

# Linux: the libraries are installed privately (/usr/lib/lumen); each one finds its siblings there
if [ "$OS" = linux ] && command -v patchelf >/dev/null; then
    for lib in "$PREFIX"/lib/*.so.*; do
        [ -L "$lib" ] || patchelf --set-rpath '$ORIGIN' "$lib"
    done
    for bin in "$PREFIX"/bin/ffmpeg "$PREFIX"/bin/ffprobe; do
        [ -f "$bin" ] && patchelf --set-rpath '$ORIGIN/../lib' "$bin"
    done
fi

echo "Done: $PREFIX"
