#!/usr/bin/env python3
"""Synthetisches Test-DCP (SMPTE) für Lumen – optional verschlüsselt mit KDM.

    python tools/make_test_dcp.py <ffmpeg> <openssl> <ausgabe> [--encrypt <leaf.pem>]

Erzeugt mit FFmpeg ein JPEG-2000-Bild-MXF (2048x858, 24 fps, 2 s) und ein
PCM-Ton-MXF, dazu CPL (2 Rollen über dieselben Spurdateien mit verschiedenen
Einstiegspunkten, Marker FFOC/LFOC/FFEC), PKL, ASSETMAP und Interop-Untertitel.

--encrypt: Essenz-KLVs werden zu SMPTE-429-6-Triplets (AES-128-CBC, Prüfwert
"CHUKCHUK…"), Partitionen/Index/RIP werden auf die neuen Offsets angepasst.
Zusätzlich entstehen kdm.xml (RSA-OAEP für das angegebene Leaf-Zertifikat, z. B.
aus "dcp_test gencert") und keys.txt (Schlüssel-ID + Schlüssel, zum Vergleich).
Die Verschlüsselung erfolgt mit dem openssl-Programm (keine Python-Pakete nötig).
"""
import argparse
import base64
import hashlib
import os
import struct
import subprocess
import sys
import uuid
import zlib

TRIPLET = bytes.fromhex("060e2b34020401070d010301027e0100")
PARTITION_PREFIX = bytes.fromhex("060e2b34020501010d01020101")  # + Typ (02 Kopf, 03 Body, 04 Fuß)
INDEX_KEY = bytes.fromhex("060e2b34025301010d01020110010000")
RIP_KEY = bytes.fromhex("060e2b34020501010d01020101110100")
CHECK = b"CHUK" * 4
CONTEXT = uuid.uuid4().bytes


def ber(n, size=4):
    return bytes([0x80 | (size - 1)]) + n.to_bytes(size - 1, "big")


def read_klvs(data):
    pos, out = 0, []
    while pos + 17 <= len(data):
        key = data[pos:pos + 16]
        b = data[pos + 16]
        if b & 0x80:
            n = b & 0x7F
            length = int.from_bytes(data[pos + 17:pos + 17 + n], "big")
            hdr = 17 + n
        else:
            length, hdr = b, 17
        out.append((pos, key, hdr, length))
        pos += hdr + length
    return out


def is_essence(key):
    return key[:4] == b"\x06\x0e\x2b\x34" and key[4] == 1 and key[5] == 2 and key[8:12] in (b"\x0d\x01\x03\x01", b"\x0e\x09\x06\x01")


# Immersive Audio (Dolby Atmos / SMPTE ST 429-18)
IAB_ELEMENT = bytes.fromhex("060e2b34010201050e09060100000001")
IAB_DESCRIPTOR = bytes.fromhex("060e2b34025301050e09060300000000")


def write_iab_mxf(frames_path, dst):
    """Minimale IAB-Spurdatei: Kopf-Partition, Deskriptor, je Edit Unit ein
    ImmersiveAudioDataElement (Preamble + IAFrame aus iab_testgen), Fuß-Partition."""
    data = open(frames_path, "rb").read()
    frames, pos = [], 0
    while pos + 4 <= len(data):
        n = struct.unpack("<I", data[pos:pos + 4])[0]
        frames.append(data[pos + 4:pos + 4 + n])
        pos += 4 + n
    op1a = bytes.fromhex("060e2b34040101010d01020101010900")
    container = bytes.fromhex("060e2b34040101050e09060701010100")

    def partition(kind, this, footer):
        v = struct.pack(">HHIQQQQQIQI", 1, 3, 1, this, 0, footer, 0, 0, 0, 0, 0)
        v += op1a + struct.pack(">II", 1, 16) + container
        key = PARTITION_PREFIX + bytes([kind, 4, 0])
        return key + ber(len(v)) + v

    # Deskriptor als lokales Set: InstanceUID, SampleRate 24/1, ContainerDuration
    desc = (struct.pack(">HH", 0x3C0A, 16) + uuid.uuid4().bytes + struct.pack(">HHII", 0x3001, 8, 24, 1)
            + struct.pack(">HHQ", 0x3002, 8, len(frames)))
    body = IAB_DESCRIPTOR + ber(len(desc)) + desc
    body += b"".join(IAB_ELEMENT + ber(len(f)) + f for f in frames)
    head_len = len(partition(2, 0, 0))
    footer_pos = head_len + len(body)
    out = partition(2, 0, footer_pos) + body + partition(4, footer_pos, footer_pos)
    open(dst, "wb").write(out)
    return len(frames)


