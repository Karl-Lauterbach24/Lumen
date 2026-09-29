#!/usr/bin/env python3
"""Test-DVD mit Menü für Lumen (DVD-Navigation, SPU-Dekoder, Hervorhebung).

    python tools/make_test_dvd.py <ffmpeg> <dvdauthor> <spumux> <ausgabe>

Erzeugt eine PAL-DVD (VIDEO_TS) mit:
  - Hauptmenü (VMGM): Standbild (pause=inf) mit zwei Buttons
      Button 1 -> Titel 1, Button 2 -> Titel 1 Kapitel 2
      Hervorhebung/Auswahl als Subpicture (spumux)
  - Titel 1: 6 s, 16:9, zwei Kapitel (0 s, 3 s), zwei Tonspuren (de/en),
    Untertitelspur (de) mit einer Einblendung bei 1–2,5 s; danach zurück ins Menü
Benötigt dvdauthor/spumux (z. B. selbst gebaut, siehe README "Tests").
"""
import os
import struct
import subprocess
import sys
import zlib


def png(path, boxes, w=720, h=576):
    """RGBA-PNG mit deckenden Rechtecken (x, y, breite, höhe, (r, g, b), rahmen) auf transparentem Grund."""
    px = bytearray(w * h * 4)
    for x0, y0, bw, bh, rgb, frame in boxes:
        for y in range(y0, y0 + bh):
            for x in range(x0, x0 + bw):
                if frame and x0 + frame <= x < x0 + bw - frame and y0 + frame <= y < y0 + bh - frame:
                    continue
                o = (y * w + x) * 4
                px[o:o + 4] = bytes(rgb) + b"\xff"
    raw = b"".join(b"\x00" + bytes(px[y * w * 4:(y + 1) * w * 4]) for y in range(h))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def run(*args, **kw):
    subprocess.run(list(args), check=True, **kw)


def main():
    ffmpeg, dvdauthor, spumux, out = sys.argv[1:5]
    work = os.path.join(out, "_work")
    os.makedirs(work, exist_ok=True)
    q = ["-hide_banner", "-loglevel", "error", "-y"]

    # Menü-Hintergrund mit zwei Button-Flächen
    run(ffmpeg, *q, "-f", "lavfi", "-i", "color=c=0x202a44:s=720x576:d=1:r=25",
        "-vf", "drawbox=x=160:y=200:w=400:h=60:color=0x3b5bdb:t=fill,drawbox=x=160:y=320:w=400:h=60:color=0x2f9e44:t=fill",
        "-frames:v", "1", os.path.join(work, "menu_bg.png"))
    run(ffmpeg, *q, "-loop", "1", "-i", os.path.join(work, "menu_bg.png"), "-f", "lavfi", "-i", "anullsrc=r=48000:cl=stereo",
        "-t", "1", "-target", "pal-dvd", "-aspect", "4:3", os.path.join(work, "menu.mpg"))
    # Hervorhebung (Rahmen) und Auswahl (gefüllt), je eine Farbe auf transparentem Grund
    png(os.path.join(work, "hl.png"), [(156, 196, 408, 68, (255, 220, 0), 6), (156, 316, 408, 68, (255, 220, 0), 6)])
    png(os.path.join(work, "sel.png"), [(156, 196, 408, 68, (255, 255, 255), 0), (156, 316, 408, 68, (255, 255, 255), 0)])
    with open(os.path.join(work, "menu.xml"), "w") as f:
        f.write(f"""<subpictures><stream><spu start="00:00:00.00" force="yes"
 highlight="{os.path.join(work, 'hl.png')}" select="{os.path.join(work, 'sel.png')}">
<button name="b1" x0="156" y0="196" x1="563" y1="263" down="b2"/>
<button name="b2" x0="156" y0="316" x1="563" y1="383" up="b1"/>
</spu></stream></subpictures>
""")
    with open(os.path.join(work, "menu.mpg"), "rb") as src, open(os.path.join(work, "menu_spu.mpg"), "wb") as dst:
        run(spumux, "-m", "dvd", os.path.join(work, "menu.xml"), stdin=src, stdout=dst)

    # Titel: 16:9, zwei Tonspuren
    run(ffmpeg, *q, "-f", "lavfi", "-i", "testsrc2=size=720x576:rate=25", "-f", "lavfi", "-i", "sine=f=440:r=48000",
        "-f", "lavfi", "-i", "sine=f=880:r=48000", "-t", "6", "-map", "0:v", "-map", "1:a", "-map", "2:a",
        "-target", "pal-dvd", "-aspect", "16:9", os.path.join(work, "title.mpg"))
    png(os.path.join(work, "sub.png"), [(200, 470, 320, 40, (255, 255, 255), 0), (200, 470, 320, 40, (0, 0, 0), 3)])
    with open(os.path.join(work, "sub.xml"), "w") as f:
        f.write(f"""<subpictures><stream>
<spu start="00:00:01.00" end="00:00:02.50" image="{os.path.join(work, 'sub.png')}"/>
</stream></subpictures>
""")
    with open(os.path.join(work, "title.mpg"), "rb") as src, open(os.path.join(work, "title_spu.mpg"), "wb") as dst:
        run(spumux, "-m", "dvd", os.path.join(work, "sub.xml"), stdin=src, stdout=dst)

    dvd = os.path.join(out, "DVD")
    with open(os.path.join(work, "dvd.xml"), "w") as f:
        f.write(f"""<dvdauthor dest="{dvd}" jumppad="yes">
<vmgm><menus><video format="pal" aspect="4:3"/><subpicture><stream id="0" mode="normal"/></subpicture>
<pgc entry="title"><vob file="{os.path.join(work, 'menu_spu.mpg')}" pause="inf"/>
<button name="b1">jump title 1;</button>
<button name="b2">jump title 1 chapter 2;</button>
</pgc></menus></vmgm>
<titleset><titles><video format="pal" aspect="16:9" widescreen="nopanscan"/>
<audio lang="de"/><audio lang="en"/><subpicture lang="de"/>
<pgc><vob file="{os.path.join(work, 'title_spu.mpg')}" chapters="0,0:03"/><post>call vmgm menu 1;</post></pgc>
</titles></titleset></dvdauthor>
""")
    env = dict(os.environ, VIDEO_FORMAT="PAL")
    run(dvdauthor, "-x", os.path.join(work, "dvd.xml"), env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print("DVD:", dvd)


if __name__ == "__main__":
    main()
