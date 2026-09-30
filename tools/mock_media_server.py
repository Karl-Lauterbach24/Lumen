#!/usr/bin/env python3
"""Local test double for Jellyfin/Emby and Plex (tests/media_test.cpp).

    python tools/mock_media_server.py <port> <video-file>

Models the endpoints Lumen uses, following the Jellyfin/Emby and Plex APIs:
  Jellyfin/Emby: POST /Users/AuthenticateByName, /System/Info/Public, /Users/<id>/Views,
                 /Users/<id>/Items (ParentId, SearchTerm), /Users/<id>/Items/Resume,
                 /Videos/<id>/stream?static=true&api_key=, /Sessions/Playing[/Progress|/Stopped]
  Plex:          /plex/identity, /plex/, /plex/library/sections, /plex/library/sections/1/all,
                 /plex/library/metadata/<id>/children, /plex/hubs/search, /plex/library/onDeck,
                 /plex/library/parts/<id>/file.mp4, /plex/:/timeline
  plex.tv:       POST /api/v2/pins, GET /api/v2/pins/<id> (token on the 2nd poll), /api/v2/resources
GET /_log returns the progress reports received (JSON list).
Test credentials only: user "test" / password "test", Plex token "plex-test-token".
"""
import json
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

PORT = int(sys.argv[1])
VIDEO = open(sys.argv[2], "rb").read()
TOKEN = "jf-test-token"
PLEX_TOKEN = "plex-test-token"
USER_ID = "u1"
LOG = []
PIN_POLLS = {"n": 0}

JF_ITEMS = {
    None: [{"Id": "lib-movies", "Name": "Movies", "Type": "CollectionFolder", "IsFolder": True, "CollectionType": "movies"},
           {"Id": "lib-shows", "Name": "Shows", "Type": "CollectionFolder", "IsFolder": True, "CollectionType": "tvshows"}],
    "lib-movies": [{"Id": "m1", "Name": "Test Movie", "Type": "Movie", "MediaType": "Video", "IsFolder": False,
                    "ProductionYear": 2026, "RunTimeTicks": 20000000, "ImageTags": {"Primary": "abc"},
                    "UserData": {"PlaybackPositionTicks": 5000000}}],
    "lib-shows": [{"Id": "s1", "Name": "Test Show", "Type": "Series", "IsFolder": True}],
    "s1": [{"Id": "se1", "Name": "Season 1", "Type": "Season", "IsFolder": True}],
    "se1": [{"Id": "e1", "Name": "Pilot", "Type": "Episode", "MediaType": "Video", "IsFolder": False,
             "ParentIndexNumber": 1, "IndexNumber": 1, "SeriesName": "Test Show", "RunTimeTicks": 20000000}],
}

PLEX_SECTIONS = {"MediaContainer": {"Directory": [{"key": "1", "title": "Filme", "type": "movie"},
                                                  {"key": "2", "title": "Serien", "type": "show"}]}}
PLEX_MOVIE = {"ratingKey": "101", "key": "/library/metadata/101", "type": "movie", "title": "Plex Movie", "year": 2025,
              "duration": 2000, "viewOffset": 700, "thumb": "/library/metadata/101/thumb/1",
              "Media": [{"Part": [{"key": "/library/parts/7/file.mp4"}]}]}
