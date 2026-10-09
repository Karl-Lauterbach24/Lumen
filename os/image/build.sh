#!/bin/bash
# Builds the LumenOS image: a Debian system that starts straight into Lumen as a player.
#
#   sudo os/image/build.sh <amd64|arm64> <Lumen-…-linux-<arch>.deb> [output folder]
#
# Runs on Debian 13 with the package "live-build" (the build of the other architecture needs
# qemu-user-static, or any other way the machine runs that architecture's programs). The result
# is LumenOS-<version>-<arch>.iso: starts from a USB stick or a disc on UEFI machines (amd64: on
# BIOS machines too), runs from there, and installs itself to the internal disk from its settings.
#
# What is in the image: Debian's base system and kernel with firmware, a compositor for one
# program (cage), sound (PipeWire), network (NetworkManager), Bluetooth (BlueZ), and Lumen from the
# package given – which brings LumenOS's own part (share/lumen/os) with it.
#
# For encrypted discs the image carries two things that are not installed until the user says yes
# in the setup (see os/README.md): libdvdcss as a package built while the image is made, and the
# Blu-ray key database of its community as it is on that day. Leave them out with
#   LUMENOS_NO_DVDCSS=1   LUMENOS_NO_KEYDB=1
# (the device then builds or loads them itself when asked, which needs a network).
set -euo pipefail
arch="${1:?architecture: amd64 or arm64}"
deb="$(readlink -f "${2:?the Lumen package for that architecture}")"
out="$(readlink -f "${3:-.}")"
here="$(cd "$(dirname "$0")" && pwd)"
[ "$(id -u)" = 0 ] || { echo "run as root (live-build needs it)"; exit 1; }
case "$arch" in amd64 | arm64) ;; *) echo "amd64 or arm64"; exit 1 ;; esac
[ -f "$deb" ] || { echo "no such package: $deb"; exit 1; }
[ "$(dpkg-deb -f "$deb" Architecture)" = "$arch" ] || { echo "$deb is not built for $arch"; exit 1; }
version="$(dpkg-deb -f "$deb" Version)"
work="${LUMENOS_WORK:-/var/tmp/lumenos-build-$arch}"
mirror="${LUMENOS_MIRROR:-http://deb.debian.org/debian/}"

# Start clean, but keep the packages a build before this one has already loaded. A build that
# broke off may have left the system folders of its image mounted: never delete through those.
if [ -d "$work" ]; then
    ( cd "$work" && lb clean > /dev/null 2>&1 ) || true
    if grep -q " $work/" /proc/mounts; then
        awk -v w="$work/" 'index($2, w) == 1 { print $2 }' /proc/mounts | sort -r | while read -r m; do umount -l "$m" 2> /dev/null || true; done
    fi
    if grep -q " $work/" /proc/mounts; then
        echo "something is still mounted below $work - not touching it"
        exit 1
    fi
    find "$work" -mindepth 1 -maxdepth 1 ! -name cache -exec rm -rf {} +
fi
mkdir -p "$work" "$out"
cd "$work"

extra=()
if [ "$(dpkg --print-architecture)" != "$arch" ]; then
    # a foreign architecture: the second stage of the bootstrap runs that architecture's programs
    qemu=""
    case "$arch" in amd64) qemu=/usr/bin/qemu-x86_64-static ;; arm64) qemu=/usr/bin/qemu-aarch64-static ;; esac
    [ -x "$qemu" ] && extra+=(--bootstrap-qemu-arch "$arch" --bootstrap-qemu-static "$qemu")
fi
case "$arch" in
amd64) loaders="grub-pc,grub-efi" ;;
arm64) loaders="grub-efi" ;;
esac

lb config \
    --distribution trixie \
    --architectures "$arch" \
    --archive-areas "main contrib non-free-firmware" \
    --mirror-bootstrap "$mirror" --mirror-chroot "$mirror" --mirror-binary "$mirror" \
    --binary-images iso-hybrid \
    --bootloaders "$loaders" \
    --apt-recommends false \
    --apt-indices false \
    --debootstrap-options "--variant=minbase" \
    --firmware-chroot false --firmware-binary false \
    --memtest none \
    --win32-loader false \
    --chroot-squashfs-compression-type zstd \
    --iso-application "LumenOS" \
    --iso-publisher "Lumen; https://github.com/Karl-Lauterbach24/Lumen" \
    --iso-volume "LumenOS $version" \
    --image-name "LumenOS" \
    --bootappend-live "boot=live components quiet loglevel=3 vt.global_cursor_default=0 hostname=lumenos live-config.nocomponents=user-setup,sudo,xinit,gdm3,lightdm,sddm,login,x-session-manager" \
    "${extra[@]}" > "$work/config.log" 2>&1 || { tail -20 "$work/config.log"; exit 1; }

