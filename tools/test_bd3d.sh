#!/usr/bin/env bash
# Blu-ray 3D end to end without a real disc:
#   tests/data/mvc8.h264   two-view H.264/MVC stream (made with FFmpeg-mvc's
#                          tests/fate/h264-mvc/mvc-mkfix.pl: left eye luma 165, right eye 36)
#   -> synthetic 3D disc (tools/make_test_bd3d.py)
#   -> Lumen merges base and dependent view (mvcmerge_test, libbluray)
#   -> the MVC decoder of the FFmpeg in PATH must deliver both views of all 8 pictures
#
#   tools/test_bd3d.sh <build-dir> <work-dir>
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$1"
WORK="$2"
PY="$(command -v python3 || command -v python)"

rm -rf "$WORK"
mkdir -p "$WORK"
"$PY" "$HERE/tools/make_test_bd3d.py" "$HERE/tests/data/mvc8.h264" "$WORK/disc"
"$BUILD/mvcmerge_test" "$WORK/disc" 0 "$WORK/merged.m2ts"
ffmpeg -hide_banner -version | head -1
# every picture is 32x16 grey: byte 0 = left eye, byte 16 = right eye
ffmpeg -hide_banner -loglevel error -view_ids -1 -i "$WORK/merged.m2ts" -fps_mode passthrough -f rawvideo -pix_fmt gray -y "$WORK/views.gray"
result="$("$PY" -c "
import sys, collections
d = open(sys.argv[1], 'rb').read()
c = collections.Counter('%d,%d' % (d[i], d[i + 16]) for i in range(0, len(d) - 511, 512))
print(''.join(' %d %s' % (n, k) for k, n in sorted(c.items())))" "$WORK/views.gray")"
echo "views (count left,right):$result"
if [ "$result" != " 8 165,36" ]; then
    echo "::error::Blu-ray 3D: expected 8 pictures with left 165 / right 36, got:$result"
    exit 1
fi
echo "Blu-ray 3D: both views decoded"
# the merged stream is H.264/MVC in MPEG-TS: the 3D detection must recognise it as two views
if [ -x "$BUILD/stereodetect_test" ] || [ -x "$BUILD/stereodetect_test.exe" ]; then
    T="$BUILD/stereodetect_test"; [ -x "$T" ] || T="$BUILD/stereodetect_test.exe"
    "$T" "$WORK/merged.m2ts" mvc
fi

# Time line of a playlist: every clip of a Blu-ray counts its own time, Lumen moves PTS, DTS and PCR
# onto the playlist's time line (retimeM2ts). A generated M2TS, moved by 100 s, must carry exactly
# these time stamps afterwards, for picture and sound.
if [ -x "$BUILD/tsretime_test" ] || [ -x "$BUILD/tsretime_test.exe" ]; then
    T="$BUILD/tsretime_test"; [ -x "$T" ] || T="$BUILD/tsretime_test.exe"
    ffmpeg -hide_banner -loglevel error -f lavfi -i testsrc2=size=320x180:rate=24 -f lavfi -i sine=frequency=440 -t 2 \
        -c:v mpeg2video -bf 2 -c:a mp2 -f mpegts -mpegts_m2ts_mode 1 -y "$WORK/time.m2ts"
    "$T" "$WORK/time.m2ts" "$WORK/time-moved.m2ts" 9000000
    stamps() { ffprobe -hide_banner -loglevel error -show_entries packet=stream_index,pts,dts -of csv=p=0 "$1" | tr -d '\r'; }
    stamps "$WORK/time.m2ts" > "$WORK/time.csv"
    stamps "$WORK/time-moved.m2ts" > "$WORK/time-moved.csv"
    result="$("$PY" -c "
import sys
a = [l.split(',') for l in open(sys.argv[1]) if l.strip()]
b = [l.split(',') for l in open(sys.argv[2]) if l.strip()]
ok = len(a) == len(b) and len(a) > 40 and all(x[0] == y[0] and int(y[1]) - int(x[1]) == 9000000 and int(y[2]) - int(x[2]) == 9000000 for x, y in zip(a, b))
print('%d packets, %s' % (len(a), 'all moved by 100 s' if ok else 'time stamps differ'))" "$WORK/time.csv" "$WORK/time-moved.csv")"
    echo "playlist time line: $result"
    case "$result" in *"all moved by 100 s") ;; *) echo "::error::Blu-ray: time stamps not moved onto the playlist's time line"; exit 1 ;; esac
fi

# Stream formats change from playlist to playlist (menu sound AC-3, film sound DTS-HD on the same PID):
# such a stream must get a PID of its own, in the packets and in the programme table (TsRemap).
if [ -x "$BUILD/tsremap_test" ] || [ -x "$BUILD/tsremap_test.exe" ]; then
    T="$BUILD/tsremap_test"; [ -x "$T" ] || T="$BUILD/tsremap_test.exe"
    if ! "$T" > "$WORK/tsremap.log" 2>&1; then
        cat "$WORK/tsremap.log"
        echo "::error::Blu-ray: stream with a changed format did not get its own PID"
        exit 1
    fi
    echo "playlist streams: $(grep -c '^OK' "$WORK/tsremap.log") checks passed"
fi

# BD-J menus: rides along because every CI platform calls this script. Report only, the packages are
# built either way: where no Java is at hand the test skips itself, and a failure shows as a warning.
if [ -z "${LUMEN_SKIP_BDJ:-}" ]; then
    bash "$HERE/tools/test_bdj.sh" "$BUILD" "$WORK/bdj" || echo "::warning::BD-J menu test failed on this platform"
fi

# Rides along because every CI platform (Windows too) calls this script: how fast the software
# decoder runs there. Report only. LUMEN_SKIP_BENCH=1 leaves it out.
if [ -z "${LUMEN_SKIP_BENCH:-}" ]; then
    bash "$HERE/tools/bench_decode.sh" "$WORK/bench" || true
fi
