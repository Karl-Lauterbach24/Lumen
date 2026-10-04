#!/bin/sh
# Builds libbluray's Java archive (BD-J menus) without ant, with javac and jar alone:
#
#     tools/build_bdj.sh <libbluray source folder or libbluray-<version>.tar.xz> [output folder]
#
# The result is libbluray-j2se-<version>.jar and libbluray-awt-j2se-<version>.jar, by default in
# resources/bdj. libbluray only loads the archive of its own version, so the packages take the
# pair that matches the libbluray they contain (CMakeLists.txt, tools/deploy_windows.py).
# Source: https://download.videolan.org/pub/videolan/libbluray/<version>/libbluray-<version>.tar.xz
#
# Any JDK from 8 on works; the classes are built for Java 7 (Java 8 where the JDK no longer can),
# like libbluray's own build does.
set -eu

src=${1:?usage: tools/build_bdj.sh <libbluray source folder or tarball> [output folder]}
out=${2:-"$(cd "$(dirname "$0")/.." && pwd)/resources/bdj"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

if [ -f "$src" ]; then
    tar -xf "$src" -C "$work"
    src=$(echo "$work"/libbluray-*)
fi
[ -f "$src/src/libbluray/bdj/build.xml" ] || { echo "not a libbluray source tree: $src" >&2; exit 1; }
version=$(sed -n "s/^project('libbluray'.*version: *'\([0-9.]*\)'.*/\1/p; s/^ *version: *'\([0-9.]*\)'.*/\1/p" "$src/meson.build" | head -1)
if [ -z "$version" ] && [ -f "$src/configure.ac" ]; then
    version=$(sed -n 's/^m4_define(\[bluray_\(major\|minor\|micro\)\], *\([0-9]*\)).*/\2/p' "$src/configure.ac" | paste -sd. -)
fi
[ -n "$version" ] || { echo "cannot read the libbluray version from $src" >&2; exit 1; }

# the same steps as libbluray's meson.build picks for the JDK at hand
major=$(javac -version 2>&1 | sed -n 's/^javac \([0-9]*\)\.\{0,1\}\([0-9]*\).*/\1 \2/p' | awk '{print ($1 == 1) ? $2 : $1}')
lint="-Xlint:-deprecation"
if [ "$major" -ge 18 ]; then
    level=1.8
    lint="-Xlint:-removal"
elif [ "$major" -ge 12 ]; then
    level=1.7
elif [ "$major" -ge 9 ]; then
    level=1.6
else
    level=1.5
fi

classes="$work/classes"
mkdir -p "$classes" "$out"
cd "$src/src/libbluray/bdj"
find ../../../contrib/asm/src -name '*.java' | sort > "$work/asm.txt"
find java java-j2se java-build-support -name '*.java' | sort > "$work/bdj.txt"

javac -d "$classes" -g -source $level -target $level -XDignore.symbol.file -Xlint:-options -nowarn \
    @"$work/asm.txt"
javac -d "$classes" -g -source $level -target $level -XDignore.symbol.file -Xlint:-options -nowarn $lint \
    -cp "$classes" -sourcepath java:java-j2se:java-build-support @"$work/bdj.txt"

# two archives, split as in libbluray's build.xml: the AWT part goes onto the boot class path
cd "$classes"
# classes that are only needed while compiling
rm -f sun/awt/CausedFocusEvent*.class java/awt/event/FocusEvent*.class
find . -type f ! -path './java/awt/*' ! -path './sun/*' | sort > "$work/main.txt"
find . -type f \( -path './java/awt/*' -o -path './sun/*' \) | sort > "$work/awt.txt"
jar cf "$out/libbluray-j2se-$version.jar" @"$work/main.txt"
jar cf "$out/libbluray-awt-j2se-$version.jar" @"$work/awt.txt"
ls -l "$out/libbluray-j2se-$version.jar" "$out/libbluray-awt-j2se-$version.jar"