def aes_cbc(openssl, key, iv, data):
    r = subprocess.run([openssl, "enc", "-aes-128-cbc", "-nopad", "-K", key.hex(), "-iv", iv.hex()],
                       input=data, capture_output=True, check=True)
    return r.stdout


def encrypt_mxf(src, dst, key, openssl, track_file_id):
    def triplet(k, value, seq):
        iv = os.urandom(16)
        pad = 16 - (len(value) % 16)
        enc = aes_cbc(openssl, key, iv, CHECK + value + bytes([pad]) * pad)
        esv = iv + enc
        body = (ber(16) + CONTEXT + ber(8) + (0).to_bytes(8, "big") + ber(16) + k + ber(8) + len(value).to_bytes(8, "big")
                + ber(len(esv)) + esv + ber(16) + track_file_id + ber(8) + seq.to_bytes(8, "big"))
        return TRIPLET + ber(len(body)) + body
    rewrite_mxf(src, dst, triplet)


def stereo_mxf(left, right, dst):
    """3D-Spurdatei (SMPTE 429-10): je Edit Unit linkes und rechtes JPEG-2000-Element."""
    rdata = open(right, "rb").read()
    rvalues = [rdata[p + h:p + h + n] for p, k, h, n in read_klvs(rdata) if is_essence(k)]

    def pair(k, value, seq):
        lk = k[:13] + bytes([2]) + k[14:15] + bytes([1])
        rk = k[:13] + bytes([2]) + k[14:15] + bytes([2])
        rv = rvalues[seq - 1]
        return lk + ber(len(value)) + value + rk + ber(len(rv)) + rv
    rewrite_mxf(left, dst, pair)


