#!/usr/bin/env bash
# Does Lumen start on a Linux desktop the way a user starts it? The package builds run the
# installed program with Qt Quick's software renderer under a virtual X server, in the container
# that built it – with every development package present. This script goes closer to a real system:
#
#   1. X11, Qt Quick on OpenGL (Mesa llvmpipe)            -> control window must render
#   2. Wayland (weston, headless), Qt Quick on OpenGL     -> control window must render
#   3. which distribution packages the running program has loaded files from, and which of them
#      the package's declared dependencies do not pull in (QML modules, Qt plugins: nothing links
#      to them, so no tool finds them)
#
#   tools/test_start_linux.sh <build-dir> <work-dir>
#
# Report only: prints what it finds and always exits 0. In CI (root in a container) it installs
# weston and the Qt Wayland plugin for step 2.
set -uo pipefail
BUILD="$1"
WORK="$2"
[ "$(uname -s)" = "Linux" ] || exit 0
[ -x "$BUILD/lumen" ] || { echo "start test: $BUILD/lumen not built - skipped"; exit 0; }
mkdir -p "$WORK"
HERE="$(cd "$(dirname "$0")/.." && pwd)"

have() { command -v "$1" > /dev/null 2>&1; }
owner() { # package that owns a file
    if have dpkg-query; then dpkg-query -S "$1" 2> /dev/null | head -1 | cut -d: -f1
    else rpm -qf --qf '%{NAME}\n' "$1" 2> /dev/null | grep -v ' ' | head -1; fi
}

# run <name> <seconds> <command...>: starts Lumen, waits for the control window's picture
run() {
    local name="$1"; shift
    local png="$WORK/$name.png" log="$WORK/$name.log" maps="$WORK/$name.maps"
    rm -f "$png" "$maps"
    LUMEN_APP_NAME=LumenStartTest LUMEN_NO_DRIVES=1 LUMEN_SNAPSHOT="$png" LUMEN_SNAPSHOT_DELAY=4000 LUMEN_QUIT_AFTER=14 \
        "$@" > "$log" 2>&1 &
    local pid=$!
    for _ in $(seq 1 30); do
        [ -s "$png" ] && break
        kill -0 "$pid" 2> /dev/null || break
        sleep 1
    done
    # the files the program has mapped: libraries and plugins it really uses
    local lumen; lumen="$(pgrep -n -x lumen || true)"
    [ -n "$lumen" ] && [ -r "/proc/$lumen/maps" ] && awk '$6 ~ /^\// {print $6}' "/proc/$lumen/maps" | sort -u > "$maps"
    # it ends by itself (LUMEN_QUIT_AFTER); a program that does not must not hold up the build
    for _ in $(seq 1 40); do kill -0 "$pid" 2> /dev/null || break; sleep 1; done
    if kill -0 "$pid" 2> /dev/null; then
        echo "::warning::start test [$name]: did not end by itself - stopped"
        pkill -9 -x lumen 2> /dev/null; kill -9 "$pid" 2> /dev/null
    fi
    wait "$pid" 2> /dev/null; local rc=$?
    if [ -s "$png" ]; then
        echo "start test [$name]: control window rendered (exit $rc)"
    else
        echo "::warning::start test [$name]: no control window (exit $rc)"
        echo "::group::start test [$name] log"; tail -120 "$log" | cut -c1-300; echo "::endgroup::"
    fi
    grep -iE "qt\.qpa|wayland|could not|cannot|failed|not installed|is not a type|module .* not|error|warning" "$log" | sort | uniq -c | sort -rn | head -15 || true
}

# watched <name> <command...>: one start under gdb. Returns 1 when the program had to be interrupted.
watched() {
    local name="$1"; shift
    local log="$WORK/$name.log"
    ( sleep 45; p="$(pgrep -n -x lumen)"; [ -n "$p" ] && kill -INT "$p" ) > /dev/null 2>&1 &
    local dog=$!
    LUMEN_APP_NAME=LumenStartTest LUMEN_NO_DRIVES=1 LUMEN_QUIT_AFTER=8 \
        timeout 120 gdb -q -batch -ex "set pagination off" -ex "set debuginfod enabled off" -ex "handle SIGPIPE nostop noprint pass" \
        -ex run -ex "thread apply all bt 18" --args "$@" > "$log" 2>&1
    kill "$dog" 2> /dev/null; wait "$dog" 2> /dev/null
    if grep -qE "exited normally|exited with code" "$log"; then return 0; fi
    echo "::warning::start test [$name]: did not end by itself"
    echo "::group::start test [$name]: where the threads stand"
    grep -vE "^\[(New|Thread|Detaching)|^warning:|IconImage|^$" "$log" | tail -260 | cut -c1-200
    echo "::endgroup::"
    pkill -9 -x lumen 2> /dev/null
    return 1
}

