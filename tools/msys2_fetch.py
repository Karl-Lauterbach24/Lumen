#!/usr/bin/env python3
"""Lädt MSYS2-Pakete samt Abhängigkeiten und entpackt sie in einen Zielordner.

    python tools/msys2_fetch.py --repo mingw64 --dest 3rdparty/msys2 mpv ffmpeg
    python tools/msys2_fetch.py --repo msys    --dest 3rdparty/msys2-tools make

Benötigt 7-Zip (zstd-fähig) im PATH oder unter "C:/Program Files/7-Zip/7z.exe".
Wird nur für den Windows-Build von FFmpeg-mvc/libmpv genutzt.
"""
import argparse
import io
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request

REPOS = {
    "mingw64": ("https://repo.msys2.org/mingw/mingw64/", "mingw64.db", "mingw-w64-x86_64-"),
    "ucrt64": ("https://repo.msys2.org/mingw/ucrt64/", "ucrt64.db", "mingw-w64-ucrt-x86_64-"),
    "msys": ("https://repo.msys2.org/msys/x86_64/", "msys.db", ""),
}
SEVEN_ZIP = shutil.which("7z") or r"C:\Program Files\7-Zip\7z.exe"


def fetch(url):
    with urllib.request.urlopen(url) as r:
        return r.read()


def unzstd(data, suffix):
    """Entpackt .zst mit 7-Zip und gibt die rohe Tar-Datei zurück."""
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, "in" + suffix)
        with open(src, "wb") as f:
            f.write(data)
        subprocess.run([SEVEN_ZIP, "x", "-y", "-o" + tmp, src], check=True, stdout=subprocess.DEVNULL)
        tars = [p for p in os.listdir(tmp) if p != os.path.basename(src)]
        with open(os.path.join(tmp, tars[0]), "rb") as f:
            return f.read()


def load_db(repo):
    base, dbname, _ = REPOS[repo]
    raw = fetch(base + dbname)
    try:
        tf = tarfile.open(fileobj=io.BytesIO(raw))
    except tarfile.ReadError:
        tf = tarfile.open(fileobj=io.BytesIO(unzstd(raw, ".tar.zst")))
    pkgs, provides = {}, {}
    for m in tf.getmembers():
        if not m.name.endswith("/desc"):
            continue
        text = tf.extractfile(m).read().decode()
        fields, key = {}, None
        for line in text.splitlines():
            if line.startswith("%") and line.endswith("%"):
                key = line.strip("%")
                fields[key] = []
            elif line and key:
                fields[key].append(line)
        name = fields["NAME"][0]
        pkgs[name] = fields
        for p in fields.get("PROVIDES", []):
            provides.setdefault(re.split(r"[<>=]", p)[0], name)
    return pkgs, provides


def resolve(pkgs, provides, roots, prefix, skip):
    order, seen = [], set()

    def visit(name):
        name = re.split(r"[<>=]", name)[0]
        if name not in pkgs:
            full = prefix + name
            name = full if full in pkgs else provides.get(name, provides.get(full, name))
        if name in seen or name in skip:
            return
        if name not in pkgs:
            print("  ! nicht gefunden:", name, file=sys.stderr)
            return
        seen.add(name)
        for dep in pkgs[name].get("DEPENDS", []):
            visit(dep)
        order.append(name)

    for r in roots:
        visit(r)
    return order


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default="mingw64", choices=REPOS)
    ap.add_argument("--dest", required=True)
    ap.add_argument("--skip", nargs="*", default=[], help="Pakete (voller Name) nicht laden")
    ap.add_argument("--list", action="store_true", help="nur auflisten")
    ap.add_argument("packages", nargs="+")
    a = ap.parse_args()

    base, _, prefix = REPOS[a.repo]
    pkgs, provides = load_db(a.repo)
    order = resolve(pkgs, provides, a.packages, prefix, set(a.skip))
    total = sum(int(pkgs[n].get("CSIZE", ["0"])[0]) for n in order)
    print(f"{len(order)} Pakete, {total / 1e6:.1f} MB")
    if a.list:
        for n in order:
            print(" ", pkgs[n]["FILENAME"][0], pkgs[n].get("CSIZE", ["?"])[0])
        return
    os.makedirs(a.dest, exist_ok=True)
    # 7-Zip entpackt auch in sehr tiefe Ordner (> 260 Zeichen); Doku/Übersetzungen weglassen
    excludes = ["-x!.BUILDINFO", "-x!.MTREE", "-x!.PKGINFO", "-x!.INSTALL"]
    excludes += ["-xr!" + os.path.join(prefix_dir, "share", d)
                 for prefix_dir in ("ucrt64", "mingw64", "usr")
                 for d in ("doc", "man", "info", "locale", "gtk-doc", "licenses", "help")]
    for n in order:
        fn = pkgs[n]["FILENAME"][0]
        print("  ", fn, flush=True)
        tar = unzstd(fetch(base + fn), ".pkg.tar.zst")
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "pkg.tar")
            with open(path, "wb") as f:
                f.write(tar)
            # -aos: vorhandene Dateien behalten (gleiche Pakete; evtl. von laufendem Build gesperrt)
            subprocess.run([SEVEN_ZIP, "x", "-y", "-aos", "-o" + os.path.abspath(a.dest), path] + excludes,
                           check=True, stdout=subprocess.DEVNULL)


if __name__ == "__main__":
    main()
