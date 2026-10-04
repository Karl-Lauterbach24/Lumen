#!/usr/bin/env python3
"""Erzeugt eine synthetische Blu-ray mit BD-J-Menü zum Testen (unverschlüsselt, unsigniert).

    python tools/make_test_bdj.py out_dir --jar resources/bdj/libbluray-j2se-1.5.0.jar [--seconds 20]

Braucht ffmpeg mit libx264 sowie javac und jar (JDK ab 8) im PATH.
  BDMV/index.bdmv            First Play und Top Menu: BD-J-Objekt 00000, Titel 1: Movie Object 0
  BDMV/BDJO/00000.bdjo       ein Xlet, startet von selbst; Playlist 00000 läuft dahinter an
  BDMV/JAR/00000.jar         tests/data/bdj/MenuXlet.java: Feld mit zwei Schaltflächen,
                             Pfeiltasten wechseln die Auswahl, Enter blendet das Menü aus
  BDMV/PLAYLIST, CLIPINF, STREAM   ein Clip (Testbild, 1080p24)
Die Dateiformate folgen den libbluray-Parsern (index_parse.c, bdjo_parse.c), der Rest kommt aus
make_test_bd3d.py.
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_test_bd3d import PACKET, PID_BASE, M2tsWriter, access_units, clpi, movie_object, mpls, split_nals  # noqa: E402

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ORG_ID, APP_ID = 0x7FFF0001, 0x4001
XLET_CLASS = "lumentest.MenuXlet"


def index_bdmv():
    appinfo = struct.pack(">I", 34) + bytes([0x00, (6 << 4) | 2]) + b"\x00" * 32

    def bdj(name):
        # Objekttyp BD-J (2 Bit = 2), Reserve; playback_type 3 (interaktiv), Name des BDJO, Reserve
        return struct.pack(">I", 2 << 30) + struct.pack(">H", 3 << 14) + name.encode("ascii") + b"\x00"

    def hdmv(ref):
        return struct.pack(">I", 1 << 30) + struct.pack(">HHI", 0, ref, 0)

    idx_body = bdj("00000") + bdj("00000") + struct.pack(">H", 1) + hdmv(0)
    indexes = struct.pack(">I", len(idx_body)) + idx_body
    head = b"INDX0200" + struct.pack(">II", 40 + len(appinfo), 0)
    head += b"\x00" * (40 - len(head))
    return head + appinfo + indexes


def bdjo():
    def app_string(text):
        b = text.encode("ascii")
        return bytes([len(b)]) + b + (b"" if len(b) & 1 else b"\x00")  # auf gerade Länge auffüllen

    # TerminalInfo: keine Standardschrift, HAVi-Konfiguration 1 (1920x1080), keine Tastensperren
    terminal = b"*****" + bytes([1 << 4]) + b"\x00" * 4
    cache = bytes([1, 0]) + bytes([1]) + b"00000" + b"\x00" * 3 + b"\x00" * 3  # ein Eintrag: JAR 00000
    # eine Playlist, Zugriff auf alle, die erste startet von selbst (das Bild hinter dem Menü)
    playlists = struct.pack(">I", (1 << 21) | (1 << 20) | (1 << 19)) + b"00000" + b"\x00"

    name = b"eng" + bytes([4]) + b"Menu"
    app = bytes([1, 1 << 4]) + struct.pack(">IH", ORG_ID, APP_ID) + b"\x00" * 10  # autostart, Typ BD-J
    app += struct.pack(">H", 1 << 12) + struct.pack(">HBBBB", 1, 1, 0, 0, 0)       # ein Profil 1.0.0
    app += bytes([3, (3 << 6) | (3 << 4)])                                          # Priorität, Bindung, Sichtbarkeit
    app += struct.pack(">H", len(name)) + name + (b"\x00" if len(name) & 1 else b"")
    app += app_string("") + struct.pack(">H", 0)                                    # Symbol
    app += app_string("00000") + app_string("") + app_string(XLET_CLASS)            # JAR, Klassenpfad, Startklasse
    app += bytes([0]) + b"\x00"                                                     # keine Parameter
    table = bytes([1, 0]) + app

    keys = struct.pack(">I", 0)
    access = struct.pack(">H", 1) + b"."
    body = b""
    for block in (terminal, cache, playlists, table):
        body += struct.pack(">I", len(block)) + block
    return b"BDJO0200" + b"\x00" * 40 + body + keys + access


def tool(name):
    """javac/jar aus dem PATH, sonst aus JAVA_HOME."""
    found = shutil.which(name)
    if not found and os.environ.get("JAVA_HOME"):
        found = shutil.which(name, path=os.path.join(os.environ["JAVA_HOME"], "bin"))
    return found or name


def build_jar(jar_api, out):
    """Übersetzt das Xlet gegen libblurays BD-J-Klassen und packt es."""
    work = tempfile.mkdtemp()
    try:
        src = os.path.join(HERE, "tests", "data", "bdj", "MenuXlet.java")
        subprocess.run([tool("javac"), "-nowarn", "-Xlint:-options", "-source", "8", "-target", "8", "-cp", jar_api,
                        "-d", work, src], check=True)
        subprocess.run([tool("jar"), "cf", os.path.abspath(out), "-C", work, "lumentest"], check=True)
    finally:
        shutil.rmtree(work, ignore_errors=True)


def video(seconds, fps):
    """Testbild als H.264 (Annex B, ein Bild je Zugriffseinheit mit Trennzeichen, ohne B-Bilder)."""
    cmd = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
           f"testsrc2=size=1920x1080:rate={fps}", "-t", str(seconds), "-c:v", "libx264", "-preset", "ultrafast",
           "-pix_fmt", "yuv420p", "-profile:v", "high", "-level", "4.1", "-crf", "30",
           "-x264-params", f"aud=1:keyint={fps}:min-keyint={fps}:bframes=0:repeat-headers=1", "-f", "h264", "-"]
    return subprocess.run(cmd, check=True, stdout=subprocess.PIPE).stdout


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--jar", required=True, help="libbluray-j2se-<Version>.jar (BD-J-Klassen zum Übersetzen)")
    ap.add_argument("--seconds", type=int, default=20)
    a = ap.parse_args()

    fps = 24
    aus = access_units(split_nals(video(a.seconds, fps)))
    frame, first = 90000 // fps, 90000
    w = M2tsWriter(PID_BASE, 0x1B)
    w.psi()
    eps = []
    for i, au in enumerate(aus):
        pts = first + i * frame
        w.pcr(pts - 45000)
        if i % fps == 0:
            w.psi()
        spn = w.pes(au["base"], pts)
        if au["idr"]:
            eps.append((pts, spn))
    end = first + len(aus) * frame
    m2ts = w.finish()

    d = lambda *p: os.path.join(a.out, "BDMV", *p)
    for sub in ("STREAM", "CLIPINF", "PLAYLIST", "BACKUP", "AUXDATA", "BDJO", "JAR", "META"):
        os.makedirs(d(sub), exist_ok=True)
    files = {
        d("STREAM", "00001.m2ts"): m2ts,
        d("CLIPINF", "00001.clpi"): clpi(len(m2ts) // PACKET, PID_BASE, 0x1B, first, end, eps),
        d("PLAYLIST", "00000.mpls"): mpls(first // 2, end // 2, ss=False),
        d("index.bdmv"): index_bdmv(),
        d("MovieObject.bdmv"): movie_object(),
        d("BDJO", "00000.bdjo"): bdjo(),
    }
    for path, data in files.items():
        with open(path, "wb") as f:
            f.write(data)
    build_jar(a.jar, d("JAR", "00000.jar"))
    print(f"{len(aus)} Bilder, {len(eps)} Sprungmarken, BD-J-Menü {XLET_CLASS} -> {a.out}")


if __name__ == "__main__":
    main()
