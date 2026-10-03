#!/usr/bin/env python3
"""Kleine PGS-Untertiteldatei (.sup) für Tests: drei Bilder (ein weißer Balken) und je eine Zeile,
die sie wieder löscht.

    python tools/make_test_pgs.py <ausgabe.sup> [start-ende ...]   (Sekunden, Vorgabe 1-3 4-6 7.5-9)
"""
import struct
import sys

W, H = 1920, 1080


def segment(pts, kind, data):
    t = int(round(pts * 90000))
    return b"PG" + struct.pack(">IIBH", t, t, kind, len(data)) + data


def display(pts, show):
    out = b""
    if show:
        pcs = struct.pack(">HHBHBBBB", W, H, 0x10, 1, 0x80, 0, 0, 1) + struct.pack(">HBBHH", 0, 0, 0, 860, 960)
    else:
        pcs = struct.pack(">HHBHBBBB", W, H, 0x10, 2, 0x00, 0, 0, 0)
    out += segment(pts, 0x16, pcs)
    out += segment(pts, 0x17, struct.pack(">BBHHHH", 1, 0, 860, 960, 200, 40))
    if show:
        # Palette: 0 durchsichtig, 1 weiß (Y Cr Cb A)
        out += segment(pts, 0x14, bytes([0, 0]) + bytes([0, 16, 128, 128, 0]) + bytes([1, 235, 128, 128, 255]))
        line = bytes([0x00, 0xC0 | (200 >> 8), 200 & 0xFF, 0x01, 0x00, 0x00])  # 200 Punkte Farbe 1, Zeilenende
        rle = line * 40
        ods = struct.pack(">HBB", 0, 0, 0xC0) + (len(rle) + 4).to_bytes(3, "big") + struct.pack(">HH", 200, 40) + rle
        out += segment(pts, 0x15, ods)
    out += segment(pts, 0x80, b"")
    return out


def main():
    spans = [tuple(float(x) for x in a.split("-")) for a in sys.argv[2:]] or [(1, 3), (4, 6), (7.5, 9)]
    data = b"".join(display(a, True) + display(b, False) for a, b in spans)
    with open(sys.argv[1], "wb") as f:
        f.write(data)


if __name__ == "__main__":
    main()