export LD_LIBRARY_PATH="$HERE/3rdparty/prefix/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export LIBGL_ALWAYS_SOFTWARE=1

# --- 1. X11 -----------------------------------------------------------------------------------
if have xvfb-run; then
    QT_QPA_PLATFORM=xcb run x11-opengl xvfb-run -a -s "-screen 0 1600x900x24" "$BUILD/lumen"
    QT_QPA_PLATFORM=xcb QT_QUICK_BACKEND=software run x11-software xvfb-run -a -s "-screen 0 1600x900x24" "$BUILD/lumen"
else
    echo "start test: no xvfb-run - X11 skipped"
fi

# --- 2. Wayland ---------------------------------------------------------------------------------
if [ "${CI:-}" = "true" ] && [ "$(id -u)" = "0" ] && ! have weston; then
    if have apt-get; then
        DEBIAN_FRONTEND=noninteractive apt-get install -y -q --no-install-recommends weston qt6-wayland > "$WORK/install.log" 2>&1 || true
    elif have dnf; then
        dnf install -y -q weston qt6-qtwayland > "$WORK/install.log" 2>&1 || true
    fi
fi
if have weston; then
    # an absolute folder of our own with mode 0700, as libwayland demands
    XDG_RUNTIME_DIR="$(mktemp -d)"; export XDG_RUNTIME_DIR
    chmod 700 "$XDG_RUNTIME_DIR"
    weston --backend=headless --renderer=gl --socket=lumen-test --width=1600 --height=900 > "$WORK/weston.log" 2>&1 &
    WESTON=$!
    sleep 2
    if ! kill -0 "$WESTON" 2> /dev/null; then
        # older weston or no EGL for it: software compositing; clients still use their own OpenGL
        weston --backend=headless --socket=lumen-test --width=1600 --height=900 > "$WORK/weston.log" 2>&1 &
        WESTON=$!
    fi
    for _ in $(seq 1 20); do [ -S "$XDG_RUNTIME_DIR/lumen-test" ] && break; sleep 0.5; done
    if [ "${CI:-}" = "true" ] && [ "$(id -u)" = "0" ] && ! have gdb; then
        { have apt-get && DEBIAN_FRONTEND=noninteractive apt-get install -y -q --no-install-recommends gdb > /dev/null 2>&1; } \
            || { have dnf && dnf install -y -q gdb > /dev/null 2>&1; } || true
    fi
    if [ -S "$XDG_RUNTIME_DIR/lumen-test" ]; then
        WAYLAND_DISPLAY=lumen-test QT_QPA_PLATFORM=wayland run wayland-opengl "$BUILD/lumen"
        WAYLAND_DISPLAY=lumen-test QT_QPA_PLATFORM=wayland QT_QUICK_BACKEND=software run wayland-software "$BUILD/lumen"
        # a picture in the player window: a generated test pattern, grabbed after six seconds
        rm -f "$WORK/wayland-player.png"
        LUMEN_PLAYER_SNAPSHOT="$WORK/wayland-player.png@+7" WAYLAND_DISPLAY=lumen-test QT_QPA_PLATFORM=wayland \
            run wayland-play "$BUILD/lumen" "av://lavfi:testsrc2=size=1280x720:rate=24"
        if [ -s "$WORK/wayland-player.png" ]; then echo "start test [wayland-play]: player window rendered"
        else echo "::warning::start test [wayland-play]: no picture from the player window"; fi
        # Start and end, several times and watched: once in about seventy runs the program did not
        # end by itself on Wayland. Under gdb a run that is still there after 45 s is interrupted and
        # every thread says where it stands.
        if have gdb; then
            hung=0
            for i in $(seq 1 "${LUMEN_START_TEST_REPEAT:-6}"); do
                WAYLAND_DISPLAY=lumen-test QT_QPA_PLATFORM=wayland QT_QUICK_BACKEND=software watched "wayland-software-$i" "$BUILD/lumen" || hung=$((hung + 1))
                WAYLAND_DISPLAY=lumen-test QT_QPA_PLATFORM=wayland watched "wayland-opengl-$i" "$BUILD/lumen" || hung=$((hung + 1))
                WAYLAND_DISPLAY=lumen-test QT_QPA_PLATFORM=wayland watched "wayland-play-$i" "$BUILD/lumen" "av://lavfi:testsrc2=size=1280x720:rate=24" || hung=$((hung + 1))
            done
            echo "start test [wayland, watched]: $hung of $((3 * ${LUMEN_START_TEST_REPEAT:-6})) runs did not end by themselves"
        fi
        # mpv's own window on Wayland (profile choice "native"; the default until 1.3.2). It ended the
        # program at start when mpv got no graphics context of its own (tools/patches/mpv-wayland-egl-uninit.patch);
        # with the patch it must end normally, with or without a picture.
        if have gdb; then
            LUMEN_PLAYER_WINDOW=native LUMEN_APP_NAME=LumenStartTest LUMEN_NO_DRIVES=1 LUMEN_QUIT_AFTER=8 WAYLAND_DISPLAY=lumen-test \
                QT_QPA_PLATFORM=wayland timeout 60 gdb -q -batch -ex "set pagination off" -ex "set debuginfod enabled off" \
                -ex run -ex "bt 16" --args "$BUILD/lumen" > "$WORK/wayland-native.log" 2>&1
            if grep -qE "Assertion|SIGABRT|SIGSEGV|SIGBUS" "$WORK/wayland-native.log"; then
                echo "::warning::start test [wayland-native]: mpv's own window ends the program"
                echo "::group::start test [wayland-native] backtrace"
                grep -vE "^\[(New|Thread|Detaching)|warning:|IconImage" "$WORK/wayland-native.log" | tail -30 | cut -c1-220
                echo "::endgroup::"
            else
                echo "start test [wayland-native]: mpv's own window does not end the program ($(grep -cE 'exited normally|exited with code' "$WORK/wayland-native.log") normal exit)"
            fi
        fi
    else
        echo "start test: weston did not start - Wayland skipped"; tail -5 "$WORK/weston.log"
    fi
    kill "$WESTON" 2> /dev/null
