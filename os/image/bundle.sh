#!/bin/bash
# Packs LumenOS-update-<version>.zip: Lumen's Debian packages of one version and their checksums.
# On a USB stick it updates a LumenOS device that has no network (Settings › Update).
#
#   os/image/bundle.sh <output folder> Lumen-<version>-linux-amd64.deb Lumen-<version>-linux-arm64.deb
set -euo pipefail
out="$(cd "${1:?output folder}" && pwd)"
shift
[ $# -gt 0 ] || { echo "name the packages"; exit 1; }
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
version=""
for deb in "$@"; do
    name="$(basename "$deb")"
    case "$name" in Lumen-*-linux-*.deb) ;; *) echo "$name: not a package of Lumen for Debian"; exit 1 ;; esac
    v="${name#Lumen-}"; v="${v%%-linux-*}"
    [ -z "$version" ] || [ "$v" = "$version" ] || { echo "the packages are of different versions"; exit 1; }
    version="$v"
    cp "$deb" "$work/$name"
done
( cd "$work" && sha256sum Lumen-*.deb > SHA256SUMS.txt )
zip="$out/LumenOS-update-$version.zip"
rm -f "$zip"
# (the packages are compressed already: stored, not packed again)
( cd "$work" && python3 -c '
import sys, zipfile
with zipfile.ZipFile(sys.argv[1], "w", zipfile.ZIP_STORED) as z:
    for name in sys.argv[2:]:
        z.write(name)
' "$zip" SHA256SUMS.txt Lumen-*.deb )
( cd "$out" && sha256sum "$(basename "$zip")" > "$(basename "$zip").sha256" )
ls -la "$zip"
