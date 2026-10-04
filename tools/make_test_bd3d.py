#!/usr/bin/env python3
"""Erzeugt eine synthetische, unverschlüsselte Blu-ray-3D-Ordnerstruktur zum Testen.

    python tools/make_test_bd3d.py mvc.h264 out_dir [--fps 24]

Eingabe: H.264/MVC-Annex-B-Strom (z. B. von FFmpeg-mvc tests/fate/h264-mvc/mvc-mkfix.pl).
Aufbau wie auf einer echten 3D-Disc:
  BDMV/STREAM/00001.m2ts   Basisansicht,     PID 0x1011 (stream_type 0x1B)
  BDMV/STREAM/00002.m2ts   abhängige Ansicht, PID 0x1012 (stream_type 0x20, NAL 15/20)
  BDMV/CLIPINF/*.clpi     inkl. EP-Map (Sprungmarken)
  BDMV/PLAYLIST/00000.mpls PlayItem 00001 + SS-Subpfad (Typ 8) -> 00002
  BDMV/index.bdmv          ein Titel, 3D-Inhalt markiert
Die Dateiformate folgen den libbluray-Parsern (clpi_parse.c, mpls_parse.c, index_parse.c).
"""
import argparse
import os
import struct

PID_PAT, PID_PMT, PID_PCR, PID_BASE, PID_DEP = 0x0000, 0x0100, 0x1001, 0x1011, 0x1012
PACKET = 192


def crc32_mpeg(data):
    crc = 0xFFFFFFFF
    for b in data:
        crc ^= b << 24
        for _ in range(8):
            crc = ((crc << 1) ^ 0x04C11DB7) & 0xFFFFFFFF if crc & 0x80000000 else (crc << 1) & 0xFFFFFFFF
    return crc


def split_nals(data):
    """Annex-B -> Liste von NAL-Einheiten (inkl. Startcode)."""
    starts = []
    i = 0
    while True:
        i = data.find(b"\x00\x00\x01", i)
        if i < 0:
            break
        s = i - 1 if i > 0 and data[i - 1] == 0 else i
        starts.append((s, i + 3))
        i += 3
    nals = []
    for k, (s, h) in enumerate(starts):
        end = starts[k + 1][0] if k + 1 < len(starts) else len(data)
        nals.append((data[h] & 0x1F, data[s:end]))
    return nals


def access_units(nals):
    """Gruppiert NALs zu Zugriffseinheiten (Start bei AUD, Typ 9) und trennt die Ansichten."""
    aus, cur, in_dep = [], None, False
    for t, nal in nals:
        if t == 9 or cur is None:
            cur = {"base": b"", "dep": b"", "idr": False}
            aus.append(cur)
            in_dep = False
        # Subset-SPS (15), MVC-Slices (20; FFmpeg-mvc-Fixtures: 19), Delimiter (24)
        # und die danach folgenden PPS/SEI gehören zur abhängigen Ansicht
        if t in (15, 19, 20, 24):
            in_dep = True
        if in_dep:
            cur["dep"] += nal
        else:
            cur["base"] += nal
            if t == 5:
                cur["idr"] = True
    return [a for a in aus if a["base"]]


