#!/usr/bin/env bash
# Does Lumen start on a Linux desktop the way a user starts it? The package builds run the
# installed program with Qt Quick's software renderer under a virtual X server, in the container
# that built it – with every development package present. This script goes closer to a real system:
#
#   1. X11, Qt Quick on OpenGL (Mesa llvmpipe)            -> control window must render
#   2. Wayland (weston, headless), Qt Quick on OpenGL     -> control window must render
#   3. sound through PipeWire: with a PipeWire daemon and WirePlumber started for the test, Lumen
#      plays a tone that must arrive at the sink; without a service, the connection that fails
#      must not hold the program (tools/patches/mpv-pipewire-start-after-connect.patch)
#   4. which distribution packages the running program has loaded files from, and which of them
#      the package's declared dependencies do not pull in (QML modules, Qt plugins: nothing links
#      to them, so no tool finds them)
#
#   tools/test_start_linux.sh <build-dir> <work-dir>
#
# Report only: prints what it finds and always exits 0. In CI (root in a container) it installs
# weston and the Qt Wayland plugin for step 2, PipeWire, WirePlumber and a D-Bus daemon for step 3.
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

# watched <name> <command...>: one start as in run(), left alone. Only when the program is still
# there 45 s later does a debugger look where its threads stand: the shell that started it becomes
# gdb, because a container lets nothing but a parent attach. Returns 1 for such a run.
watched() {
    local name="$1"; shift
    local log="$WORK/$name.log"
    rm -f "$log.stacks"
    LUMEN_APP_NAME=LumenStartTest LUMEN_NO_DRIVES=1 LUMEN_SNAPSHOT="$WORK/watched.png" LUMEN_SNAPSHOT_DELAY=4000 LUMEN_QUIT_AFTER=14 \
        timeout 180 sh -c '
            log="$1"; shift
            "$@" > "$log" 2>&1 &
            pid=$!
            for _ in $(seq 1 45); do
                if ! kill -0 "$pid" 2> /dev/null || grep -q "^State:[[:space:]]*Z" "/proc/$pid/status" 2> /dev/null; then exit 0; fi
                sleep 1
            done
            exec gdb -q -batch -p "$pid" -ex "set pagination off" -ex "set debuginfod enabled off" -ex "thread apply all bt 18" > "$log.stacks" 2>&1
        ' sh "$log" "$@"
    [ -e "$log.stacks" ] || return 0
    echo "::warning::start test [$name]: did not end by itself"
    echo "::group::start test [$name]: where the threads stand"
    grep -vE "^\[(New|Thread|Detaching)|^warning:|^$" "$log.stacks" | tail -260 | cut -c1-200
    echo "--- its own output"; grep -v IconImage "$log" | tail -20 | cut -c1-200
    echo "::endgroup::"
    pkill -9 -x lumen 2> /dev/null
    return 1
}

