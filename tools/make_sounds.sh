#!/bin/bash
# Renders LumenOS's interface sounds and its ambient loop into resources/sounds (FLAC, 48 kHz stereo).
# They come out of tools/soundgen – a small synthesizer, so the files are Lumen's own. Needs a C++
# compiler and ffmpeg. Run it after changing tools/soundgen and commit what it writes.
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
c++ -O2 -std=c++17 "$here"/tools/soundgen/UiSynth.cpp "$here"/tools/soundgen/main.cpp -o "$work/soundgen"
"$work/soundgen" "$work"
mkdir -p "$here/resources/sounds"
for wav in "$work"/*.wav; do
    name="$(basename "$wav" .wav)"
    ffmpeg -v error -y -i "$wav" -c:a flac -compression_level 8 -bitexact -map_metadata -1 "$here/resources/sounds/$name.flac"
done
ls -la "$here/resources/sounds"