class M2tsWriter:
    def __init__(self, pid, stream_type):
        self.pid, self.stream_type = pid, stream_type
        self.packets = []
        self.cc = {}
        self.ats = 0

    def _packet(self, pid, payload, pusi=False, af=None):
        cc = self.cc.get(pid, 0)
        head = bytes([0x47, (0x40 if pusi else 0) | (pid >> 8), pid & 0xFF])
        if af is None and len(payload) < 184:
            af = b""
        if af is not None:
            stuff = 184 - len(payload) - 1 - len(af)
            af_len = len(af) + stuff
            afield = bytes([af_len]) + (af + b"\xff" * stuff if af else (b"\x00" + b"\xff" * (stuff - 1) if stuff > 0 else b""))
            ctrl = 0x30 if payload else 0x20
            ts = head + bytes([ctrl | (cc & 0xF)]) + afield + payload
        else:
            ts = head + bytes([0x10 | (cc & 0xF)]) + payload
        if payload:
            self.cc[pid] = (cc + 1) & 0xF
        assert len(ts) == 188, len(ts)
        self.packets.append(struct.pack(">I", self.ats & 0x3FFFFFFF) + ts)
        self.ats += 27000000 // 5000  # nominell 5000 Pakete/s

    def section(self, pid, table):
        self._packet(pid, b"\x00" + table + b"\xff" * (183 - len(table)), pusi=True)

    def psi(self):
        pat = bytes([0x00, 0xB0, 13, 0x00, 0x01, 0xC1, 0x00, 0x00, 0x00, 0x01, 0xE0 | (PID_PMT >> 8), PID_PMT & 0xFF])
        pat += struct.pack(">I", crc32_mpeg(pat))
        es = bytes([self.stream_type, 0xE0 | (self.pid >> 8), self.pid & 0xFF, 0xF0, 0x00])
        body = bytes([0x00, 0x01, 0xC1, 0x00, 0x00, 0xE0 | (PID_PCR >> 8), PID_PCR & 0xFF, 0xF0, 0x00]) + es
        pmt = bytes([0x02, 0xB0 | ((len(body) + 4) >> 8), (len(body) + 4) & 0xFF]) + body
        pmt += struct.pack(">I", crc32_mpeg(pmt))
        self.section(PID_PAT, pat)
        self.section(PID_PMT, pmt)

    def pcr(self, pcr90):
        base = pcr90 & 0x1FFFFFFFF
        af = bytes([0x10, (base >> 25) & 0xFF, (base >> 17) & 0xFF, (base >> 9) & 0xFF, (base >> 1) & 0xFF,
                    ((base & 1) << 7) | 0x7E, 0x00])
        self._packet(PID_PCR, b"", af=af)

    @staticmethod
    def _ts(prefix, v):
        return bytes([(prefix << 4) | (((v >> 30) & 7) << 1) | 1, (v >> 22) & 0xFF, (((v >> 15) & 0x7F) << 1) | 1,
                      (v >> 7) & 0xFF, ((v & 0x7F) << 1) | 1])

    def pes(self, es, pts):
        """Schreibt eine Zugriffseinheit; liefert die SPN (Paketnummer) des PES-Beginns."""
        spn = len(self.packets)
        data = b"\x00\x00\x01\xE0\x00\x00\x80\xC0\x0A" + self._ts(3, pts) + self._ts(1, pts) + es
        first = True
        while data:
            chunk, data = data[:184], data[184:]
            self._packet(self.pid, chunk, pusi=first)
            first = False
        return spn

    def finish(self):
        while len(self.packets) % 32:     # Aligned Units (6144 Byte)
            self.packets.append(struct.pack(">I", self.ats) + bytes([0x47, 0x1F, 0xFF, 0x10]) + b"\xff" * 184)
        return b"".join(self.packets)


