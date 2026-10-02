#!/bin/bash
# Vorübergehend (Fehlersuche): viele kurze Render-Sitzungen in mehreren Varianten, das Ergebnis
# als Annotationen (ohne Anmeldung per API lesbar).
#   tools/ci_rendercheck.sh <cast_test> <sync.mp4> [Programm davor, z. B. xvfb-run -a]
set +e
TEST=$1; FILE=$2; shift 2
variant() {  # <name> <runden> <LUMEN_CAST_EXP> [argumente...]
    local name=$1 rounds=$2 exp=$3; shift 3
    local out
    out=$(LUMEN_CAST_EXP="$exp" "${WRAP[@]}" "$TEST" --rendercheck "$FILE" "$rounds" "$@" 2>/dev/null | grep "^RC")
    echo "== $name"; echo "$out"
    local body
    body=$(echo "$out" | cut -c1-1500 | sed 's/%/%25/g' | awk '{printf "%s%%0A", $0}')
    echo "::notice title=rc-$name::$body"
}
WRAP=("$@")
variant basis 12 ""
variant dumb 12 "" gpu-dumb-mode=yes
variant rgba8 12 "" fbo-format=rgba8
variant fast 12 "" profile=fast
variant nodither 12 "" dither-depth=no
variant cbilinear 12 "" cscale=bilinear
variant clean 12 clean
variant f444 12 "" vf=format=yuv444p
variant f420 12 "" vf=format=yuv420p
variant nv12 12 "" vf=format=nv12
variant gray 12 "" vf=format=gray
exit 0