# pipewire_sound: Lumen with a PipeWire that runs (called while weston is up). The build machines
# have no sound service, so there mpv's PipeWire output only ever fails to connect. Here a PipeWire
# daemon and WirePlumber run in the test's runtime folder, with one sink that leads nowhere. Lumen
# is started alone (mpv connects to list the sound devices and keeps that connection to the end) and
# with a file: a picture and six seconds of a 440 Hz tone, while pw-record listens at the sink.
# Wanted: mpv opens the PipeWire output, the tone arrives, the program ends by itself.
pipewire_sound() {
    local pw="$WORK/pipewire" ffmpeg="$HERE/3rdparty/prefix/bin/ffmpeg"
    rm -rf "$pw"; mkdir -p "$pw/config/pipewire/pipewire.conf.d" "$pw/state"
    [ -x "$ffmpeg" ] || ffmpeg="$(command -v ffmpeg || true)"
    if [ "${CI:-}" = "true" ] && [ "$(id -u)" = "0" ] && ! { have pipewire && have wireplumber && have pw-record; }; then
        if have apt-get; then
            DEBIAN_FRONTEND=noninteractive apt-get install -y -q --no-install-recommends pipewire pipewire-bin wireplumber > "$pw/install.log" 2>&1 || true
            DEBIAN_FRONTEND=noninteractive apt-get install -y -q --no-install-recommends dbus-daemon dbus-session-bus-common >> "$pw/install.log" 2>&1 || true
        elif have dnf; then
            dnf install -y -q --setopt=install_weak_deps=False pipewire pipewire-utils wireplumber dbus-daemon > "$pw/install.log" 2>&1 || true
        fi
    fi
    if ! { have pipewire && have wireplumber && have pw-record && [ -n "$ffmpeg" ]; }; then
        echo "start test [pipewire]: no pipewire, wireplumber or pw-record - sound through PipeWire skipped"
        return
    fi
    cat > "$pw/config/pipewire/pipewire.conf.d/50-lumen-test.conf" << 'EOF'
context.objects = [
    { factory = adapter
        args = {
            factory.name     = support.null-audio-sink
            node.name        = "lumen-test"
            node.description = "Lumen test sink"
            media.class      = Audio/Sink
            audio.position   = [ FL FR ]
        }
    }
]
EOF
    # the daemons get their own configuration and state folders and a session bus (WirePlumber asks
    # for one); Lumen runs as in the other steps and finds PipeWire through the runtime folder
    local bus="" daemon session denv=(XDG_CONFIG_HOME="$pw/config" XDG_STATE_HOME="$pw/state")
    if have dbus-daemon; then
        dbus-daemon --session --nofork --nopidfile --address="unix:path=$XDG_RUNTIME_DIR/lumen-test-bus" > "$pw/dbus.log" 2>&1 &
        bus=$!
        denv+=(DBUS_SESSION_BUS_ADDRESS="unix:path=$XDG_RUNTIME_DIR/lumen-test-bus")
        for _ in $(seq 1 20); do [ -S "$XDG_RUNTIME_DIR/lumen-test-bus" ] && break; sleep 0.25; done
    fi
    env "${denv[@]}" pipewire > "$pw/pipewire.log" 2>&1 &
    daemon=$!
    for _ in $(seq 1 40); do [ -S "$XDG_RUNTIME_DIR/pipewire-0" ] && break; sleep 0.25; done
    env "${denv[@]}" wireplumber > "$pw/wireplumber.log" 2>&1 &
    session=$!
    # ready when WirePlumber has made the sink the default one
    local ready=0
    for _ in $(seq 1 60); do
        if wpctl inspect @DEFAULT_AUDIO_SINK@ 2> /dev/null | grep -q 'node.name = "lumen-test"'; then ready=1; break; fi
        kill -0 "$daemon" 2> /dev/null && kill -0 "$session" 2> /dev/null || break
        sleep 0.5
    done
    if [ "$ready" = 0 ]; then
        echo "::warning::start test [pipewire]: no PipeWire with a default sink came up - sound through PipeWire not checked"
        echo "::group::start test [pipewire]: what the daemons said"
        tail -n 25 "$pw/install.log" "$pw/dbus.log" "$pw/pipewire.log" "$pw/wireplumber.log" 2> /dev/null | cut -c1-240
        wpctl status 2>&1 | head -40
        echo "::endgroup::"
    else
        echo "start test [pipewire]: PipeWire $(pipewire --version | sed -n 's/^Linked with libpipewire //p') and WirePlumber $(wireplumber --version | sed -n 's/^Linked with libwireplumber //p') run, default sink: lumen-test"
        "$ffmpeg" -hide_banner -loglevel error -y -f lavfi -i "testsrc2=size=640x360:rate=24" -f lavfi -i "sine=frequency=440:sample_rate=48000" \
            -t 6 -c:v libx264 -preset ultrafast -pix_fmt yuv420p -c:a pcm_s16le -ac 2 "$pw/tone.mkv"
        local stood=0 i rec log ao tenths seconds peak links
        for i in 1 2; do
            log="$WORK/pipewire-start-$i.log"
            LUMEN_MPV_LOG=v WAYLAND_DISPLAY=lumen-test QT_QPA_PLATFORM=wayland watched "pipewire-start-$i" "$BUILD/lumen" || stood=$((stood + 1))
            if grep -q "ao/pipewire: Core version" "$log"; then
                echo "start test [pipewire-start-$i]: mpv connected to PipeWire ($(grep -m1 -o "Core version: .*" "$log" | cut -c1-40)) and the program ended"
            else
                echo "::warning::start test [pipewire-start-$i]: no PipeWire connection in mpv's log"
            fi
            log="$WORK/pipewire-play-$i.log"
            pw-record --target lumen-test -P '{ stream.capture.sink = true }' --rate 48000 --channels 2 --format s16 "$pw/monitor-$i.wav" > "$pw/record-$i.log" 2>&1 &
            rec=$!
            ( sleep 7; pw-link -l > "$pw/links-$i.txt" 2>&1 ) &
            LUMEN_MPV_LOG=v WAYLAND_DISPLAY=lumen-test QT_QPA_PLATFORM=wayland watched "pipewire-play-$i" "$BUILD/lumen" "$pw/tone.mkv" || stood=$((stood + 1))
            kill -INT "$rec" 2> /dev/null; wait "$rec" 2> /dev/null
            ao="$(grep -m1 -o "AO: \[[a-z]*\].*" "$log" | cut -c1-60)"
            # tenths of a second in which the tone is in the recording (left channel), its length, the highest sample
            read -r tenths seconds peak < <("$ffmpeg" -hide_banner -loglevel error -i "$pw/monitor-$i.wav" -af "pan=mono|c0=c0" -f s16le -ar 48000 - 2> /dev/null | python3 -c "
import array, sys
d = sys.stdin.buffer.read()
a = array.array('h'); a.frombytes(d[:len(d) // 2 * 2])
print(sum(1 for i in range(0, len(a) - 4799, 4800) if max(a[i:i + 4800]) > 1000), '%.1f' % (len(a) / 48000), max(a) if a else 0)")
            links="$(grep -B1 -- "|-> lumen-test:playback" "$pw/links-$i.txt" 2> /dev/null | grep -v -- "^--" | tr -s ' \n' ' ')"
            if [ "${ao#AO: \[pipewire\]}" != "$ao" ] && [ "${tenths:-0}" -ge 30 ]; then
                echo "start test [pipewire-play-$i]: $ao - $((tenths / 10)).$((tenths % 10)) s of the tone arrived at the sink (highest sample $peak of 32767, ${seconds} s recorded)"
            else
                echo "::warning::start test [pipewire-play-$i]: sound did not get through PipeWire (mpv: ${ao:-no audio output}; tone in the recording: ${tenths:-0} tenths of a second of ${seconds:-0} s)"
            fi
            echo "::group::start test [pipewire-play-$i]: links at the sink after 7 s, mpv's sound output"
            echo "links: ${links:-none}"
            grep -E "mpv (ao|ao/[a-z]+|cplayer): " "$log" | grep -E "ao/|AO:|[Aa]udio" | sed 's/^.*mpv /mpv /' | cut -c1-200 | sort | uniq -c | sort -rn | head -30
            tail -n 5 "$pw/record-$i.log" 2> /dev/null | cut -c1-200
            echo "::endgroup::"
        done
        echo "start test [pipewire]: $stood of 4 runs with a running PipeWire did not end by themselves"
    fi
    kill "$session" "$daemon" $bus 2> /dev/null
    wait "$session" "$daemon" $bus 2> /dev/null
    rm -f "$XDG_RUNTIME_DIR"/pipewire-0* "$XDG_RUNTIME_DIR/lumen-test-bus"
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
        # Start and end, several times and watched. Found this way (4 of 360 runs, Fedora 44 with
        # PipeWire 1.6.9 only, no PipeWire service in the container): mpv's core stands in
        # pw_thread_loop_stop() after a failed connection (ao_pipewire: hotplug_init -> uninit), and
        # the program neither goes on nor ends. Since tools/patches/mpv-pipewire-start-after-connect.patch
        # mpv no longer starts that thread without a connection. LUMEN_START_TEST_REPEAT=30 repeats the search.
        if have gdb; then
            hung=0
            for i in $(seq 1 "${LUMEN_START_TEST_REPEAT:-30}"); do
                WAYLAND_DISPLAY=lumen-test QT_QPA_PLATFORM=wayland QT_QUICK_BACKEND=software watched "wayland-software-$i" "$BUILD/lumen" || hung=$((hung + 1))
                WAYLAND_DISPLAY=lumen-test QT_QPA_PLATFORM=wayland watched "wayland-opengl-$i" "$BUILD/lumen" || hung=$((hung + 1))
                WAYLAND_DISPLAY=lumen-test QT_QPA_PLATFORM=wayland watched "wayland-play-$i" "$BUILD/lumen" "av://lavfi:testsrc2=size=1280x720:rate=24" || hung=$((hung + 1))
            done
            echo "start test [wayland, watched]: $hung of $((3 * ${LUMEN_START_TEST_REPEAT:-30})) runs did not end by themselves"
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
        # --- 3. PipeWire, with a service ----------------------------------------------------------
        have gdb && pipewire_sound
    else
        echo "start test: weston did not start - Wayland skipped"; tail -5 "$WORK/weston.log"
    fi
    kill "$WESTON" 2> /dev/null
else
    echo "start test: no weston - Wayland skipped"
fi

# --- 3. PipeWire, without a service -------------------------------------------------------------
# The connection that fails, many times and quickly, with a folder for the runtime files in which
# no service listens. pw_order makes libpipewire's calls in mpv 0.41's order and in the patched one;
# mpv_devices asks Lumen's libmpv for the sound devices, each time in a new instance.
# rounds <output> <rounds> <seconds each> <command...>: sets PASSES and STOOD (rounds stopped by the clock)
rounds() {
    local out="$1" n="$2" limit="$3" rc; shift 3
    PASSES=0; STOOD=0
    for _ in $(seq 1 "$n"); do
        timeout -s KILL "$limit" "$@" > "$out" 2> /dev/null; rc=$?
        PASSES=$((PASSES + $(wc -l < "$out")))
        case "$rc" in
            0) ;;
            124|137) STOOD=$((STOOD + 1)); [ "$STOOD" -ge 3 ] && break ;;
            *) return "$rc" ;;
        esac
    done
    return 0
}
if have cc && have pkg-config && pkg-config --exists libpipewire-0.3 && [ -f "$HERE/3rdparty/prefix/include/mpv/client.h" ]; then
    P="$WORK/pipewire-none"; rm -rf "$P"; mkdir -p "$P/run"; chmod 700 "$P/run"
    cat > "$P/pw_order.c" << 'EOF'
/* The calls of mpv's PipeWire output when no PipeWire service answers: in mpv 0.41's order
 * ("before": the loop's thread is started, then the connection is tried, fails, and the thread is
 * stopped again) or in the order of tools/patches/mpv-pipewire-start-after-connect.patch ("after":
 * the thread starts once there is a connection, so here it never does). One line per pass. */
#include <pipewire/pipewire.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    int before = argc > 1 && strcmp(argv[1], "before") == 0;
    int n = argc > 2 ? atoi(argv[2]) : 100;
    for (int i = 1; i <= n; i++) {
        pw_init(NULL, NULL);
        struct pw_thread_loop *loop = pw_thread_loop_new("mpv/ao/pipewire", NULL);
        if (!loop)
            return 2;
        pw_thread_loop_lock(loop);
        if (before && pw_thread_loop_start(loop) < 0)
            return 2;
        struct pw_context *context = pw_context_new(pw_thread_loop_get_loop(loop), NULL, 0);
        if (!context)
            return 2;
        if (pw_context_connect(context, pw_properties_new(PW_KEY_REMOTE_NAME, NULL, NULL), 0))
            return 3; /* a PipeWire service answered: not the case this is about */
        pw_context_destroy(context);
        pw_thread_loop_unlock(loop);
        pw_thread_loop_stop(loop);
        pw_thread_loop_destroy(loop);
        pw_deinit();
        printf("%d\n", i);
        fflush(stdout);
    }
    return 0;
}
EOF
    cat > "$P/mpv_devices.c" << 'EOF'