def rewrite_mxf(src, dst, essence_fn):
    """Essenz-KLVs ersetzen (essence_fn(key, value, nr) -> Bytes) und Partitionen,
    Index-Tabellen und RIP auf die neuen Offsets anpassen."""
    data = open(src, "rb").read()
    klvs = read_klvs(data)
    out = bytearray()
    remap = {}
    seq = 0
    for pos, k, hdr, length in klvs:
        remap[pos] = len(out)
        value = data[pos + hdr:pos + hdr + length]
        if is_essence(k):
            seq += 1
            out += essence_fn(k, value, seq)
        else:
            out += data[pos:pos + hdr + length]
    remap[len(data)] = len(out)

    def new(off):
        return remap[off]

    # Essenzbereiche je Body-Partition: alte -> neue Stream-Offsets
    parts = [(p, k, h, l) for p, k, h, l in klvs if k[:13] == PARTITION_PREFIX and k[13] in (2, 3, 4)]
    body_regions = []  # (old_start, new_start, body_offset)
    for i, (p, k, h, l) in enumerate(parts):
        v = data[p + h:p + h + l]
        body_sid = struct.unpack(">I", v[60:64])[0]
        if body_sid == 0:
            continue
        start = p + h + l
        header_bytes, index_bytes = struct.unpack(">QQ", v[32:48])
        # nachfolgendes Fill überspringen
        nxt = [x for x in klvs if x[0] == start]
        if nxt and nxt[0][1][8:12] == b"\x03\x01\x02\x10":
            start += nxt[0][2] + nxt[0][3]
        start += header_bytes + index_bytes
        body_regions.append([start, new(start), 0])
    # neue BodyOffsets = aufsummierte neue Essenzgrößen
    ends = [p for p, *_ in parts[1:]] + [len(data)]
    running = 0
    for region in body_regions:
        region[2] = running
        end_old = min(e for e in ends if e > region[0])
        running += new(end_old) - region[1]

    def stream_offset(old_so):
        # Body-Partition mit alten BodyOffsets bestimmen
        old_running, best = 0, None
        for region in body_regions:
            end_old = min(e for e in ends if e > region[0])
            size = end_old - region[0]
            if old_so < old_running + size:
                best = (region, old_so - old_running)
                break
            old_running += size
        if best is None:
            return old_so
        region, rel = best
        return region[2] + new(region[0] + rel) - region[1]

    # Partition-Packs, Index-Tabellen, RIP anpassen
    for pos, k, hdr, length in klvs:
        n = new(pos)
        if k[:13] == PARTITION_PREFIX and k[13] in (2, 3, 4):
            v = n + hdr
            this_p, prev_p, footer_p = struct.unpack(">QQQ", out[v + 8:v + 32])
            struct.pack_into(">QQQ", out, v + 8, new(this_p), new(prev_p) if prev_p in remap else prev_p,
                             new(footer_p) if footer_p in remap else footer_p)
            body_sid = struct.unpack(">I", out[v + 60:v + 64])[0]
            if body_sid:
                start_old = pos + hdr + length
                for region in body_regions:
                    if region[0] >= start_old and all(not (start_old < p2 < region[0]) for p2, *_ in parts):
                        struct.pack_into(">Q", out, v + 52, region[2])
                        break
        elif k == INDEX_KEY:
            t = n + hdr
            end = t + length
            slices = 0
            while t + 4 <= end:
                tag, l = struct.unpack(">HH", out[t:t + 4])
                if tag == 0x3F08:
                    slices = out[t + 4]
                if tag == 0x3F0A:
                    count, size = struct.unpack(">II", out[t + 4:t + 12])
                    for i in range(count):
                        e = t + 12 + i * size
                        so = struct.unpack(">Q", out[e + 3:e + 11])[0]
                        struct.pack_into(">Q", out, e + 3, stream_offset(so))
                t += 4 + l
        elif k == RIP_KEY:
            v = n + hdr
            for i in range((length - 4) // 12):
                off = v + i * 12 + 4
                old = struct.unpack(">Q", out[off:off + 8])[0]
                if old in remap:
                    struct.pack_into(">Q", out, off, new(old))
    open(dst, "wb").write(out)


def write_png(path, w, h, rgba):
    """Einfarbiges RGBA-PNG mit 4 px transparentem Rand (Bilduntertitel)."""
    clear = bytes(4)
    raw = b"".join(b"\x00" + b"".join(bytes(rgba) if 4 <= x < w - 4 and 4 <= y < h - 4 else clear for x in range(w))
                   for y in range(h))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def sha1_b64(path):
    return base64.b64encode(hashlib.sha1(open(path, "rb").read()).digest()).decode()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ffmpeg")
    ap.add_argument("openssl")
    ap.add_argument("out")
    ap.add_argument("--encrypt", metavar="LEAF_PEM")
    ap.add_argument("--stereo", action="store_true", help="3D-DCP: linkes Auge Testbild, rechtes Auge rot")
    ap.add_argument("--iab", metavar="FRAMES", help="IAB/Atmos-Spur aus iab_testgen-Frames (48 = 2 Rollen à 24)")
    ap.add_argument("--valid-from", default="2026-01-01T00:00:00+00:00", help="Beginn des KDM-Zeitraums (ISO 8601 mit Zone)")
    ap.add_argument("--valid-until", default="2036-01-01T00:00:00+00:00", help="Ende des KDM-Zeitraums (ISO 8601 mit Zone)")
    a = ap.parse_args()
    if len(a.valid_from) != 25 or len(a.valid_until) != 25:
        sys.exit("--valid-from/--valid-until: Form 2026-01-01T00:00:00+00:00 (25 Zeichen, wie im KDM-Schlüsselblock)")
    os.makedirs(a.out, exist_ok=True)
    tmp = os.path.join(a.out, "_tmp")
    os.makedirs(tmp, exist_ok=True)

    pic_plain = os.path.join(tmp, "pic.mxf")
    snd_plain = os.path.join(tmp, "snd.mxf")
    subprocess.run([a.ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
                    "testsrc2=size=2048x858:rate=24", "-t", "2", "-c:v", "jpeg2000", "-pix_fmt", "xyz12le",
                    "-b:v", "6M", "-f", "mxf", pic_plain], check=True)
    subprocess.run([a.ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
                    "sine=f=440:r=48000:d=2", "-c:a", "pcm_s24le", "-mxf_audio_edit_rate", "24",
                    "-f", "mxf_opatom", snd_plain], check=True)

    ids = {n: str(uuid.uuid4()) for n in ("cpl", "pkl", "am", "pic", "snd", "sub", "reel1", "reel2", "kpic", "ksnd", "cc", "img",
                                           "iab", "kiab")}
    keys = {"kpic": os.urandom(16), "ksnd": os.urandom(16), "kiab": os.urandom(16)}
    if a.stereo:
        right_plain = os.path.join(tmp, "pic_r.mxf")
        subprocess.run([a.ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
                        "color=c=red:size=2048x858:rate=24", "-t", "2", "-c:v", "jpeg2000", "-pix_fmt", "xyz12le",
                        "-b:v", "6M", "-f", "mxf", right_plain], check=True)
        stereo_plain = os.path.join(tmp, "pic_3d.mxf")
        stereo_mxf(pic_plain, right_plain, stereo_plain)
        pic_plain = stereo_plain
    pic = os.path.join(a.out, "picture.mxf")
    snd = os.path.join(a.out, "sound.mxf")
    if a.encrypt:
        encrypt_mxf(pic_plain, pic, keys["kpic"], a.openssl, uuid.UUID(ids["pic"]).bytes)
        encrypt_mxf(snd_plain, snd, keys["ksnd"], a.openssl, uuid.UUID(ids["snd"]).bytes)
    else:
        os.replace(pic_plain, pic)
        os.replace(snd_plain, snd)

    iab = os.path.join(a.out, "atmos.mxf")
    if a.iab:
        iab_plain = os.path.join(tmp, "iab.mxf")
        write_iab_mxf(a.iab, iab_plain)
        if a.encrypt:
            encrypt_mxf(iab_plain, iab, keys["kiab"], a.openssl, uuid.UUID(ids["iab"]).bytes)
        else:
            os.replace(iab_plain, iab)

    sub = os.path.join(a.out, "subtitle.xml")
    open(sub, "w", encoding="utf-8").write(f"""<?xml version="1.0" encoding="UTF-8"?>
<DCSubtitle Version="1.0"><SubtitleID>{ids['sub']}</SubtitleID><MovieTitle>Lumen Test</MovieTitle><ReelNumber>1</ReelNumber>
<Language>German</Language>
<Font Id="" Color="FFFFFFFF" Effect="border" EffectColor="FF000000" Italic="no" Size="42" Weight="normal">
<Subtitle SpotNumber="1" TimeIn="00:00:00:050" TimeOut="00:00:00:200" FadeUpTime="5" FadeDownTime="5">
<Text VAlign="bottom" VPosition="12">Erste Zeile</Text><Text VAlign="bottom" VPosition="6"><Font Italic="yes">kursiv</Font> &amp; normal</Text>
</Subtitle>
<Subtitle SpotNumber="2" TimeIn="00:00:00:210" TimeOut="00:00:00:240"><Text VAlign="top" VPosition="8" HAlign="left" HPosition="5">Oben links</Text></Subtitle>
<Subtitle SpotNumber="3" TimeIn="00:00:00:100" TimeOut="00:00:00:240" FadeUpTime="0" FadeDownTime="0"><Image VAlign="top" VPosition="10" HAlign="center">subimage.png</Image></Subtitle>
</Font></DCSubtitle>
""")
    subimg = os.path.join(a.out, "subimage.png")
    write_png(subimg, 600, 80, (255, 210, 0, 255))
    cc = os.path.join(a.out, "captions.xml")
    open(cc, "w", encoding="utf-8").write(f"""<?xml version="1.0" encoding="UTF-8"?>
<DCSubtitle Version="1.0"><SubtitleID>{ids['cc']}</SubtitleID><MovieTitle>Lumen Test</MovieTitle><ReelNumber>1</ReelNumber>
<Language>German</Language><Font Id="" Color="FFFFFFFF" Size="36">
<Subtitle SpotNumber="1" TimeIn="00:00:00:075" TimeOut="00:00:00:240"><Text VAlign="bottom" VPosition="20">[Musik]</Text></Subtitle>
</Font></DCSubtitle>
""")

    def key_id(name):
        return f"<KeyId>urn:uuid:{ids[name]}</KeyId>" if a.encrypt else ""

    def picture_tag(entry):
        body = (f"<Id>urn:uuid:{ids['pic']}</Id><EditRate>24 1</EditRate><IntrinsicDuration>48</IntrinsicDuration><EntryPoint>{entry}</EntryPoint>"
                f"<Duration>24</Duration>{key_id('kpic')}<FrameRate>{'48' if a.stereo else '24'} 1</FrameRate><ScreenAspectRatio>2048 858</ScreenAspectRatio>")
        if a.stereo:
            return f'<msp-cpl:MainStereoscopicPicture xmlns:msp-cpl="http://www.smpte-ra.org/schemas/429-10/2008/Main-Stereo-Picture-CPL">{body}</msp-cpl:MainStereoscopicPicture>'
        return f"<MainPicture>{body}</MainPicture>"

    def aux(entry):
        if not a.iab:
            return ""
        return (f'<axd:AuxData xmlns:axd="http://www.dolby.com/schemas/2012/AD"><Id>urn:uuid:{ids["iab"]}</Id><EditRate>24 1</EditRate>'
                f"<IntrinsicDuration>48</IntrinsicDuration><EntryPoint>{entry}</EntryPoint><Duration>24</Duration>{key_id('kiab')}"
                "<axd:DataType>urn:smpte:ul:060e2b34.04010105.0e090604.00000000</axd:DataType></axd:AuxData>")

    def reel(n, entry):
        return f"""<Reel><Id>urn:uuid:{ids['reel' + str(n)]}</Id><AssetList>
<MainMarkers><Id>urn:uuid:{uuid.uuid4()}</Id><EditRate>24 1</EditRate><IntrinsicDuration>24</IntrinsicDuration><MarkerList>
{'<Marker><Label>FFOC</Label><Offset>0</Offset></Marker>' if n == 1 else '<Marker><Label>FFEC</Label><Offset>12</Offset></Marker><Marker><Label>LFOC</Label><Offset>23</Offset></Marker>'}
</MarkerList></MainMarkers>
{picture_tag(entry)}
<MainSound><Id>urn:uuid:{ids['snd']}</Id><EditRate>24 1</EditRate><IntrinsicDuration>48</IntrinsicDuration><EntryPoint>{entry}</EntryPoint><Duration>24</Duration>{key_id('ksnd')}</MainSound>
{aux(entry)}
{'<MainSubtitle><Id>urn:uuid:' + ids['sub'] + '</Id><EditRate>24 1</EditRate><IntrinsicDuration>24</IntrinsicDuration><EntryPoint>0</EntryPoint><Duration>24</Duration><Language>de</Language></MainSubtitle><cc-cpl:MainClosedCaption xmlns:cc-cpl="http://www.digicine.com/PROTO-ASDCP-CC-CPL-20070926#"><Id>urn:uuid:' + ids['cc'] + '</Id><EditRate>24 1</EditRate><IntrinsicDuration>24</IntrinsicDuration><EntryPoint>0</EntryPoint><Duration>24</Duration><Language>de</Language></cc-cpl:MainClosedCaption>' if n == 1 else ''}
</AssetList></Reel>"""

    cpl = os.path.join(a.out, f"cpl_{ids['cpl']}.xml")
    open(cpl, "w", encoding="utf-8").write(f"""<?xml version="1.0" encoding="UTF-8"?>
<CompositionPlaylist xmlns="http://www.smpte-ra.org/schemas/429-7/2006/CPL"><Id>urn:uuid:{ids['cpl']}</Id>
<AnnotationText>Lumen Test</AnnotationText><IssueDate>2026-09-29T12:00:00+00:00</IssueDate><Issuer>Lumen</Issuer><Creator>make_test_dcp.py</Creator>
<ContentTitleText>LumenTest_FTR_S_DE-XX_51_2K_20260929_SMPTE{'_ENC' if a.encrypt else ''}</ContentTitleText><ContentKind>feature</ContentKind>
<ContentVersion><Id>urn:uuid:{uuid.uuid4()}</Id><LabelText>v1</LabelText></ContentVersion>
<RatingList/><ReelList>{reel(1, 0)}{reel(2, 24)}</ReelList></CompositionPlaylist>
""")
    files = [(ids["cpl"], cpl, "text/xml"), (ids["pic"], pic, "application/mxf"), (ids["snd"], snd, "application/mxf"),
             (ids["sub"], sub, "text/xml"), (ids["cc"], cc, "text/xml"), (ids["img"], subimg, "image/png")]
    if a.iab:
        files.append((ids["iab"], iab, "application/mxf"))
    pkl = os.path.join(a.out, f"pkl_{ids['pkl']}.xml")
    assets = "".join(f"<Asset><Id>urn:uuid:{i}</Id><Hash>{sha1_b64(p)}</Hash><Size>{os.path.getsize(p)}</Size><Type>{t}</Type>"
                     f"<OriginalFileName>{os.path.basename(p)}</OriginalFileName></Asset>" for i, p, t in files)
    open(pkl, "w", encoding="utf-8").write(f"""<?xml version="1.0" encoding="UTF-8"?>
<PackingList xmlns="http://www.smpte-ra.org/schemas/429-8/2007/PKL"><Id>urn:uuid:{ids['pkl']}</Id><IssueDate>2026-09-29T12:00:00+00:00</IssueDate>
<Issuer>Lumen</Issuer><Creator>make_test_dcp.py</Creator><AssetList>{assets}</AssetList></PackingList>
""")
    am_assets = f"<Asset><Id>urn:uuid:{ids['pkl']}</Id><PackingList>true</PackingList><ChunkList><Chunk><Path>{os.path.basename(pkl)}</Path></Chunk></ChunkList></Asset>"
    am_assets += "".join(f"<Asset><Id>urn:uuid:{i}</Id><ChunkList><Chunk><Path>{os.path.basename(p)}</Path></Chunk></ChunkList></Asset>" for i, p, t in files)
    open(os.path.join(a.out, "ASSETMAP.xml"), "w", encoding="utf-8").write(f"""<?xml version="1.0" encoding="UTF-8"?>
<AssetMap xmlns="http://www.smpte-ra.org/schemas/429-9/2007/AM"><Id>urn:uuid:{ids['am']}</Id><VolumeCount>1</VolumeCount>
<IssueDate>2026-09-29T12:00:00+00:00</IssueDate><Issuer>Lumen</Issuer><Creator>make_test_dcp.py</Creator><AssetList>{am_assets}</AssetList></AssetMap>
""")
    open(os.path.join(a.out, "VOLINDEX.xml"), "w").write('<?xml version="1.0"?><VolumeIndex xmlns="http://www.smpte-ra.org/schemas/429-9/2007/AM"><Index>1</Index></VolumeIndex>\n')

    if a.encrypt:
        with open(os.path.join(a.out, "keys.txt"), "w") as f:
            f.write(f"{ids['kpic']} {keys['kpic'].hex()}\n{ids['ksnd']} {keys['ksnd'].hex()}\n")
            if a.iab:
                f.write(f"{ids['kiab']} {keys['kiab'].hex()}\n")
        ciphers = []
        kdm_keys = [("kpic", "MDIK"), ("ksnd", "MDAK")] + ([("kiab", "MDEK")] if a.iab else [])
        for name, ktype in ((n, t.encode()) for n, t in kdm_keys):
            block = (bytes.fromhex("f1dc124460169a0e85bc300642f866ab") + bytes(20) + uuid.UUID(ids["cpl"]).bytes + ktype
                     + uuid.UUID(ids[name]).bytes + a.valid_from.encode() + a.valid_until.encode() + keys[name])
            assert len(block) == 138
            r = subprocess.run([a.openssl, "pkeyutl", "-encrypt", "-certin", "-inkey", a.encrypt,
                                "-pkeyopt", "rsa_padding_mode:oaep"], input=block, capture_output=True, check=True)
            ciphers.append(base64.b64encode(r.stdout).decode())
        serial = subprocess.run([a.openssl, "x509", "-noout", "-serial", "-in", a.encrypt], capture_output=True, text=True, check=True).stdout
        serial = str(int(serial.strip().split("=")[1], 16))
        enc = "".join(f'<enc:EncryptedKey><enc:EncryptionMethod Algorithm="http://www.w3.org/2001/04/xmlenc#rsa-oaep-mgf1p"/>'
                      f"<enc:CipherData><enc:CipherValue>{c}</enc:CipherValue></enc:CipherData></enc:EncryptedKey>" for c in ciphers)
        typed = "".join(f"<TypedKeyId><KeyType>{t}</KeyType><KeyId>urn:uuid:{ids[n]}</KeyId></TypedKeyId>" for n, t in kdm_keys)
        open(os.path.join(a.out, "kdm.xml"), "w", encoding="utf-8").write(f"""<?xml version="1.0" encoding="UTF-8"?>
<DCinemaSecurityMessage xmlns="http://www.smpte-ra.org/schemas/430-3/2006/ETM" xmlns:enc="http://www.w3.org/2001/04/xmlenc#" xmlns:ds="http://www.w3.org/2000/09/xmldsig#">
<AuthenticatedPublic Id="ID_AuthenticatedPublic"><MessageId>urn:uuid:{uuid.uuid4()}</MessageId><MessageType>http://www.smpte-ra.org/430-1/2006/KDM#kdm-key-type</MessageType>
<AnnotationText>Lumen Test KDM</AnnotationText><IssueDate>2026-09-29T12:00:00+00:00</IssueDate>
<RequiredExtensions><KDMRequiredExtensions xmlns="http://www.smpte-ra.org/schemas/430-1/2006/KDM">
<Recipient><X509IssuerSerial><ds:X509IssuerName>test</ds:X509IssuerName><ds:X509SerialNumber>{serial}</ds:X509SerialNumber></X509IssuerSerial><X509SubjectName>test</X509SubjectName></Recipient>
<CompositionPlaylistId>urn:uuid:{ids['cpl']}</CompositionPlaylistId><ContentTitleText>Lumen Test</ContentTitleText>
<ContentKeysNotValidBefore>{a.valid_from}</ContentKeysNotValidBefore><ContentKeysNotValidAfter>{a.valid_until}</ContentKeysNotValidAfter>
<KeyIdList>{typed}</KeyIdList></KDMRequiredExtensions></RequiredExtensions></AuthenticatedPublic>
<AuthenticatedPrivate Id="ID_AuthenticatedPrivate">{enc}</AuthenticatedPrivate></DCinemaSecurityMessage>
""")
    for f in os.listdir(tmp):
        os.remove(os.path.join(tmp, f))
    os.rmdir(tmp)
    print(f"DCP geschrieben: {a.out} (CPL {ids['cpl']}, {'verschlüsselt' if a.encrypt else 'offen'})")


if __name__ == "__main__":
    sys.exit(main())
