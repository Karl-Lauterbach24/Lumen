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
if [ -n "${JAVA_HOME:-}" ] && ! command -v javac > /dev/null 2>&1; then
    PATH="$PATH:$JAVA_HOME/bin"
fi
if ! command -v javac > /dev/null 2>&1 || ! command -v jar > /dev/null 2>&1; then
    echo "BD-J menu: no JDK (javac, jar) - skipped"
    exit 0
fi
API="$(ls "$HERE"/resources/bdj/libbluray-j2se-*.jar | head -1)"

rm -rf "$WORK"
mkdir -p "$WORK"
"$PY" "$HERE/tools/make_test_bdj.py" "$WORK/disc" --jar "$API" --seconds 5
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
