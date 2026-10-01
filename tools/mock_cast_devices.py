#!/usr/bin/env python3
"""Local test doubles for cast receivers (tests/cast_test.cpp).

    python tools/mock_cast_devices.py <cert.pem> <key.pem>

Starts, on free local ports:
  dlna         UPnP MediaRenderer: /desc.xml, AVTransport control (SetAVTransportURI, Play, Stop)
  airplay      AirPlay video: POST /play, GET /playback-info, POST /stop
  locked       AirPlay device that demands pairing: POST /play -> 403
  chromecast   CASTV2 over TLS: CONNECT, LAUNCH (CC1AD845), LOAD, STOP, heartbeat
A device "plays" by fetching the stream Lumen names: HLS playlist + first segment, or the first
bytes of the continuous MPEG-TS stream, and checks that it is MPEG-TS.

Prints one JSON object per line on stdout:
  {"ready": {"dlna": port, "airplay": port, "locked": port, "chromecast": port}}
  {"device": "dlna", "event": "play", "url": "...", "ok": true, "detail": "..."}
Commands on stdin (one per line):
  tv <base-url>     act as a Lumen TV app: hello, poll, play what Lumen sends, send a remote key
  quit
"""
import json
import socket
import socketserver
import ssl
import struct
import sys
import threading
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urljoin
from xml.sax.saxutils import unescape

CERT, KEY = sys.argv[1], sys.argv[2]
LOCK = threading.Lock()
OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def emit(obj):
    with LOCK:
        sys.stdout.write(json.dumps(obj) + "\n")
        sys.stdout.flush()


def is_ts(data):
    return len(data) >= 188 * 3 and data[0] == 0x47 and data[188] == 0x47 and data[376] == 0x47


def fetch_hls(url):
    """Playlist + first segment. Returns (ok, detail)."""
    try:
        text = OPENER.open(url, timeout=10).read().decode()
        segs = [l for l in text.splitlines() if l and not l.startswith("#")]
        if not text.startswith("#EXTM3U") or not segs:
            return False, "no segments in playlist"
        data = OPENER.open(urljoin(url, segs[0]), timeout=10).read()
        return is_ts(data), "%d segments, first %d bytes" % (len(segs), len(data))
    except Exception as e:  # noqa: BLE001 - reported to the test
        return False, repr(e)


def fetch_ts(url):
    """First bytes of the continuous stream. Returns (ok, detail)."""
    try:
        r = OPENER.open(url, timeout=10)
        data = b""
        while len(data) < 188 * 400:
            chunk = r.read(188 * 50)
            if not chunk:
                break
            data += chunk
        headers = {k.lower(): v for k, v in r.headers.items()}
        r.close()
        ok = is_ts(data) and "contentfeatures.dlna.org" in headers
        return ok, "%d bytes, type %s" % (len(data), headers.get("content-type"))
    except Exception as e:  # noqa: BLE001
        return False, repr(e)


class Server(ThreadingHTTPServer):
    """HTTPServer looks up the host name of its address when binding; on some systems (macOS CI)
    that takes many seconds per server."""

    def server_bind(self):
        socketserver.TCPServer.server_bind(self)
        self.server_name, self.server_port = self.server_address[:2]


