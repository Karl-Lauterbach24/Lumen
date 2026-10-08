#!/usr/bin/env bash
# BD-J menus (disc menus written in Java) end to end without a real disc:
#   tools/make_test_bdj.py   synthetic disc: a film and a small Java menu over it (two buttons)
#   -> bdj_test              libbluray starts the menu in a Java VM, the test presses down and enter
#                            and checks what the menu draws; the film behind it must play
#
#   tools/test_bdj.sh <build-dir> <work-dir>
#
# Needs a JDK (javac, jar) to build the menu, a Java runtime of the program's processor type and
# libbluray's Java archive in the version of the libbluray in use (resources/bdj, or the
# distribution's libbluray-bdj). Where one of them is missing the test says so and is skipped.
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
# Linux build containers have no Java: in CI install what a user of the package gets with it – the
# distribution's libbluray-bdj (libbluray's Java archive in its version) and a Java runtime – plus the
# compiler for the test menu
if [ "$(uname -s)" = "Linux" ] && [ "${CI:-}" = "true" ] && [ "$(id -u)" = "0" ] && ! command -v javac > /dev/null 2>&1; then
    if command -v apt-get > /dev/null 2>&1; then
        DEBIAN_FRONTEND=noninteractive apt-get install -y -q --no-install-recommends default-jdk-headless libbluray-bdj > "${TMPDIR:-/tmp}/bdj-install.log" 2>&1 || true
        echo "BD-J menu: libbluray-bdj needs: $(apt-cache depends libbluray-bdj 2> /dev/null | sed -n 's/^ *Depends: //p' | tr '\n' ' ')"
    elif command -v dnf > /dev/null 2>&1; then
        dnf install -y -q java-devel libbluray-bdj > "${TMPDIR:-/tmp}/bdj-install.log" 2>&1 || true
        echo "BD-J menu: libbluray-bdj needs: $(dnf -q repoquery --installed --requires libbluray-bdj 2> /dev/null | tr '\n' ' ')"
    fi
    ls /usr/share/java/libbluray* 2> /dev/null || echo "BD-J menu: no libbluray archive in /usr/share/java"
fi
if [ -n "${JAVA_HOME:-}" ] && ! command -v javac > /dev/null 2>&1; then
    # MSYS2 does not take over the Windows PATH; JAVA_HOME is a Windows path there
    home="$JAVA_HOME"
    if command -v cygpath > /dev/null 2>&1; then home="$(cygpath -u "$JAVA_HOME")"; fi
    PATH="$PATH:$home/bin"
fi
if ! command -v javac > /dev/null 2>&1 || ! command -v jar > /dev/null 2>&1; then
    echo "BD-J menu: no JDK (javac, jar) - skipped"
    exit 0
fi
API="$(ls "$HERE"/resources/bdj/libbluray-j2se-*.jar | head -1)"

rm -rf "$WORK"
mkdir -p "$WORK"
"$PY" "$HERE/tools/make_test_bdj.py" "$WORK/disc" --jar "$API" --seconds 5
# macOS and Windows packages carry their own Java runtime (tools/make_jre.py): test with one built the
# same way, so the test says what the package will do. Linux uses the Java of the system.
case "$(uname -s)" in
    Linux) ;;
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
