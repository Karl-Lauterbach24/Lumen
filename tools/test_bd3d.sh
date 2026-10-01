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