class Quiet(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def body(self):
        return self.rfile.read(int(self.headers.get("Content-Length") or 0))

    def reply(self, status, body=b"", ctype="text/plain"):
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        try:
            self.wfile.write(body)
        except OSError:
            pass  # the sender closed the connection right after its last request


# ---------------------------------------------------------------- DLNA
class Dlna(Quiet):
    uri = None

    def do_GET(self):
        if self.path != "/desc.xml":
            return self.reply(404)
        # control URL relative, service list as real renderers send it
        self.reply(200, b"""<?xml version="1.0"?>
<root xmlns="urn:schemas-upnp-org:device-1-0"><device>
<deviceType>urn:schemas-upnp-org:device:MediaRenderer:1</deviceType>
<friendlyName>Test TV &amp; Renderer</friendlyName><modelName>MockRenderer</modelName>
<UDN>uuid:11111111-2222-3333-4444-555555555555</UDN>
<serviceList>
<service><serviceType>urn:schemas-upnp-org:service:RenderingControl:1</serviceType>
<controlURL>/RenderingControl/control</controlURL></service>
<service><serviceType>urn:schemas-upnp-org:service:AVTransport:1</serviceType>
<controlURL>AVTransport/control</controlURL></service>
</serviceList></device></root>""", "text/xml")

    def do_POST(self):
        body = self.body().decode()
        action = (self.headers.get("SOAPACTION") or "").strip('"').split("#")[-1]
        if self.path != "/AVTransport/control":
            return self.reply(404)
        if action == "SetAVTransportURI":
            a, b = body.find("<CurrentURI>"), body.find("</CurrentURI>")
            Dlna.uri = unescape(body[a + 12:b])
            meta_ok = "DLNA.ORG_OP" in unescape(body) and "object.item.videoItem" in unescape(body)
            emit({"device": "dlna", "event": "seturi", "url": Dlna.uri, "ok": meta_ok})
        elif action == "Play":
            ok, detail = fetch_ts(Dlna.uri)
            emit({"device": "dlna", "event": "play", "url": Dlna.uri, "ok": ok, "detail": detail})
        elif action == "Stop":
            emit({"device": "dlna", "event": "stop", "ok": True})
        else:
            return self.reply(500, b"<errorDescription>Invalid Action</errorDescription>", "text/xml")
        self.reply(200, ('<?xml version="1.0"?><s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/"><s:Body>'
                         '<u:%sResponse xmlns:u="urn:schemas-upnp-org:service:AVTransport:1"/></s:Body></s:Envelope>' % action).encode(),
                   "text/xml")


# ---------------------------------------------------------------- AirPlay
def airplay_handler(name, locked):
    class Airplay(Quiet):
        def do_GET(self):
            self.reply(200, b"<plist><dict/></plist>", "text/x-apple-plist+xml")

        def do_POST(self):
            body = self.body().decode()
            if self.path == "/play":
                if locked:
                    emit({"device": name, "event": "refused", "ok": True})
                    return self.reply(403)
                url = ""
                for line in body.splitlines():
                    if line.lower().startswith("content-location:"):
                        url = line.split(":", 1)[1].strip()
                ok, detail = fetch_hls(url)
                emit({"device": name, "event": "play", "url": url, "ok": ok, "detail": detail,
                      "session": bool(self.headers.get("X-Apple-Session-ID"))})
                return self.reply(200 if ok else 500)
            if self.path == "/stop":
                emit({"device": name, "event": "stop", "ok": True})
            self.reply(200)
    return Airplay


# ---------------------------------------------------------------- Chromecast
def varint(n):
    out = b""
    while n >= 0x80:
        out += bytes([n & 0x7F | 0x80])
        n >>= 7
    return out + bytes([n])


def field(num, data):
    return varint(num << 3 | 2) + varint(len(data)) + data


def cast_message(src, dst, ns, payload):
    msg = varint(1 << 3) + varint(0) + field(2, src.encode()) + field(3, dst.encode()) + field(4, ns.encode())
    msg += varint(5 << 3) + varint(0) + field(6, json.dumps(payload).encode())
    return struct.pack(">I", len(msg)) + msg


def parse_cast(msg):
    out, pos = {}, 0

    def read_varint():
        nonlocal pos
        v, shift = 0, 0
        while True:
            b = msg[pos]
            pos += 1
            v |= (b & 0x7F) << shift
            shift += 7
            if not b & 0x80:
                return v

    while pos < len(msg):
        key = read_varint()
        if key & 7 == 0:
            out[key >> 3] = read_varint()
        else:
            n = read_varint()
            out[key >> 3] = msg[pos:pos + n]
            pos += n
    return out


def chromecast_client(conn):
    def recv(n):
        data = b""
        while len(data) < n:
            chunk = conn.recv(n - len(data))
            if not chunk:
                raise EOFError
            data += chunk
        return data

    def send(src, dst, ns, payload):
        conn.sendall(cast_message(src, dst, ns, payload))

    app = {"appId": "CC1AD845", "displayName": "Default Media Receiver", "sessionId": "sess-1", "transportId": "web-1",
           "namespaces": [{"name": "urn:x-cast:com.google.cast.media"}]}
    connected = set()
    try:
        while True:
            (length,) = struct.unpack(">I", recv(4))
            m = parse_cast(recv(length))
            src, dst, ns = m[2].decode(), m[3].decode(), m[4].decode()
            p = json.loads(m[6].decode())
            t = p.get("type")
            if ns.endswith("tp.connection"):
                if t == "CONNECT":
                    connected.add(dst)
            elif ns.endswith("tp.heartbeat"):
                if t == "PING":
                    send(dst, src, ns, {"type": "PONG"})
            elif ns.endswith("cast.receiver"):
                if t == "LAUNCH":
                    ok = p.get("appId") == "CC1AD845" and "receiver-0" in connected
                    emit({"device": "chromecast", "event": "launch", "ok": ok})
                    send("receiver-0", "*", ns, {"type": "RECEIVER_STATUS", "requestId": p.get("requestId"),
                                                 "status": {"applications": [app], "volume": {"level": 1.0}}})
                elif t == "STOP":
                    emit({"device": "chromecast", "event": "stop", "ok": p.get("sessionId") == "sess-1"})
            elif ns.endswith("cast.media") and t == "LOAD":
                media = p.get("media", {})
                ok, detail = fetch_hls(media.get("contentId", ""))
                ok = ok and dst == "web-1" and "web-1" in connected and media.get("contentType") == "application/x-mpegURL" \
                    and media.get("streamType") == "LIVE"
                emit({"device": "chromecast", "event": "play", "url": media.get("contentId"), "ok": ok, "detail": detail})
                send("web-1", src, ns, {"type": "MEDIA_STATUS" if ok else "LOAD_FAILED", "requestId": p.get("requestId"),
                                        "status": [{"mediaSessionId": 1, "playerState": "PLAYING"}]})
    except (EOFError, OSError, ssl.SSLError):
        pass
    finally:
        conn.close()


def chromecast_server(sock):
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(CERT, KEY)
    while True:
        raw, _ = sock.accept()
        try:
            conn = ctx.wrap_socket(raw, server_side=True)
        except (ssl.SSLError, OSError):
            continue
        threading.Thread(target=chromecast_client, args=(conn,), daemon=True).start()


# ---------------------------------------------------------------- Lumen TV app
def tv_app(base):
    def call(method, path):
        req = urllib.request.Request(base + path, method=method, data=b"" if method == "POST" else None)
        return json.loads(OPENER.open(req, timeout=40).read().decode())

    try:
        info = call("GET", "/api/info")
        hello = call("POST", "/api/hello?id=mock-tv-1&name=Mock%20TV&platform=test")
        emit({"device": "tv", "event": "hello", "ok": info.get("name") == "Lumen" and hello.get("id") == "mock-tv-1"})
        page = OPENER.open(base + "/", timeout=10).read().decode()
        script = OPENER.open(base + "/receiver.js", timeout=10).read().decode()
        emit({"device": "tv", "event": "page", "ok": "receiver.js" in page and "lumen.id" in script})
        refused = call("POST", "/api/key?id=mock-tv-1&key=enter")
        emit({"device": "tv", "event": "key-before-play", "ok": refused.get("ok") is False})
        state = "idle"
        while True:
            cmd = call("GET", "/api/poll?id=mock-tv-1&state=" + state)
            if cmd.get("cmd") == "play":
                ok, detail = fetch_hls(cmd.get("hls", ""))
                ok2, detail2 = fetch_ts(cmd.get("ts", ""))
                emit({"device": "tv", "event": "play", "url": cmd.get("hls"), "ok": ok and ok2, "detail": detail + "; " + detail2})
                state = "playing" if ok and ok2 else "error"
                key = call("POST", "/api/key?id=mock-tv-1&key=enter")
                emit({"device": "tv", "event": "key", "ok": key.get("ok") is True})
            elif cmd.get("cmd") == "stop":
                emit({"device": "tv", "event": "stop", "ok": True})
                state = "idle"
    except Exception as e:  # noqa: BLE001
        emit({"device": "tv", "event": "ended", "detail": repr(e)})


def main():
    servers = {
        "dlna": Server(("127.0.0.1", 0), Dlna),
        "airplay": Server(("127.0.0.1", 0), airplay_handler("airplay", False)),
        "locked": Server(("127.0.0.1", 0), airplay_handler("locked", True)),
    }
    for s in servers.values():
        threading.Thread(target=s.serve_forever, daemon=True).start()
    cc = socket.socket()
    cc.bind(("127.0.0.1", 0))
    cc.listen(5)
    threading.Thread(target=chromecast_server, args=(cc,), daemon=True).start()
    ports = {k: s.server_address[1] for k, s in servers.items()}
    ports["chromecast"] = cc.getsockname()[1]
    emit({"ready": ports})
    for line in sys.stdin:
        parts = line.split()
        if not parts:
            continue
        if parts[0] == "tv":
            threading.Thread(target=tv_app, args=(parts[1],), daemon=True).start()
        elif parts[0] == "quit":
            break


if __name__ == "__main__":
    main()
