#!/usr/bin/env bash
# How fast does the software decoder of the FFmpeg in PATH run on this machine?
# Report only – never fails. Rides along with tools/test_bd3d.sh so that every CI platform
# prints its numbers (virtual machines with few cores: a lower bound, not a real player).
# The clips are made here: noisy test pictures at bit rates of the real formats.
#
#   tools/bench_decode.sh <work-dir>
WORK="${1:?work dir}"
mkdir -p "$WORK"
FF="ffmpeg -hide_banner -loglevel error -y"

cores="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo "${NUMBER_OF_PROCESSORS:-?}")"
cpu="$( (sysctl -n machdep.cpu.brand_string || grep -m1 'model name' /proc/cpuinfo | cut -d: -f2 || echo "${PROCESSOR_IDENTIFIER:-}") 2>/dev/null | head -1 | sed 's/^ *//')"
echo "decode-bench: $(uname -sm), ${cpu:-unknown CPU}, $cores cores"
echo "decode-bench: synthetic clips, decoder alone (no display); real time needs 1.0x or more"

# bench <label> <frames per second the format needs> <file> [decoder options]
bench() {
    local label="$1" need="$2" file="$3"; shift 3
    [ -s "$file" ] || { echo "decode-bench: $label: clip could not be made"; return; }
    local frames size dur secs
    frames="$(ffprobe -v error -count_packets -select_streams v:0 -show_entries stream=nb_read_packets -of csv=p=0 "$file" 2>/dev/null | tr -d ' ,\r')"
    dur="$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$file" 2>/dev/null | tr -d ' ,\r')"
    size="$(wc -c < "$file" | tr -d ' ')"
    # wall clock time of the decoder alone (-benchmark prints rtime)
    # … over eight passes, so that starting the decoder threads does not count for much
    frames=$(( ${frames:-0} * 8 ))
    secs="$(ffmpeg -hide_banner -benchmark "$@" -stream_loop 7 -i "$file" -an -f null - 2>&1 | sed -n 's/.*rtime=\([0-9.]*\)s.*/\1/p' | tail -1)"
    awk -v l="$label" -v n="$need" -v f="${frames:-0}" -v s="${secs:-0}" -v z="$size" -v d="${dur:-0}" 'BEGIN {
        if (s <= 0 || f <= 0) { print "decode-bench: " l ": not measured"; exit }
        fps = f / s; mbit = d > 0 ? z * 8 / d / 1e6 : 0; slow = (fps < n) ? "  <- below real time" : ""
        printf "decode-bench: %-28s %4.0f Mbit/s  %6.1f fps  (%.1fx of %d)%s\n", l, mbit, fps, fps / n, n, slow
    }'
}

NOISE="noise=alls=24:allf=t"
X264="-c:v libx264 -preset veryfast -pix_fmt yuv420p -g 24"
$FF -f lavfi -i "testsrc2=size=1920x1080:rate=24" -t 2 -vf "$NOISE" $X264 -b:v 40M -maxrate 40M -bufsize 30M "$WORK/h264-1080.mkv" 2>/dev/null
bench "H.264 1080p24 (Blu-ray)" 24 "$WORK/h264-1080.mkv"
$FF -f lavfi -i "testsrc2=size=3840x2160:rate=24" -t 1 -vf "$NOISE" $X264 -b:v 80M -maxrate 80M -bufsize 60M "$WORK/h264-2160.mkv" 2>/dev/null
bench "H.264 2160p24" 24 "$WORK/h264-2160.mkv"
$FF -f lavfi -i "testsrc2=size=1920x1080:rate=25" -t 2 -vf "$NOISE,format=yuv420p" -c:v mpeg2video -b:v 35M -maxrate 35M -bufsize 9M -flags +ildct+ilme "$WORK/mpeg2-1080i.mkv" 2>/dev/null
bench "MPEG-2 1080i25" 25 "$WORK/mpeg2-1080i.mkv"
# JPEG 2000 as in a DCP: XYZ 12 bit, every picture a key picture, at most 250 Mbit/s.
# The encoder has no rate control: raise the quantiser until the pictures are small enough.
# j2k <file> <size> <pictures per second> <pictures>
j2k() {
    local file="$1" size="$2" rate="$3" count="$4" q
    # soft noise over a test picture: detail like film grain, the same share of the picture in 2K and 4K
    local blur=1.5; [ "${size%x*}" -gt 3000 ] && blur=3
    local src="-f lavfi -i testsrc2=size=${size}:rate=${rate} -vf noise=alls=6:allf=t,gblur=sigma=$blur" enc="-c:v jpeg2000 -pix_fmt xyz12le"
    for q in 6 10 16 24 36 52 76 110 160; do
        $FF $src -frames:v 1 $enc -q:v $q "$file" 2>/dev/null || return
        [ $(( $(wc -c < "$file") * 8 * rate )) -le 250000000 ] && break
    done
    $FF $src -frames:v "$count" $enc -q:v $q "$file" 2>/dev/null
}
j2k "$WORK/j2k-2k.mxf" 2048x1080 24 12
bench "JPEG 2000 2K 24 (DCP)" 24 "$WORK/j2k-2k.mxf"
# what Lumen falls back to when the machine is too slow: first the finest bit planes of every
# code block are left out (full resolution, below what an 8 or 10 bit output shows) ...
bench "JPEG 2000 2K 24, -2 planes" 24 "$WORK/j2k-2k.mxf" -skip_planes 2
bench "JPEG 2000 2K 24, -4 planes" 24 "$WORK/j2k-2k.mxf" -skip_planes 4
j2k "$WORK/j2k-2k48.mxf" 2048x1080 48 12
bench "JPEG 2000 2K 48 (3D DCP)" 48 "$WORK/j2k-2k48.mxf"
j2k "$WORK/j2k-4k.mxf" 4096x2160 24 6
bench "JPEG 2000 4K 24 (DCP)" 24 "$WORK/j2k-4k.mxf"
bench "JPEG 2000 4K 24, -4 planes" 24 "$WORK/j2k-4k.mxf" -skip_planes 4
# ... then half the resolution inside the decoder
bench "JPEG 2000 4K 24, half res." 24 "$WORK/j2k-4k.mxf" -lowres 1
bench "JPEG 2000 4K 24, half, -4" 24 "$WORK/j2k-4k.mxf" -lowres 1 -skip_planes 4
exit 0
