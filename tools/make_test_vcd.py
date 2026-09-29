#!/usr/bin/env python3
"""Synthetisches Video-CD-Abbild (CUE/BIN, Mode 2/2352) zum Testen von lumenvcd://.

    python tools/make_test_vcd.py <ffmpeg> <ausgabeordner>

Track 1: leerer Datenbereich (Mode 2 Form 1), Track 2: MPEG-1-Programmstrom aus
FFmpeg ("-f vcd", 2324-Byte-Packs) in Mode-2-Form-2-Rohsektoren. Kein ISO-9660-
Dateisystem – Lumen erkennt das Abbild an den XA-Tracks.
"""
import os
import subprocess
import sys

SYNC = b"\x00" + b"\xff" * 10 + b"\x00"


def bcd(v):
    return ((v // 10) << 4) | (v % 10)


def header(lba):
    f = lba + 150
    return SYNC + bytes([bcd(f // 4500), bcd((f // 75) % 60), bcd(f % 75), 2])


def main():
    ffmpeg, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    mpg = os.path.join(out, "_av.mpg")
    subprocess.run([ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i", "testsrc2=size=352x288:rate=25",
                    "-f", "lavfi", "-i", "sine=f=660:r=44100", "-t", "4", "-target", "pal-vcd", mpg], check=True)
    data = open(mpg, "rb").read()
    os.remove(mpg)
    lead = 300
    with open(os.path.join(out, "vcd.bin"), "wb") as b:
        for lba in range(lead):  # Track 1: Mode 2 Form 1, leer
            b.write(header(lba) + bytes([0, 0, 0x08, 0] * 2) + bytes(2048) + bytes(280))
        for i in range(0, len(data), 2324):
            chunk = data[i:i + 2324].ljust(2324, b"\x00")
            sub = bytes([1, 1, 0x62, 0x0f]) * 2  # Form 2, Echtzeit, Video
            b.write(header(lead + i // 2324) + sub + chunk + bytes(4))
    with open(os.path.join(out, "vcd.cue"), "w") as c:
        c.write('FILE "vcd.bin" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n'
                '  TRACK 02 MODE2/2352\n    INDEX 01 00:04:00\n')
    print("VCD-Abbild:", os.path.join(out, "vcd.cue"))


if __name__ == "__main__":
    main()
