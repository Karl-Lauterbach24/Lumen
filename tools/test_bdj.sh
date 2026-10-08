#!/usr/bin/env bash
# BD-J menus (disc menus written in Java) end to end without a real disc:
#   tools/make_test_bdj.py   synthetic disc: a film and a small Java menu over it (two buttons)
#   -> bdj_test              libbluray starts the menu in a Java VM, the test presses down and enter
#                            and checks what the menu draws; the film behind it must play
#
#   tools/test_bdj.sh <build-dir> <work-dir>
#
# Runs with the Java runtime the packages carry: on Linux the one tools/build_deps.sh made
# (3rdparty/prefix/jre), on macOS and Windows one cut the same way from a JDK of this machine
# (tools/make_jre.py). Without one it uses the Java of the system; without any the test says so and
# is skipped. The menu itself lies ready (tests/data/bdj/menu.jar), no Java compiler is needed.
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$1"
WORK="$2"
PY="$(command -v python3 || command -v python)"

T="$BUILD/bdj_test"; [ -x "$T" ] || T="$BUILD/bdj_test.exe"
if [ ! -x "$T" ]; then
    echo "BD-J menu: bdj_test not built - skipped"
    exit 0
fi
rm -rf "$WORK"
mkdir -p "$WORK"
"$PY" "$HERE/tools/make_test_bdj.py" "$WORK/disc" --seconds 5
# test with the runtime as the packages carry it, so the test says what the package will do
case "$(uname -s)" in
    Linux)
        # Java's window library in that runtime links to libXtst and libXi (tools/make_jre.py). A build
        # container may have neither (Debian's has not): the menu could not start here, and the
        # package's dependency scan (dpkg-shlibdeps), which comes after the tests, would stop.
        if [ "${CI:-}" = "true" ] && [ "$(id -u)" = "0" ] && command -v ldconfig > /dev/null 2>&1 \
            && ! { ldconfig -p | grep -q 'libXtst\.so\.6' && ldconfig -p | grep -q 'libXi\.so\.6'; }; then
            if command -v apt-get > /dev/null 2>&1; then
                DEBIAN_FRONTEND=noninteractive apt-get install -y -q --no-install-recommends libxtst6 libxi6 > /dev/null 2>&1 || true
            elif command -v dnf > /dev/null 2>&1; then
                dnf install -y -q libXtst libXi > /dev/null 2>&1 || true
            fi
            ldconfig -p | grep -q 'libXtst\.so\.6' && echo "BD-J menu: installed libXtst and libXi for the Java runtime"
        fi
        # the runtime tools/build_deps.sh made for the packages
        if [ -f "$HERE/3rdparty/prefix/jre/lumen-jre.txt" ]; then
            export LUMEN_DEPS_JRE="$HERE/3rdparty/prefix/jre"
            echo "BD-J menu: with the runtime the packages carry ($(cut -d' ' -f1 "$LUMEN_DEPS_JRE/lumen-jre.txt"))"
        fi ;;
    *)
        "$PY" "$HERE/tools/make_jre.py" "$WORK/jre" || true
        if [ -f "$WORK/jre/lumen-jre.txt" ]; then
            export JAVA_HOME="$WORK/jre"
            echo "BD-J menu: with the runtime the packages carry ($(cut -d' ' -f1 "$WORK/jre/lumen-jre.txt"))"
        fi ;;
esac
# with a time limit: a Java VM that hangs must not hold up the build
if command -v timeout > /dev/null 2>&1; then
    limit=(timeout 240)
else
    limit=(perl -e 'alarm 240; exec @ARGV')
fi
status=0
"${limit[@]}" "$T" "$WORK/disc" "$HERE/resources/bdj" "$WORK/menu.png" > "$WORK/bdj.log" 2>&1 || status=$?
# the Java VM is talkative; show what the test itself says
grep -E '^(LIBBLURAY_CP|JAVA_HOME|BD-J|menu drawn|key |film behind|waited for|the film|cannot open|not a BD-J|bd_play)' "$WORK/bdj.log" || true
case "$status" in
    0) ;;
    77) echo "BD-J menu: skipped" ;;
    *) echo "::group::BD-J log"; tail -60 "$WORK/bdj.log"; echo "::endgroup::"
       echo "::error::BD-J menu test failed"; exit 1 ;;
esac
