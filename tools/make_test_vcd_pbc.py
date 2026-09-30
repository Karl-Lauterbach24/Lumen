#!/usr/bin/env python3
"""Video-CD 2.0 mit Wiedergabesteuerung (PBC) für tests/vcd_test.cpp.

    python tools/make_test_vcd_pbc.py <ffmpeg> <vcdxbuild> <ausgabeordner>

Das Abbild (pbc.cue/pbc.bin) erstellt vcdxbuild aus GNU VCDImager nach dem
White Book – unabhängig von Lumens eigenem PBC-Leser:
  LID 1  Auswahl: Menübild (Segment), 1 = Film, 2 = Extras, 3 = Film ab
         Einsprungpunkt 2 s, Default = Film
  LID 2  Film (4 s, Testbild)  -> Next Extras, Prev/Return Menü
  LID 3  Extras (3 s, rot)     -> Next Menü, Prev Film
  LID 4  Film ab 2 s           -> Next Menü
"""
import os
import subprocess
import sys

XML = """<?xml version="1.0"?>
<!DOCTYPE videocd PUBLIC "-//GNU//DTD VideoCD//EN" "http://www.gnu.org/software/vcdimager/videocd.dtd">
<videocd xmlns="http://www.gnu.org/software/vcdimager/1.0/" class="vcd" version="2.0">
  <info><album-id>LUMENPBC</album-id><volume-count>1</volume-count><volume-number>1</volume-number></info>
  <pvd><volume-id>LUMEN_PBC</volume-id><system-id>CD-RTOS CD-BRIDGE</system-id></pvd>
  <segment-items><segment-item src="menu.mpg" id="seg-menu"/></segment-items>
  <sequence-items>
    <sequence-item src="film.mpg" id="seq-film"><entry id="entry-film-2s">2.000000</entry></sequence-item>
    <sequence-item src="extras.mpg" id="seq-extras"/>
  </sequence-items>
  <pbc>
    <selection id="lid-menu">
      <bsn>1</bsn><default ref="lid-film"/><wait>-1</wait><loop jump-timing="immediate">1</loop>
      <play-item ref="seg-menu"/>
      <select ref="lid-film"/><select ref="lid-extras"/><select ref="lid-chapter"/>
    </selection>
    <playlist id="lid-film"><prev ref="lid-menu"/><next ref="lid-extras"/><return ref="lid-menu"/><wait>0</wait>
      <play-item ref="seq-film"/></playlist>
    <playlist id="lid-extras"><prev ref="lid-film"/><next ref="lid-menu"/><return ref="lid-menu"/><wait>0</wait>
      <play-item ref="seq-extras"/></playlist>
    <playlist id="lid-chapter"><next ref="lid-menu"/><return ref="lid-menu"/><wait>0</wait>
      <play-item ref="entry-film-2s"/></playlist>
    <endlist id="lid-end" rejected="true"/>
  </pbc>
</videocd>
"""


def main():
    ffmpeg, vcdxbuild, out = sys.argv[1:4]
    os.makedirs(out, exist_ok=True)

    def run(*args):
        subprocess.run([ffmpeg, "-hide_banner", "-loglevel", "error", "-y", *args], check=True, cwd=out)

    boxes = ",".join(f"drawbox=x=80:y={y}:w=192:h=40:c={c}:t=fill" for y, c in ((60, "0xf0f0f0"), (124, "0xf0c000"), (188, "0x40c040")))
    run("-f", "lavfi", "-i", f"color=c=0x1a2a6c:s=352x288:r=25,{boxes}", "-frames:v", "1", "-target", "pal-vcd", "-an", "menu.mpg")
    run("-f", "lavfi", "-i", "testsrc2=size=352x288:rate=25", "-f", "lavfi", "-i", "sine=f=660:r=44100", "-t", "4",
        "-target", "pal-vcd", "film.mpg")
    run("-f", "lavfi", "-i", "color=c=red:s=352x288:r=25", "-f", "lavfi", "-i", "sine=f=330:r=44100", "-t", "3",
        "-target", "pal-vcd", "extras.mpg")
    open(os.path.join(out, "pbc.xml"), "w").write(XML)
    subprocess.run([vcdxbuild, "--cue-file=pbc.cue", "--bin-file=pbc.bin", "pbc.xml"], check=True, cwd=out)
    print(f"PBC-VCD geschrieben: {os.path.join(out, 'pbc.cue')}")


if __name__ == "__main__":
    sys.exit(main())