def clpi(num_packets, pid, coding, first_pts, end_pts, eps):
    # ClipInfo @40
    ci = struct.pack(">HBB", 0, 1, 1) + struct.pack(">I", 0) + struct.pack(">II", 6000000, num_packets) + b"\x00" * 128 + b"\x00\x00"
    clipinfo = struct.pack(">I", len(ci)) + ci
    seq = b"\x00" + bytes([1]) + struct.pack(">IBB", 0, 1, 0) + struct.pack(">HIII", PID_PCR, 0, first_pts // 2, end_pts // 2)
    sequence = struct.pack(">I", len(seq)) + seq
    attr = bytes([5, coding, (6 << 4) | 2, (3 << 4), 0, 0])  # 1080p, 24 Hz, 16:9
    prg = b"\x00" + bytes([1]) + struct.pack(">IHBB", 0, PID_PMT, 1, 0) + struct.pack(">H", pid) + attr
    program = struct.pack(">I", len(prg)) + prg
    # CPI / EP-Map: je Sprungmarke ein Coarse- und ein Fine-Eintrag
    n = len(eps)
    ep_head = bytes([0, 1]) + struct.pack(">H", pid)
    ep_head += struct.pack(">I", (0 << 22) | (1 << 18) | (n << 2) | ((n >> 16) & 3)) + struct.pack(">H", n & 0xFFFF)
    ep_head += struct.pack(">I", 2 + 12)  # Start der Stream-Daten relativ zur EP-Map
    coarse = b"".join(struct.pack(">II", (i << 14) | ((pts >> 19) & 0x3FFF), spn) for i, (pts, spn) in enumerate(eps))
    fine = b"".join(struct.pack(">I", (((pts >> 9) & 0x7FF) << 17) | (spn & 0x1FFFF)) for pts, spn in eps)
    stream = struct.pack(">I", 4 + len(coarse)) + coarse + fine
    cpi_body = struct.pack(">H", 1) + ep_head + stream
    cpi = struct.pack(">I", len(cpi_body)) + cpi_body
    mark = struct.pack(">I", 0)

    seq_start = 40 + len(clipinfo)
    prog_start = seq_start + len(sequence)
    cpi_start = prog_start + len(program)
    mark_start = cpi_start + len(cpi)
    head = b"HDMV0200" + struct.pack(">IIIII", seq_start, prog_start, cpi_start, mark_start, 0)
    head += b"\x00" * (40 - len(head))
    return head + clipinfo + sequence + program + cpi + mark


def mpls(in45, out45, ss=True):
    uo = b"\x00" * 8
    appinfo_body = bytes([0, 1]) + b"\x00\x00" + uo + b"\x00\x00"  # playback_type 1 (sequentiell)
    appinfo = struct.pack(">I", len(appinfo_body)) + appinfo_body
    video = bytes([9, 1]) + struct.pack(">H", PID_BASE) + b"\x00" * 6 + bytes([5, 0x1B, (6 << 4) | 2, 0, 0, 0])
    stn_body = b"\x00\x00" + bytes([1, 0, 0, 0, 0, 0, 0, 0]) + b"\x00" * 4 + video
    stn = struct.pack(">H", len(stn_body)) + stn_body
    pi_body = b"00001" + b"M2TS" + struct.pack(">H", 1) + bytes([0]) + struct.pack(">II", in45, out45) + uo + bytes([0x80, 0]) + b"\x00\x00" + stn
    playitem = struct.pack(">H", len(pi_body)) + pi_body
    pl_body = b"\x00\x00" + struct.pack(">HH", 1, 0) + playitem
    playlist = struct.pack(">I", len(pl_body)) + pl_body
    mark_body = struct.pack(">H", 1) + bytes([0, 1]) + struct.pack(">HIHI", 0, in45, 0xFFFF, 0)
    marks = struct.pack(">I", len(mark_body)) + mark_body

    # ExtensionData: SubPath_entries_extension (ID 2/2) mit SS-Subpfad (Typ 8)
    # 27 Bit Reserve, connection_condition (4) = 1, is_multi_clip (1) = 0
    spi_body = b"00002" + b"M2TS" + struct.pack(">I", 1 << 1) + bytes([0]) + struct.pack(">IIHI", in45, out45, 0, in45)
    spi = struct.pack(">H", len(spi_body)) + spi_body
    sp_body = bytes([0, 8]) + b"\x00\x00" + bytes([0, 1]) + spi
    subpath = struct.pack(">I", len(sp_body)) + sp_body
    entry_data = struct.pack(">IH", 2 + len(subpath), 1) + subpath
    # Kopf: Länge(4) Datenblock-Start(4) Reserve(3) Anzahl(1) + Eintrag(12) = 24 Byte
    header = struct.pack(">I", 24) + b"\x00\x00\x00" + bytes([1]) + struct.pack(">HHII", 2, 2, 24, len(entry_data))
    ext_block = struct.pack(">I", len(header) + len(entry_data)) + header + entry_data

    list_pos = 40 + len(appinfo)
    mark_pos = list_pos + len(playlist)
    ext_pos = mark_pos + len(marks)
    if not ss:
        ext_block, ext_pos = b"", 0  # 2D: ohne Erweiterungsdaten
    head = b"MPLS0200" + struct.pack(">III", list_pos, mark_pos, ext_pos) + b"\x00" * 20
    return head + appinfo + playlist + marks + ext_block


def index_bdmv():
    appinfo_body = bytes([0x60, (6 << 4) | 2]) + b"\x00" * 32  # 3D-Ausgabe bevorzugt, 3D-Inhalt vorhanden
    appinfo = struct.pack(">I", 34) + appinfo_body
    def hdmv(ref):
        # Objekttyp HDMV (2 Bit = 1), dann playback_type/Reserve (16 Bit), Movie-Object-ID, Reserve
        return struct.pack(">I", 1 << 30) + struct.pack(">HHI", 0x4000, ref, 0)

    first_play = hdmv(0xFFFF)
    top_menu = hdmv(0xFFFF)
    title = hdmv(0)
    idx_body = first_play + top_menu + struct.pack(">H", 1) + title
    indexes = struct.pack(">I", len(idx_body)) + idx_body
    head = b"INDX0200" + struct.pack(">II", 40 + len(appinfo), 0)
    head += b"\x00" * (40 - len(head))
    return head + appinfo + indexes


def movie_object():
    # Ein Movie Object: "PlayPL 0"
    cmd = struct.pack(">III", 0x22800000, 0, 0)
    obj = struct.pack(">HH", 0x8000, 1) + cmd
    body = b"\x00\x00\x00\x00" + struct.pack(">H", 1) + obj
    return b"MOBJ0200" + struct.pack(">I", 0) + b"\x00" * 28 + struct.pack(">I", len(body)) + body


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("h264")
    ap.add_argument("out")
    ap.add_argument("--fps", type=float, default=24.0)
    a = ap.parse_args()

    aus = access_units(split_nals(open(a.h264, "rb").read()))
    frame = int(round(90000 / a.fps))
    first = 90000
    base, dep = M2tsWriter(PID_BASE, 0x1B), M2tsWriter(PID_DEP, 0x20)
    base_eps, dep_eps = [], []
    for w in (base, dep):
        w.psi()
    for i, au in enumerate(aus):
        pts = first + i * frame
        for w in (base, dep):
            w.pcr(pts - 45000)
            if i % 24 == 0:
                w.psi()
        spn_b = base.pes(au["base"], pts)
        spn_d = dep.pes(au["dep"], pts) if au["dep"] else None
        if au["idr"]:
            base_eps.append((pts, spn_b))
            if spn_d is not None:
                dep_eps.append((pts, spn_d))
    end = first + len(aus) * frame

    d = lambda *p: os.path.join(a.out, "BDMV", *p)
    for sub in ("STREAM", "CLIPINF", "PLAYLIST", "BACKUP", "AUXDATA", "BDJO", "JAR", "META"):
        os.makedirs(d(sub), exist_ok=True)
    os.makedirs(d("STREAM", "SSIF"), exist_ok=True)
    files = {
        d("STREAM", "00001.m2ts"): base.finish(),
        d("STREAM", "00002.m2ts"): dep.finish(),
        d("index.bdmv"): index_bdmv(),
        d("MovieObject.bdmv"): movie_object(),
        d("PLAYLIST", "00000.mpls"): mpls(first // 2, end // 2),
    }
    files[d("CLIPINF", "00001.clpi")] = clpi(len(files[d("STREAM", "00001.m2ts")]) // PACKET, PID_BASE, 0x1B, first, end, base_eps)
    files[d("CLIPINF", "00002.clpi")] = clpi(len(files[d("STREAM", "00002.m2ts")]) // PACKET, PID_DEP, 0x20, first, end, dep_eps)
    for path, data in files.items():
        with open(path, "wb") as f:
            f.write(data)
    print(f"{len(aus)} Zugriffseinheiten, {len(base_eps)} Sprungmarken -> {a.out}")


if __name__ == "__main__":
    main()
