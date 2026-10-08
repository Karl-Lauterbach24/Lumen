#!/usr/bin/env python3
"""Builds the small Java runtime that Lumen's macOS and Windows packages carry for BD-J disc menus.

    python tools/make_jre.py <output folder> [--arch arm64|x86_64] [--jdk <JDK home>]

libbluray runs a disc's Java menu in a Java VM. So that nobody has to install Java first, the packages
bring a runtime cut down with the JDK's own jlink to the modules BD-J needs (about 45 MB instead of
300). It is taken from a JDK on the build machine: --jdk, LUMEN_JDK, the JAVA_HOME_<version>_<arch>
variables of the GitHub runners, JAVA_HOME, /usr/libexec/java_home (macOS), jlink in PATH. Java 17 is
preferred (the version the disc tests ran with), then 21, then the oldest newer one.

Never fails the build: without a suitable JDK it says so and exits 0 – the package is then built
without a runtime and BD-J menus use a Java the user installed (see src/BdjSetup.cpp).
"""
import argparse
import os
import platform
import re
import shutil
import subprocess
import sys

# what libbluray patches (java.base, java.desktop) and reads (java.rmi, java.xml), plus what disc
# programs commonly touch
MODULES = ["java.base", "java.desktop", "java.xml", "java.rmi", "java.logging", "java.naming", "java.prefs",
           "jdk.unsupported", "jdk.crypto.ec"]


def norm_arch(a):
    a = (a or "").lower()
    if a in ("arm64", "aarch64"):
        return "aarch64"
    if a in ("x86_64", "amd64", "x64"):
        return "x86_64"
    return a


def release_of(home):
    """JAVA_VERSION and OS_ARCH from the JDK's release file."""
    info = {}
    try:
        with open(os.path.join(home, "release"), encoding="utf-8", errors="replace") as f:
            for line in f:
                m = re.match(r'(\w+)="?([^"\n]*)"?', line)
                if m:
                    info[m.group(1)] = m.group(2)
    except OSError:
        pass
    return info


def jlink_of(home):
    exe = os.path.join(home, "bin", "jlink.exe" if os.name == "nt" else "jlink")
    return exe if os.path.isfile(exe) else None


def candidates(explicit):
    seen = []

    def add(home):
        if home and os.path.isdir(home):
            home = os.path.realpath(home)
            if home not in seen:
                seen.append(home)

    add(explicit)
    add(os.environ.get("LUMEN_JDK"))
    for key, value in sorted(os.environ.items()):
        if re.match(r"JAVA_HOME_\d+_", key):
            add(value)
    add(os.environ.get("JAVA_HOME"))
    if sys.platform == "darwin":
        for version in ("17", "21", ""):
            try:
                args = ["/usr/libexec/java_home"] + (["-v", version] if version else [])
                add(subprocess.run(args, capture_output=True, text=True, timeout=10).stdout.strip())
            except (OSError, subprocess.SubprocessError):
                pass
    jlink = shutil.which("jlink")
    if jlink:
        add(os.path.dirname(os.path.dirname(os.path.realpath(jlink))))
    return seen


def pick(arch, explicit):
    found = []
    for home in candidates(explicit):
        rel = release_of(home)
        if not jlink_of(home) or not os.path.isdir(os.path.join(home, "jmods")):
            continue
        if arch and norm_arch(rel.get("OS_ARCH")) != arch:
            continue
        try:
            major = int(rel.get("JAVA_VERSION", "0").split(".")[0])
        except ValueError:
            continue
        if major < 11:
            continue
        # 17 first, then 21, then the oldest newer one
        rank = {17: 0, 21: 1}.get(major, 2 + major)
        found.append((rank, home, major, rel.get("JAVA_VERSION", "")))
    return min(found) if found else None


def prune_linux(lib):
    """Linux: the runtime shall not depend on X11 or ALSA libraries of the system.

    libbluray replaces Java's window toolkit by its own (pictures go to the player as an overlay), but
    runs the VM as "not headless", and Java then loads libawt_xawt.so, which links to libX11, libXtst …
    Nothing of it is called. The headless variant offers the same entry points without those
    libraries: it takes the other one's place. Splash screen, the JAWT bridge and Java Sound are not
    used by disc menus at all.
    """
    headless, xawt = os.path.join(lib, "libawt_headless.so"), os.path.join(lib, "libawt_xawt.so")
    if os.path.isfile(headless):
        shutil.copyfile(headless, xawt)
    for name in ("libsplashscreen.so", "libjawt.so", "libjsound.so"):
        try:
            os.remove(os.path.join(lib, name))
        except OSError:
            pass


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--arch", default=platform.machine())
    ap.add_argument("--jdk")
    a = ap.parse_args()
    arch = norm_arch(a.arch)

    choice = pick(arch, a.jdk)
    if not choice:
        print(f"make_jre: no JDK with jlink for {arch} on this machine - no Java runtime in the package "
              "(BD-J menus then need a Java the user installs)")
        return 0
    _, home, major, version = choice
    stamp = os.path.join(a.out, "lumen-jre.txt")
    wanted = f"{version} {arch} {' '.join(MODULES)}\n"
    try:
        if open(stamp, encoding="utf-8").read() == wanted:
            print(f"make_jre: {a.out} is up to date (Java {version})")
            return 0
    except OSError:
        pass

    present = {os.path.splitext(n)[0] for n in os.listdir(os.path.join(home, "jmods"))}
    modules = [m for m in MODULES if m in present]
    shutil.rmtree(a.out, ignore_errors=True)
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    cmd = [jlink_of(home), "--add-modules", ",".join(modules), "--strip-debug", "--no-header-files", "--no-man-pages",
           "--compress=zip-6" if major >= 21 else "--compress=2", "--output", a.out]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0 or not os.path.isdir(a.out):
        print(f"make_jre: jlink of {home} failed - no Java runtime in the package\n{res.stdout}{res.stderr}")
        shutil.rmtree(a.out, ignore_errors=True)
        return 0
    # the launchers are not needed: libbluray loads the VM as a library
    bindir = os.path.join(a.out, "bin")
    for name in os.listdir(bindir):
        if os.path.splitext(name)[0] in ("java", "javaw", "keytool", "rmiregistry", "jrunscript"):
            os.remove(os.path.join(bindir, name))
    if sys.platform.startswith("linux"):
        prune_linux(os.path.join(a.out, "lib"))
    with open(stamp, "w", encoding="utf-8") as f:
        f.write(wanted)
    size = sum(os.path.getsize(os.path.join(d, n)) for d, _, names in os.walk(a.out) for n in names if not os.path.islink(os.path.join(d, n)))
    print(f"make_jre: Java {version} ({arch}) from {home} -> {a.out}, {size / 1048576:.0f} MB")
    return 0


if __name__ == "__main__":
    sys.exit(main())
