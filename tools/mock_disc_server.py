#!/usr/bin/env python3
"""Local test double for the web services the "disc-identify" plugin uses (tests/discinfo_test.cpp).

    python tools/mock_disc_server.py <port>

  MusicBrainz:  GET /ws/2/discid/<disc-id>?fmt=json&inc=...&toc=...   (404 for unknown IDs)
  Wikidata:     GET /w/api.php?action=wbsearchentities&search=<label>
GET /_log returns the requests received (JSON list: path, query, user-agent).
The answers have the shape of the real services, reduced to the fields a client needs.
"""
import json
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

PORT = int(sys.argv[1])
LOG = []

# Disc ID of the table of contents 1..3, lead-out 600, offsets 150/300/450 (tests/discinfo_test.cpp)
TEST_DISC_ID = "IWFVjcDW_sHbcouxZ99EXu2rOyw-"
RELEASE_ID = "11111111-2222-3333-4444-555555555555"

ARTIST = [{"name": "Test Artist", "joinphrase": "", "artist": {"id": "a1", "name": "Test Artist"}}]
RELEASE = {
    "id": RELEASE_ID,
    "title": "Test Album",
    "date": "1999-05-17",
    "artist-credit": ARTIST,
    "cover-art-archive": {"front": True, "back": False, "artwork": True, "count": 1, "darkened": False},
    "media": [
        # a second disc of the set comes first: the client must pick the medium with the matching disc ID
        {"position": 1, "format": "CD", "track-count": 2, "discs": [{"id": "other-disc-id"}],
         "tracks": [{"number": "1", "title": "Wrong Disc 1", "artist-credit": ARTIST},
                    {"number": "2", "title": "Wrong Disc 2", "artist-credit": ARTIST}]},
        {"position": 2, "format": "CD", "track-count": 3, "discs": [{"id": TEST_DISC_ID}],
         "tracks": [{"number": "1", "title": "First Song", "artist-credit": ARTIST},
                    {"number": "2", "title": "Second Song",
                     "artist-credit": [{"name": "Test Artist", "joinphrase": " feat. "}, {"name": "A Guest", "joinphrase": ""}]},
                    {"number": "3", "title": "Third Söng", "artist-credit": ARTIST}]},
    ],
}

WIKIDATA = {
    "the matrix": [
        {"id": "Q83495000", "label": "The Matrix", "description": "media franchise"},
        {"id": "Q83495", "label": "The Matrix", "description": "1999 film directed by the Wachowskis"},
    ],
}


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

    def do_GET(self):
        u = urlparse(self.path)
        q = parse_qs(u.query)
        if u.path == "/_log":
            return self.send_json(LOG)
        LOG.append({"path": u.path, "query": u.query, "user-agent": self.headers.get("User-Agent", "")})
        if u.path.startswith("/ws/2/discid/"):
            disc_id = u.path.rsplit("/", 1)[1]
            if disc_id == TEST_DISC_ID:
                return self.send_json({"id": disc_id, "sectors": 600, "offset-count": 3, "offsets": [150, 300, 450],
                                       "releases": [RELEASE]})
            return self.send_json({"error": "Not Found", "help": "see the MusicBrainz API documentation"}, 404)
        if u.path == "/w/api.php" and q.get("action", [""])[0] == "wbsearchentities":
            term = q.get("search", [""])[0].lower()
            return self.send_json({"searchinfo": {"search": term}, "search": WIKIDATA.get(term, []), "success": 1})
        self.send_json({}, 404)


ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
