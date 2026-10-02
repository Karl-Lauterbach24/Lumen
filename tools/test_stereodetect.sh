#!/bin/bash
# Test der automatischen 3D-Erkennung: Kennungen/Größen (ohne Dateien), dann kurze erzeugte
# Clips – zwei gegeneinander versetzte Ansichten nebeneinander bzw. übereinander, und 2D-Bilder,
# die nicht als 3D gelten dürfen (Farbbalken, Testbild, schwarze Balken, Fraktal).
#   tools/test_stereodetect.sh <build-verzeichnis> <arbeitsverzeichnis>
set -e
BUILD=$1; OUT=$2
mkdir -p "$OUT"
T="$BUILD/stereodetect_test"
[ -x "$T" ] || T="$BUILD/stereodetect_test.exe"
"$T" | grep -vE "^OK" || true
"$T" > /dev/null

FF="ffmpeg -hide_banner -loglevel error -y"
ENC="-c:v libx264 -preset ultrafast -pix_fmt yuv420p -g 12"
# Zwei Ansichten: derselbe Ausschnitt, die rechte um 10 Punkte versetzt
VIEWS="split[a][b];[a]crop=640:360:0:0[l];[b]crop=640:360:10:0[r]"
SRC="-f lavfi -i mandelbrot=size=660x360:rate=24"
$FF $SRC -t 8 -vf "$VIEWS;[l][r]hstack,scale=1280:720,setsar=1" $ENC "$OUT/full-sbs.mkv"          # doppelt breit
$FF $SRC -t 8 -vf "$VIEWS;[l][r]hstack,scale=640:360,setsar=1" $ENC "$OUT/half-sbs.mkv"
$FF $SRC -t 8 -vf "$VIEWS;[l][r]vstack,scale=640:360,setsar=1" $ENC "$OUT/half-tab.mkv"
$FF $SRC -t 8 -vf "$VIEWS;[l][r]vstack" $ENC "$OUT/full-tab.mkv"                          # doppelt hoch
$FF $SRC -t 8 -vf "$VIEWS;[l][r]hstack,scale=640:272,setsar=1,pad=640:360:0:44" $ENC "$OUT/half-sbs-balken.mkv"
$FF $SRC -t 8 -vf "crop=640:360:0:0" $ENC "$OUT/2d-fraktal.mkv"
$FF -f lavfi -i testsrc2=size=640x360:rate=24 -t 8 $ENC "$OUT/2d-testbild.mkv"
$FF -f lavfi -i smptebars=size=640x360:rate=24 -t 8 $ENC "$OUT/2d-farbbalken.mkv"
$FF -f lavfi -i testsrc=size=640x272:rate=24 -t 8 -vf "pad=640:360:0:44" $ENC "$OUT/2d-balken.mkv"
# der Name sagt 3D, das Bild ist es nicht: die Kennung zählt nur mit "3D" oder Größenangabe
$FF -f lavfi -i testsrc2=size=640x360:rate=24 -t 8 $ENC "$OUT/Sendung.S01E01.SBS.WEB.mkv"
$FF $SRC -t 8 -vf "$VIEWS;[l][r]hstack,scale=640:360,setsar=1" $ENC "$OUT/Film.3D.SBS.RL.mkv"
# gestaucht gespeichert, aber mit dem Seitenverhältnis der Bildpunkte (2:1) gekennzeichnet: volle Breite je Auge
$FF $SRC -t 8 -vf "$VIEWS;[l][r]hstack,scale=640:360,setsar=2" $ENC "$OUT/sbs-anamorph.mkv"

fail=0
run() { "$T" "$OUT/$1" "$2" | grep -E "^(OK|FAIL)" || fail=1; "$T" "$OUT/$1" "$2" >/dev/null || fail=1; }
run full-sbs.mkv sbsl
run half-sbs.mkv sbs2l
run half-tab.mkv ab2l
run full-tab.mkv abl
run half-sbs-balken.mkv sbs2l
run 2d-fraktal.mkv none
run 2d-testbild.mkv none
run 2d-farbbalken.mkv none
run 2d-balken.mkv none
run Sendung.S01E01.SBS.WEB.mkv none
run Film.3D.SBS.RL.mkv sbs2r
run sbs-anamorph.mkv sbsl
[ $fail -eq 0 ] && echo "BESTANDEN" || { echo "NICHT BESTANDEN"; exit 1; }