# --- packages (one per line; lines ending in ":arch" only for that architecture)
mkdir -p config/package-lists config/packages.chroot config/hooks/normal config/includes.chroot/etc/lumenos config/includes.binary
sed -e 's/[[:space:]]*#.*//' -e '/^[[:space:]]*$/d' "$here/packages.list" | while read -r name only; do
    [ -z "$only" ] || [ "$only" = "$arch" ] || continue
    echo "$name"
done > config/package-lists/lumenos.list.chroot
# Lumen itself: the package given is offered to the image as a local source, and asked for by name
# (live-build takes local packages only under Debian's file name: name_version_architecture.deb)
package="$(dpkg-deb -f "$deb" Package)"
cp "$deb" "config/packages.chroot/${package}_${version}_${arch}.deb"
echo "$package" >> config/package-lists/lumenos.list.chroot

# --- the system becomes the player (user, services, rules: all from Lumen's package)
cp "$here/setup.hook" config/hooks/normal/9000-lumenos.hook.chroot
chmod +x config/hooks/normal/9000-lumenos.hook.chroot
printf 'LUMENOS_IMAGE=%s\nLUMENOS_ARCH=%s\nLUMENOS_BUILT=%s\n' "$version" "$arch" "$(date -u +%Y-%m-%d)" > config/includes.chroot/etc/lumenos/image

# --- carried along, not installed: what the setup offers for encrypted discs
optional=config/includes.chroot/usr/share/lumenos-optional
mkdir -p "$optional"
[ -n "${LUMENOS_NO_DVDCSS:-}" ] || touch "$optional/.dvdcss"   # setup.hook builds it and removes the mark
if [ -z "${LUMENOS_NO_KEYDB:-}" ]; then
    keydb="${LUMENOS_KEYDB_URL:-http://fvonline-db.bplaced.net/export/keydb_eng.zip}"
    # (a zip names its files in plain text: enough to tell the database from an error page, and the
    # machine that builds needs no unzip for it)
    if curl -fsSL --retry 2 --max-time 600 -o "$optional/keydb.zip" "$keydb" && [ "$(head -c 2 "$optional/keydb.zip")" = PK ] && grep -aqi 'keydb\.cfg' "$optional/keydb.zip"; then
        echo "key database of $(date -u +%Y-%m-%d): $(du -h "$optional/keydb.zip" | cut -f1)"
    else
        rm -f "$optional/keydb.zip"
        echo "the key database could not be loaded from $keydb - the image goes without (the device loads it when asked)"
    fi
fi

# --- the boot menu
# (live-build keeps the menu of both loaders, BIOS and UEFI, in "grub-pc")
mkdir -p config/bootloaders
cp -r /usr/share/live/build/bootloaders/grub-pc config/bootloaders/
cp "$here/grub.cfg" config/bootloaders/grub-pc/grub.cfg
sed -i "s/@VERSION@/$version/g" config/bootloaders/grub-pc/grub.cfg

echo "LumenOS $version ($arch): building, log in $work/build.log"
# On an Apple machine's Linux, x86 programs are run by Rosetta – and Rosetta cannot start one where
# /proc is not mounted, which live-build does at the beginning and at the end of its stages. For the
# time of the build they are run by QEMU's emulator instead (qemu-user-static): slower, but everywhere.
rosetta=/proc/sys/fs/binfmt_misc/rosetta
if [ "$arch" = amd64 ] && [ "$(dpkg --print-architecture)" != amd64 ] && [ "$(head -1 "$rosetta" 2> /dev/null)" = enabled ]; then
    [ -e /proc/sys/fs/binfmt_misc/qemu-x86_64 ] || { echo "install qemu-user-static: Rosetta alone cannot build this image"; exit 1; }
    echo 0 > "$rosetta"
    trap 'echo 1 > "$rosetta"' EXIT
fi
if ! lb build > "$work/build.log" 2>&1; then
    tail -40 "$work/build.log"
    exit 1
fi
iso="$(ls "$work"/LumenOS-*.iso "$work"/*.hybrid.iso 2> /dev/null | head -1)"
[ -f "$iso" ] || { echo "no image came out"; tail -30 "$work/build.log"; exit 1; }
echo "carried along: $(ls "$work/chroot/usr/share/lumenos-optional" 2> /dev/null | tr '\n' ' ')"
grep -a 'LumenOS: ' "$work/build.log" | tail -3 || true
target="$out/LumenOS-$version-$arch.iso"
mv "$iso" "$target"
( cd "$out" && sha256sum "$(basename "$target")" > "$(basename "$target").sha256" )
ls -la "$target"
