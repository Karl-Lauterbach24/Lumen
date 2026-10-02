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
variant basis 14 ""
variant threads1 12 "" vd-lavc-threads=1
variant swscale 12 "" vf=format=rgb24
# unter Last: alle Kerne beschäftigt
pids=""
for i in 1 2 3 4 5 6; do ( while :; do :; done ) & pids="$pids $!"; done
variant last 8 ""
kill $pids 2>/dev/null
exit 0
