#!/usr/bin/env python3
"""Kopiert alle benötigten DLLs neben lumen.exe (Windows).

    python tools/deploy_windows.py build/lumen.exe --search 3rdparty/ffmpeg-mvc/bin C:/lumen-build/msys2/ucrt64/bin

Die Suchordner werden in der angegebenen Reihenfolge durchsucht – FFmpeg-mvc
zuerst, damit dessen avcodec/avformat/... die normalen FFmpeg-DLLs ersetzen.
Qt-Plugins und QML-Module erledigt vorher windeployqt.
"""
import argparse
import os
import re
import shutil
import struct
import subprocess
import sys


def lp(path):
    """Windows-Langpfad (> 260 Zeichen) für Dateioperationen."""
    p = os.path.abspath(path)
    if os.name == "nt" and len(p) > 240 and not p.startswith("\\\\?\\"):
        # "\\?\" verlangt Backslashes (MSYS2-Python liefert "C:/…")
        return "\\\\?\\" + p.replace("/", "\\")
    return p


def pe_imports(path):
    """Liest die Namen der importierten DLLs aus einer PE-Datei (x64)."""
    with open(lp(path), "rb") as f:
        data = f.read()
    if data[:2] != b"MZ":
        return []
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        return []
    num_sections = struct.unpack_from("<H", data, pe + 6)[0]
    opt_size = struct.unpack_from("<H", data, pe + 20)[0]
    opt = pe + 24
    magic = struct.unpack_from("<H", data, opt)[0]
    dir_off = opt + (112 if magic == 0x20B else 96)
    imp_rva, imp_size = struct.unpack_from("<II", data, dir_off + 8)
    sections = []
    sec = opt + opt_size
    for i in range(num_sections):
        vsize, va, raw_size, raw_ptr = struct.unpack_from("<IIII", data, sec + i * 40 + 8)
        sections.append((va, max(vsize, raw_size), raw_ptr))

    def rva2off(rva):
        for va, size, raw in sections:
            if va <= rva < va + size:
                return rva - va + raw
        return None

    names = []
    off = rva2off(imp_rva) if imp_rva else None
    while off is not None and off + 20 <= len(data):
        name_rva = struct.unpack_from("<I", data, off + 12)[0]
        if name_rva == 0:
            break
        noff = rva2off(name_rva)
        if noff is None:
            break
        end = data.index(b"\0", noff)
        names.append(data[noff:end].decode("ascii", "replace"))
        off += 20
    return names


def deploy_bdj(target):
    """Legt libblurays Java-Archiv (BD-J-Menüs) in den Ordner "bdj" neben die exe.

    libbluray lädt nur das Archiv der eigenen Version; dessen Name steht in der DLL. Die Archive
    liegen in resources/bdj (tools/build_bdj.sh).
    """
    dlls = [f for f in os.listdir(lp(target)) if re.match(r"libbluray(-\d+)?\.dll$", f.lower())]
    if not dlls:
        return
    with open(lp(os.path.join(target, dlls[0])), "rb") as f:
        m = re.search(rb"libbluray-j2se-(\d+\.\d+\.\d+)\.jar", f.read())
    if not m:
        print("BD-J: diese libbluray lädt kein Java-Archiv")
        return
    version = m.group(1).decode("ascii")
    source = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "resources", "bdj")
    jars = [os.path.join(source, f"libbluray-{part}j2se-{version}.jar") for part in ("", "awt-")]
    if not all(os.path.exists(j) for j in jars):
        print(f"BD-J: kein Archiv für libbluray {version} in resources/bdj (tools/build_bdj.sh) - "
              "BD-J-Menüs fehlen im Paket", file=sys.stderr)
        return
    os.makedirs(lp(os.path.join(target, "bdj")), exist_ok=True)
    for j in jars:
        shutil.copy2(j, lp(os.path.join(target, "bdj", os.path.basename(j))))
    print(f"BD-J: Archiv für libbluray {version} kopiert")


def deploy_jre(exe, target):
    """Java-Laufzeit für BD-J-Menüs in den Ordner "jre" neben die exe (tools/make_jre.py)."""
    with open(lp(exe), "rb") as f:
        head = f.read(4096)
    pe = struct.unpack_from("<I", head, 0x3C)[0]
    machine = struct.unpack_from("<H", head, pe + 4)[0]
    arch = {0x8664: "x86_64", 0xAA64: "aarch64"}.get(machine)
    if not arch:
        return
    script = os.path.join(os.path.dirname(os.path.abspath(__file__)), "make_jre.py")
    subprocess.run([sys.executable, script, os.path.join(target, "jre"), "--arch", arch], check=False)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("exe")
    ap.add_argument("--search", nargs="+", required=True)
    ap.add_argument("--extra", nargs="*", default=[], help="zusätzlich zu prüfende Binärdateien (z. B. Qt-Plugins)")
    ap.add_argument("--own", nargs="*", default=[], help="eigene DLLs neben der exe, nie überschreiben (z. B. libdvdcss-Shim)")
    a = ap.parse_args()

    target = os.path.dirname(os.path.abspath(a.exe))
    index = {}
    for d in a.search:
        for fn in os.listdir(d):
            if fn.lower().endswith(".dll"):
                index.setdefault(fn.lower(), os.path.join(d, fn))

    todo = [a.exe] + a.extra
    # bereits deployte Qt-Plugins/QML-Module ebenfalls prüfen
    for root, _, files in os.walk(lp(target)):
        todo += [os.path.join(root, f) for f in files if f.lower().endswith(".dll")]

    seen, copied = set(), 0
    while todo:
        path = todo.pop()
        for dep in pe_imports(path):
            key = dep.lower()
            if key in seen:
                continue
            seen.add(key)
            src = index.get(key)
            if not src:
                continue  # System-DLL
            dst = os.path.join(target, os.path.basename(src))
            if key in (o.lower() for o in a.own) and os.path.exists(lp(dst)):
                todo.append(dst)
                continue
            if not os.path.exists(lp(dst)) or os.path.getsize(lp(dst)) != os.path.getsize(src) or \
                    os.path.getmtime(lp(dst)) < os.path.getmtime(src):
                shutil.copy2(src, lp(dst))
                copied += 1
            todo.append(dst)
    # Qt-Pfade relativ zur exe (MSYS2-Qt sucht sonst unter share/qt6/…)
    qtconf = os.path.join(target, "qt.conf")
    if not os.path.exists(lp(qtconf)):
        with open(lp(qtconf), "w", encoding="ascii") as f:
            f.write("[Paths]\nPrefix = .\nPlugins = .\nQmlImports = qml\n")
    print(f"{copied} DLLs kopiert, {len(seen)} Abhängigkeiten geprüft")
    deploy_bdj(target)
    deploy_jre(a.exe, target)


if __name__ == "__main__":
    sys.exit(main())