else
    echo "start test: no weston - Wayland skipped"
fi

# --- 3. packages in use vs. packages the dependencies pull in -----------------------------------
cat "$WORK"/*.maps 2> /dev/null | sort -u > "$WORK/files.txt"
: > "$WORK/used.txt"
while read -r f; do
    case "$f" in "$HERE"/*|/tmp/*|/dev/*|/memfd*|/run/*|/home/*) continue ;; esac
    [ -e "$f" ] || continue
    p="$(owner "$f")"; [ -n "$p" ] && echo "$p" >> "$WORK/used.txt"
done < "$WORK/files.txt"
sort -u -o "$WORK/used.txt" "$WORK/used.txt"
echo "start test: $(wc -l < "$WORK/files.txt") files mapped, from $(wc -l < "$WORK/used.txt") packages"

# what the package declares (CMakeLists.txt) plus what the linker finds for lumen and its own libraries
if have dpkg-query; then
    declared="$(sed -n 's/.*CPACK_DEBIAN_PACKAGE_DEPENDS "\(.*\)").*/\1/p' "$HERE/CMakeLists.txt" | tr ',' '\n' | sed 's/^ *//; s/[ (].*//' | grep -v '^$')"
else
    declared="$(sed -n 's/.*CPACK_RPM_PACKAGE_REQUIRES "\(.*\)").*/\1/p' "$HERE/CMakeLists.txt" | tr ',' '\n' | sed 's/^ *//; s/ .*//' | grep -v '^$')"
fi
echo "start test: declared dependencies: $(echo $declared)"
linked="$( { ldd "$BUILD/lumen"; for l in "$HERE"/3rdparty/prefix/lib/*.so.*; do [ -f "$l" ] && ldd "$l"; done; } 2> /dev/null \
    | awk '$3 ~ /^\// {print $3}' | sort -u | while read -r f; do case "$f" in "$HERE"/*) ;; *) owner "$(readlink -f "$f")" ;; esac; done | sort -u)"
roots="$(printf '%s\n%s\n' "$declared" "$linked" | grep -v '^$' | sort -u)"
if have apt-cache; then
    closure="$(apt-cache depends --recurse --no-recommends --no-suggests --no-conflicts --no-breaks --no-replaces --no-enhances $roots 2> /dev/null \
        | grep -v '^ ' | sed 's/:.*//' | sort -u)"
else
    closure="$(dnf -q repoquery --installed --requires --resolve --recursive --qf '%{name}\n' $roots 2> /dev/null | sort -u)"
    closure="$(printf '%s\n%s\n' "$closure" "$roots" | sort -u)"
fi
missing="$(comm -23 "$WORK/used.txt" <(echo "$closure"))"
if [ -n "$missing" ]; then
    echo "::warning::start test: packages in use that the dependencies do not pull in: $(echo $missing)"
else
    echo "start test: every package in use is pulled in by the declared dependencies"
fi
echo "::group::start test: packages in use"; cat "$WORK/used.txt"; echo "::endgroup::"
exit 0
