#!/usr/bin/env bash
# Builds the media libraries Lumen ships, from the same pinned sources on every platform:
#
#   x264        H.264 encoder (casting)
#   FFmpeg-mvc  FFmpeg fork with the H.264/MVC decoder (Blu-ray 3D), with Lumen's patches:
#               faster JPEG 2000 decoding (DCP), same output bit for bit, and the option
#               skip_planes; MVC: slice threading with both views, two decoding faults fixed
#   mpv         libmpv, linked against that FFmpeg, with Lumen's patches (tools/patches):
#               audio tap on the timed null output, used for casting; scaler weight table
#               without uninitialized padding (black picture with software OpenGL); sound starts
#               after the fallback from refused bitstream output to decoding; Wayland: no abort
#               when no EGL context can be made (the Wayland state stayed behind and tripped X11)
#   dvdnav      libdvdread + libdvdnav 7 (only where the system has an older one)
#   bluray      libudfread + libbluray 1.5 (only where the system has an older one): the version
#               whose Java classes Lumen ships (resources/bdj), so disc menus written in Java (BD-J)
#               run on the same code everywhere
#   jre         Linux: a small Java runtime for those menus, cut with jlink from an Eclipse Temurin
#               JDK that is fetched for the build (the build containers have none), with Java's
#               own X11 window library (libbluray runs the VM "not headless"; see make_jre.py).
#               macOS and Windows take theirs from a JDK of the build machine when the package is made.
#
# All other libraries (Qt, libass, libplacebo, libcdio, OpenSSL, libxml2 ...) come from
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
UDFREAD_REPO=https://code.videolan.org/videolan/libudfread.git
UDFREAD_REF=1.2.0
BLURAY_REPO=https://code.videolan.org/videolan/libbluray.git
BLURAY_REF=1.5.0                                           # must match the archive in resources/bdj
JRE_JAVA=17                                                # the Java version the disc tests ran with
# -----------------------------------------------------------------------------------------------

PREFIX="${PREFIX:-$HERE/3rdparty/prefix}"
SRCROOT="${SRCROOT:-$HERE/3rdparty/src}"
JOBS="${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu)}"
PARTS=("$@")

OS=linux
FF_CFLAGS=""
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
    # gfxcapture (screen capture, C++/WinRT) is not needed; with clang its C++ headers pick up the
    # file VERSION of the FFmpeg source tree as <version> on the case-insensitive file system
    FF_PLATFORM=(--pkg-config=pkgconf --enable-d3d11va --enable-dxva2 --enable-d3d12va --enable-mediafoundation
                 --disable-filter=gfxcapture)
    # CLANG environments (Windows on ARM): no gcc
    if ! command -v gcc >/dev/null && command -v clang >/dev/null; then
        export CC=clang CXX=clang++
        FF_PLATFORM+=(--cc=clang --cxx=clang++)
    fi
    ;;
Darwin)
    OS=macos
    # Homebrew's headers and libraries are not in the compiler's default search path
    BREW="$(brew --prefix)"
    FF_PLATFORM=(--enable-videotoolbox --enable-audiotoolbox "--extra-ldflags=-L$BREW/lib")
    FF_CFLAGS="-I$BREW/include"
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
# Linux: programs built here (ffmpeg) find the new libraries before the final rpath fix
[ "$OS" = linux ] && export LD_LIBRARY_PATH="$PREFIX/lib:${LD_LIBRARY_PATH:-}"
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

# libbluray 1.5 where the system has an older one (Debian 13: 1.3.4, Fedora 44: 1.4.0). First in the
# list: mpv links to it, and Lumen and mpv must use the same one. The Java part is not built here
# (it would need ant and a JDK); the archive comes from resources/bdj.
if want bluray && [ "$OS" = linux ] && ! pkg-config --atleast-version="$BLURAY_REF" libbluray 2>/dev/null; then
    if ! pkg-config --atleast-version="$UDFREAD_REF" libudfread 2>/dev/null; then
        echo "=== libudfread ($UDFREAD_REF)"
        src="$(checkout udfread "$UDFREAD_REPO" "$UDFREAD_REF")"
        rm -rf "$SRCROOT/udfread-build"
        meson setup "$SRCROOT/udfread-build" "$src" --prefix="$PREFIX" --libdir=lib --buildtype=release -Ddefault_library=shared
        meson compile -C "$SRCROOT/udfread-build"
        meson install -C "$SRCROOT/udfread-build"
    fi
    echo "=== libbluray ($BLURAY_REF)"
    src="$(checkout bluray "$BLURAY_REPO" "$BLURAY_REF")"
    rm -rf "$SRCROOT/bluray-build"
    meson setup "$SRCROOT/bluray-build" "$src" --prefix="$PREFIX" --libdir=lib --buildtype=release -Ddefault_library=shared \
        -Dbdj_jar=disabled
    meson compile -C "$SRCROOT/bluray-build"
    meson install -C "$SRCROOT/bluray-build"
    pkg-config --modversion libbluray
fi

# Linux: the Java runtime for BD-J menus. The distributions' Java does not do: Debian's libbluray-bdj
# pulls in a runtime without the graphics part ("headless"), which libbluray cannot start; Fedora's
# Java is newer than its libbluray can use. So the packages carry the runtime the menus were tried with.
if want jre && [ "$OS" = linux ] && [ ! -f "$PREFIX/jre/lumen-jre.txt" ]; then
    echo "=== Java runtime (Temurin $JRE_JAVA, jlink)"
    PY="$(command -v python3 || command -v python)"
    rm -rf "$SRCROOT/jdk"
    jdk="$("$PY" "$HERE/tools/fetch_jdk.py" "$JRE_JAVA" "$SRCROOT/jdk")"
    "$PY" "$HERE/tools/make_jre.py" "$PREFIX/jre" --jdk "$jdk"
    rm -rf "$SRCROOT/jdk"
    [ -f "$PREFIX/jre/lumen-jre.txt" ] || { echo "Java runtime: jlink failed" >&2; exit 1; }
fi

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
    # Lumen's changes to FFmpeg (tools/patches/ffmpeg-*.patch), always on a clean tree
    git -C "$src" checkout -q -f HEAD -- .
    for patch in "$HERE"/tools/patches/ffmpeg-*.patch; do
        git -C "$src" apply "$patch"
        echo "applied: $(basename "$patch")"
    done
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
        --extra-cflags="-O2 $FF_CFLAGS"
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
        ${MPV_PLATFORM[@]+"${MPV_PLATFORM[@]}"}
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
