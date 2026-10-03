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
PY="$(command -v python3 || command -v python)"
SRC="$WORK/sbs.y4m"
ffmpeg -hide_banner -loglevel error -y -f lavfi \
       -i "color=c=white:s=64x32:r=24,drawbox=x=32:y=0:w=32:h=32:c=gray@1:t=fill" -t 1 -pix_fmt yuv420p "$SRC"

fail=0
# check <name> <expected: "centre,corner" per picture, space separated> <stereo_test arguments...>
check() {
    local name="$1" expected="$2"; shift 2
    local chain got count
    chain="$("$BUILD/stereo_test" "$@")"
    # every picture: 32x32 grey; byte 528 = centre, byte 0 = top left corner
    ffmpeg -hide_banner -loglevel error -i "$SRC" -vf "$chain" -f rawvideo -pix_fmt gray -y "$WORK/out.gray"
    got="$("$PY" -c "
import sys
d = open(sys.argv[1], 'rb').read()
print(' '.join('%d,%d' % (d[i + 528], d[i]) for i in range(0, len(d) - 1023, 1024)))" "$WORK/out.gray")"
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

# The other 3D and playback tests that need no window ride along here, because the CI workflows
# already call this script: automatic 3D detection (tools/test_stereodetect.sh) and the adaptation
# to the machine (tuning_test).
HERE="$(cd "$(dirname "$0")" && pwd)"
if [ -x "$BUILD/stereodetect_test" ] || [ -x "$BUILD/stereodetect_test.exe" ]; then
    "$HERE/test_stereodetect.sh" "$BUILD" "$WORK/../stereodetect"
fi
# picture subtitles read by Lumen itself (for 3D output per eye): a PGS file with three pictures
if [ -x "$BUILD/bitmapsubs_test" ] || [ -x "$BUILD/bitmapsubs_test.exe" ]; then
    B="$BUILD/bitmapsubs_test"; [ -x "$B" ] || B="$BUILD/bitmapsubs_test.exe"
    "$PY" "$(dirname "$0")/make_test_pgs.py" "$WORK/test.sup"
    "$B" "$WORK/test.sup" | grep -vE "^OK" || true
    "$B" "$WORK/test.sup" > /dev/null
fi
# DCP with a KDM outside its period: the keys must still unpack and decrypt the essence (Lumen only
# uses them after the user has confirmed the warning in the Cinema tab)
# (Homebrew's OpenSSL on macOS – the system one there is LibreSSL; elsewhere the one in PATH)
OSSL=""
if command -v brew > /dev/null 2>&1; then OSSL="$(brew --prefix openssl@3 2> /dev/null || true)/bin/openssl"; fi
[ -x "$OSSL" ] || OSSL="$(command -v openssl || true)"
if [ -x "$BUILD/dcp_test" ] && [ -n "$OSSL" ]; then
    K="$WORK/kdm-window"; rm -rf "$K"; mkdir -p "$K"
    "$BUILD/dcp_test" gencert "$K/id" > /dev/null
    "$PY" "$(dirname "$0")/make_test_dcp.py" ffmpeg "$OSSL" "$K/dcp" --encrypt "$K/id/leaf.pem" \
        --valid-from 2025-09-01T00:00:00+00:00 --valid-until 2025-09-30T23:59:59+00:00 > /dev/null
    window="$("$BUILD/dcp_test" kdm "$K/dcp/kdm.xml" "$K/id/leaf.key" | head -1)"
    played="$("$BUILD/dcp_test" play "$K/dcp" "$K/dcp/kdm.xml" "$K/id/leaf.key" 1.5 2>/dev/null | grep "loaded=" || true)"
    echo "expired KDM: $window"
    echo "expired KDM: $played"
    case "$window" in *"valid=2025-09-01T00:00:00Z..2025-09-30T23:59:59Z error="*) ;; *) echo "FAIL: KDM period not read"; exit 1 ;; esac
    case "$played" in *"loaded=1"*"keyErrors=0 missingKeys=0"*) echo "OK   keys of an expired KDM decrypt the DCP" ;; *) echo "FAIL: expired KDM keys do not decrypt"; exit 1 ;; esac
    # KDM package as distributors send it: a ZIP with deflated and stored entries
    "$PY" - "$K/dcp/kdm.xml" "$K/kdms.zip" <<'PYEOF'
import sys, zipfile
kdm = open(sys.argv[1], "rb").read()
with zipfile.ZipFile(sys.argv[2], "w") as z:
    z.writestr("KDM_screen1.xml", kdm, compress_type=zipfile.ZIP_DEFLATED)
    z.writestr("KDM_screen2.xml", kdm, compress_type=zipfile.ZIP_STORED)
    z.writestr("readme.txt", b"not a KDM", compress_type=zipfile.ZIP_DEFLATED)
PYEOF
    size=$(wc -c < "$K/dcp/kdm.xml" | tr -d ' ')
    listed="$("$BUILD/dcp_test" unzip "$K/kdms.zip" .xml)"
    echo "$listed" | sed 's/^/KDM package: /'
    [ "$(echo "$listed" | grep -c " $size ")" = 2 ] && echo "OK   KDM package: both KDMs read in full (deflated and stored)" \
        || { echo "FAIL: KDM package"; exit 1; }
fi
# subtitles of 3D files, once per eye: eye areas and line wrapping
"$BUILD/stereo_test" --subs | grep -vE "^OK" || true
"$BUILD/stereo_test" --subs > /dev/null
if [ -x "$BUILD/tuning_test" ] || [ -x "$BUILD/tuning_test.exe" ]; then
    "$BUILD/tuning_test" | grep -vE "^OK" || true
    "$BUILD/tuning_test" > /dev/null
fi
