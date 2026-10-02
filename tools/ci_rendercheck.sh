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
variant spline16 12 "" cscale=spline16
variant spline36 12 "" cscale=spline36
variant spline64 12 "" cscale=spline64
variant mitchell 12 "" cscale=mitchell
variant bicubic 12 "" cscale=bicubic
variant ewa 12 "" cscale=ewa_lanczos
variant lanczos2 12 "" cscale=lanczos cscale-radius=2
variant lanczos4 12 "" cscale=lanczos cscale-radius=4
variant hermite 12 "" cscale=hermite
variant nocorrect 12 "" correct-downscaling=no linear-downscaling=no sigmoid-upscaling=no
variant antiring 12 "" cscale-antiring=0.6
exit 0