PLEX_SHOW = {"ratingKey": "200", "key": "/library/metadata/200/children", "type": "show", "title": "Plex Show"}
PLEX_SEASON = {"ratingKey": "201", "key": "/library/metadata/201/children", "type": "season", "title": "Staffel 1"}
PLEX_EPISODE = {"ratingKey": "202", "key": "/library/metadata/202", "type": "episode", "title": "Folge 1", "index": 1,
                "parentIndex": 1, "grandparentTitle": "Plex Show", "duration": 2000,
                "Media": [{"Part": [{"key": "/library/parts/8/file.mp4"}]}]}


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def send_json(self, obj, code=200):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def send_video(self):
        rng = self.headers.get("Range")
        start, end = 0, len(VIDEO) - 1
        if rng and rng.startswith("bytes="):
            a, _, b = rng[6:].partition("-")
            start = int(a) if a else 0
            end = int(b) if b else end
        data = VIDEO[start:end + 1]
        self.send_response(206 if rng else 200)
        self.send_header("Content-Type", "video/mp4")
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Content-Length", str(len(data)))
        if rng:
            self.send_header("Content-Range", f"bytes {start}-{end}/{len(VIDEO)}")
        self.end_headers()
        self.wfile.write(data)

    def jf_authorized(self, q):
        auth = (self.headers.get("X-Emby-Token") or "") + (self.headers.get("Authorization") or "")
        return TOKEN in auth or q.get("api_key", [""])[0] == TOKEN

    def plex_authorized(self, q):
        return self.headers.get("X-Plex-Token") == PLEX_TOKEN or q.get("X-Plex-Token", [""])[0] == PLEX_TOKEN

    def do_POST(self):
        u = urlparse(self.path)
        length = int(self.headers.get("Content-Length") or 0)
        body = json.loads(self.rfile.read(length) or b"{}") if length else {}
        if u.path == "/Users/AuthenticateByName":
            if "MediaBrowser" not in (self.headers.get("Authorization") or ""):
                return self.send_json({"error": "missing client header"}, 400)
            if body.get("Username") == "test" and body.get("Pw") == "test":
                return self.send_json({"AccessToken": TOKEN, "ServerId": "srv", "User": {"Id": USER_ID, "Name": "test"}})
            return self.send_json({}, 401)
        if u.path.startswith("/Sessions/Playing"):
            if not self.jf_authorized({}):
                return self.send_json({}, 401)
            LOG.append({"server": "jellyfin", "path": u.path, "body": body})
            self.send_response(204)
            self.end_headers()
            return
        if u.path == "/api/v2/pins":
            return self.send_json({"id": 4711, "code": "abcd"})
        self.send_json({}, 404)

    def do_GET(self):
        u = urlparse(self.path)
        q = parse_qs(u.query)
        p = u.path
        if p == "/_log":
            return self.send_json(LOG)
        # --- Jellyfin/Emby
        if p == "/System/Info/Public":
            return self.send_json({"ServerName": "Mock Jellyfin", "ProductName": "Jellyfin Server"})
        if p.startswith("/Users/") or p.startswith("/Videos/"):
            if not self.jf_authorized(q):
                return self.send_json({}, 401)
        if p == f"/Users/{USER_ID}/Views":
            return self.send_json({"Items": JF_ITEMS[None]})
        if p == f"/Users/{USER_ID}/Items/Resume":
            return self.send_json({"Items": JF_ITEMS["lib-movies"]})
        if p == f"/Users/{USER_ID}/Items":
            if "SearchTerm" in q:
                term = q["SearchTerm"][0].lower()
                hits = [i for items in JF_ITEMS.values() for i in items if term in i["Name"].lower() and not i.get("IsFolder")]
                return self.send_json({"Items": hits})
            return self.send_json({"Items": JF_ITEMS.get(q.get("ParentId", [""])[0], [])})
        if p.startswith("/Videos/") and p.endswith("/stream") and q.get("static") == ["true"]:
            return self.send_video()
        # --- Plex
        if p.startswith("/plex"):
            if not self.plex_authorized(q):
                return self.send_json({}, 401)
            sub = p[5:] or "/"
            if sub == "/identity":
                return self.send_json({"MediaContainer": {"machineIdentifier": "mock"}})
            if sub == "/":
                return self.send_json({"MediaContainer": {"friendlyName": "Mock Plex"}})
            if sub == "/library/sections":
                return self.send_json(PLEX_SECTIONS)
            if sub == "/library/sections/1/all":
                return self.send_json({"MediaContainer": {"Metadata": [PLEX_MOVIE]}})
            if sub == "/library/sections/2/all":
                return self.send_json({"MediaContainer": {"Metadata": [PLEX_SHOW]}})
            if sub == "/library/metadata/200/children":
                return self.send_json({"MediaContainer": {"Metadata": [PLEX_SEASON]}})
            if sub == "/library/metadata/201/children":
                return self.send_json({"MediaContainer": {"Metadata": [PLEX_EPISODE]}})
            if sub == "/library/onDeck":
                return self.send_json({"MediaContainer": {"Metadata": [PLEX_EPISODE]}})
            if sub == "/hubs/search":
                return self.send_json({"MediaContainer": {"Hub": [{"type": "movie", "Metadata": [PLEX_MOVIE]}]}})
            if sub.startswith("/library/parts/"):
                return self.send_video()
            if sub == "/:/timeline":
                LOG.append({"server": "plex", "path": sub, "query": {k: v[0] for k, v in q.items()}})
                return self.send_json({})
        # --- plex.tv
        if p == "/api/v2/pins/4711":
            PIN_POLLS["n"] += 1
            return self.send_json({"id": 4711, "code": "abcd", "authToken": PLEX_TOKEN if PIN_POLLS["n"] >= 2 else None})
        if p == "/api/v2/resources":
            if self.headers.get("X-Plex-Token") != PLEX_TOKEN:
                return self.send_json({}, 401)
            return self.send_json([
                {"name": "Mock Plex (Konto)", "provides": "server", "accessToken": PLEX_TOKEN,
                 "connections": [{"uri": "http://10.255.255.1:32400", "local": False, "relay": True},
                                 {"uri": f"http://127.0.0.1:{PORT}/plex", "local": True, "relay": False}]},
                {"name": "Player", "provides": "player", "connections": []},
            ])
        self.send_json({}, 404)


ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
