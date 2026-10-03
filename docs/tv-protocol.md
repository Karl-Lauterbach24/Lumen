# Lumen TV protocol

How a receiver (the Lumen TV apps, or the page Lumen serves to any browser) talks to Lumen.
Version 1 (`"api": 1`).

Lumen runs a small HTTP server on the local network, port **47800** (the next free port up to
47809 if that one is taken). The receiver is always the client: TV web apps cannot accept
connections, so the TV connects to the computer and asks for commands.

All requests are plain HTTP. Parameters go in the query string or as a JSON body. Every response
carries `Access-Control-Allow-Origin: *`, so web apps on another origin can call it.

## Finding Lumen

There is no announcement on the network. A receiver

1. tries the address it used last time,
2. otherwise requests `GET /api/info` from every address of its own `/24` network on port 47800
   (short timeout, a few dozen requests at a time),
3. otherwise asks the user for the address, which Lumen shows in its Cast dialog.

```
GET /api/info
-> {"name": "Lumen", "host": "<computer name>", "version": "1.2.0", "api": 1, "casting": false}
```

A server is Lumen if `name` is `"Lumen"`.

## Session

```
POST /api/hello?id=<id>&name=<name>&platform=<platform>
-> {"id": "<id>", "server": "<computer name>", "version": "1.2.0", "api": 1}
```

* `id`: chosen by the receiver and kept across starts (any string up to 64 characters). Without it
  Lumen assigns one.
* `name`: what the user sees in Lumen's device list.
* `platform`: `tizen`, `webos`, `android`, `browser`, ...

After `hello` the receiver appears in Lumen's Cast dialog. Nothing is sent to it until the user
selects it there.

```
GET /api/poll?id=<id>&state=<state>
-> {"cmd": "none"}
-> {"cmd": "play", "hls": "http://…/stream/<token>/live.m3u8", "ts": "http://…/stream/<token>/live.ts", "title": "…"}
-> {"cmd": "stop"}
```

Long polling: the request stays open until there is a command, at most 25 seconds, then it answers
`{"cmd": "none"}`. The receiver polls again immediately. `state` reports what the receiver is doing:
`idle`, `buffering`, `playing` or `error`.

* `404` means Lumen does not know the id (it was restarted): send `hello` again.
* A receiver that has not polled for 60 seconds is removed from the list.

```
POST /api/key?id=<id>&key=<key>      -> {"ok": true}
POST /api/bye?id=<id>                -> {"ok": true}
```

`key` forwards the TV remote to Lumen. It is only accepted from the receiver Lumen is currently
casting to (`"ok": false` otherwise). Keys: `up`, `down`, `left`, `right`, `enter`, `back`, `menu`,
`play`, `pause`, `playpause`, `stop`, `rewind`, `forward`, `next`, `prev`.
The arrow keys and `enter` operate disc menus; outside a menu they seek.

## The stream

Lumen encodes exactly what its player window would show (disc menus, subtitles and tone mapping
included) and what it would play as sound:

* H.264 High profile, 1920×1080 or 1280×720, up to 30 or 60 frames per second
* AAC-LC stereo, 48 kHz
* MPEG-TS, one-second segments, each starting with PAT/PMT and a key frame

It is live. Pausing or seeking in Lumen changes what the stream shows; the receiver just keeps
playing. Two forms of the same stream:

* `hls` — HLS playlist (`live.m3u8`, segments `seg<n>.ts`). Players start about three segments
  behind the live edge, so expect three to four seconds of delay.
* `ts` — the continuous MPEG-TS stream, starting at the latest key frame. Lower delay, for players
  that can read MPEG-TS over HTTP directly.

The address contains a random token that changes with every cast session. When the session ends,
the addresses stop working and the receiver gets `stop`.

## Reference implementation

[`receiver/receiver.js`](../receiver/receiver.js) (plain ES5, used by the browser page and the
Tizen and webOS apps) and `tools/mock_cast_devices.py` (`tv_app`, used by the tests).