/* What Lumen does at every start: a new libmpv is asked for the sound devices. mpv lets every
 * output look for its devices, PipeWire first; with no service there that connection fails and
 * is taken down again at once. One line per pass; a second argument is mpv's msg-level. */
#include <mpv/client.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    int n = argc > 1 ? atoi(argv[1]) : 100;
    for (int i = 1; i <= n; i++) {
        mpv_handle *mpv = mpv_create();
        if (!mpv)
            return 2;
        mpv_set_option_string(mpv, "load-scripts", "no");
        if (argc > 2) {
            mpv_set_option_string(mpv, "terminal", "yes");
            mpv_set_option_string(mpv, "msg-level", argv[2]);
        }
        if (mpv_initialize(mpv) < 0)
            return 2;
        char *list = mpv_get_property_string(mpv, "audio-device-list");
        if (!list)
            return 2;
        mpv_free(list);
        mpv_terminate_destroy(mpv);
        printf("%d\n", i);
        fflush(stdout);
    }
    return 0;
}
EOF
    # shellcheck disable=SC2046
    if cc -O1 -o "$P/pw_order" "$P/pw_order.c" $(pkg-config --cflags --libs libpipewire-0.3) > "$P/cc.log" 2>&1 \
        && cc -O1 -o "$P/mpv_devices" "$P/mpv_devices.c" -I"$HERE/3rdparty/prefix/include" -L"$HERE/3rdparty/prefix/lib" \
              -Wl,-rpath-link,"$HERE/3rdparty/prefix/lib" -lmpv >> "$P/cc.log" 2>&1; then
        none=(env -u PIPEWIRE_REMOTE -u PIPEWIRE_RUNTIME_DIR XDG_RUNTIME_DIR="$P/run")
        version="$(pkg-config --modversion libpipewire-0.3)"
        if rounds "$P/before.txt" 25 60 "${none[@]}" "$P/pw_order" before 200; then
            echo "start test [pipewire, no service]: libpipewire $version, mpv 0.41's order (thread started, connection fails, thread stopped): $PASSES passes, stood $STOOD times"
        else
            echo "::warning::start test [pipewire, no service]: pw_order before ended with $? after $PASSES passes"
        fi
        if rounds "$P/after.txt" 25 60 "${none[@]}" "$P/pw_order" after 200; then
            [ "$STOOD" = 0 ] || echo "::warning::start test [pipewire, no service]: the patched order stood $STOOD times in $PASSES passes"
            echo "start test [pipewire, no service]: libpipewire $version, patched order (thread starts with a connection only): $PASSES passes, stood $STOOD times"
        else
            echo "::warning::start test [pipewire, no service]: pw_order after ended with $? after $PASSES passes"
        fi
        if rounds "$P/devices.txt" 10 120 "${none[@]}" "$P/mpv_devices" 100; then
            [ "$STOOD" = 0 ] || echo "::warning::start test [pipewire, no service]: Lumen's libmpv stood $STOOD times while asked for the sound devices ($PASSES passes)"
            echo "start test [pipewire, no service]: Lumen's libmpv asked for the sound devices in $PASSES new instances, stood $STOOD times"
            echo "    $("${none[@]}" timeout -s KILL 60 "$P/mpv_devices" 1 "all=no,ao/pipewire=v" 2>&1 | grep -m1 -i "connect" | cut -c1-160)"
        else
            echo "::warning::start test [pipewire, no service]: mpv_devices ended with $? after $PASSES passes"
        fi
    else
        echo "::warning::start test [pipewire, no service]: the test programs did not build"; tail -n 20 "$P/cc.log" | cut -c1-240
    fi
else
    echo "start test [pipewire, no service]: no compiler or no PipeWire headers - skipped"
fi

# --- 4. packages in use vs. packages the dependencies pull in -----------------------------------
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
