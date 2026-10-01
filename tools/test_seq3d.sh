#!/usr/bin/env bash
# Frame-sequential 3D (shutter glasses): applies the filter chains Lumen builds to a small
# side-by-side clip (left eye white, right eye grey) with the FFmpeg in PATH and checks the
# order of the resulting pictures.
#
#   tools/test_seq3d.sh <build-dir> <work-dir>
set -euo pipefail
BUILD="$1"
WORK="$2"
mkdir -p "$WORK"
SRC="$WORK/sbs.y4m"
ffmpeg -hide_banner -loglevel error -y -f lavfi \
       -i "color=c=white:s=64x32:r=24,drawbox=x=32:y=0:w=32:h=32:c=gray@1:t=fill" -t 1 -pix_fmt yuv420p "$SRC"

fail=0
# check <name> <expected: "centre,corner" per picture, space separated> <stereo_test arguments...>
check() {
    local name="$1" expected="$2"; shift 2
    local chain got count
    chain="$("$BUILD/stereo_test" "$@")"
    # every picture: 32x32 grey; field 529 = centre, field 1 = top left corner
    got="$(ffmpeg -hide_banner -loglevel error -i "$SRC" -vf "$chain" -f rawvideo -pix_fmt gray - \
           | od -An -tu1 -w1024 -v | awk '{printf "%s,%s ", $529, $1}')"
    count="$(echo "$got" | wc -w | tr -d ' ')"
    local n; n="$(echo "$expected" | wc -w | tr -d ' ')"
    if [ "$(echo "$got" | cut -d' ' -f1-"$n")" = "$expected" ] && [ "$count" -ge "$2" ] && [ "$count" -le $(( $2 + 2 )) ]; then
        echo "OK   $name ($count pictures)"
    else
        echo "FAIL $name"
        echo "     chain:    $chain"
        echo "     expected: $expected  (about $2 pictures)"
        echo "     got:      $(echo "$got" | cut -d' ' -f1-8) ... ($count pictures)"
        fail=1
    fi
}

# centre: 255 = left eye, 128 = right eye, 76 = red sync picture, 0 = black
check "L R at 120 Hz"                        "255,255 128,128 255,255 128,128" sbsl 120 LR
check "eyes swapped"                         "128,128 255,255 128,128 255,255" sbsl 120 LR swap
check "pattern shifted by one picture"       "128,128 255,255 128,128 255,255" sbsl 120 LR phase=1
check "L S R S at 240 Hz, red sync pictures" "255,255 76,76 128,128 76,76 255,255 76,76" sbsl 240 LSRS
check "white sync pictures at half level"    "255,255 127,127 128,128 127,127" sbsl 240 LSRS color=#ffffff level=50
check "L B R B: black pictures"              "255,255 0,0 128,128 0,0" sbsl 240 LBRB
check "trigger box: white for L, else black" "255,255 128,0 255,255 128,0" sbsl 120 LR box=tl size=20
check "box with sync pictures"               "255,255 76,0 128,0 76,0 255,255" sbsl 240 LSRS box=tl size=20
check "right eye first in the source"        "128,128 255,255 128,128" sbsr 120 LR
[ "$fail" = 0 ] && echo "PASSED" || { echo "FAILED"; exit 1; }
